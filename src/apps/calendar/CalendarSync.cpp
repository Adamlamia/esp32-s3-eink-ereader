// ===========================================================================
//  CalendarSync.cpp  —  Wi-Fi + NTP + HTTPS ICS sync session (Round 2)
// ===========================================================================
//  See CalendarSync.h for the design notes (portal coordination, task stack,
//  secrets convention). Everything here reuses the Round-1 seams:
//    core::parseIcsFeed     — RFC 5545 parsing (per feed)
//    core::todayStartUtc / nextNDaysUtc — sync window anchored at "now"
//    core::expandAndCollect — recurrence expansion + window filter + sort
//  and hands the materialised events to CalendarStore for persistence.
// ===========================================================================
#include "CalendarSync.h"
#include "CalendarStore.h"
#include "app/AppManager.h"
#include "core/IcsParser.h"
#include "core/CalendarDate.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <string.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// --- Session tuning ---------------------------------------------------------
static const int     SYNC_WIFI_TRIES   = 30;    // x 500 ms  -> <= 15 s Wi-Fi join
static const int     SYNC_NTP_TRIES    = 30;    // x 500 ms  -> <= 15 s NTP wait
static const int64_t SYNC_NTP_MIN_EPOCH = CAL_CLOCK_MIN_EPOCH;  // shared "clock is sane" floor (config.h)
static const size_t  SYNC_BODY_MAX     = 32768; // reject absurdly large ICS bodies
static const int     SYNC_HTTP_TIMEOUT_MS = 10000;
static const int     SYNC_HTTP_CONNECT_MS = 8000;
static const size_t  SYNC_TASK_STACK_BYTES = 24 * 1024;  // see header (8 KB parse buf + TLS)

// ===========================================================================
//  Shared STA + NTP plumbing (reused by run() and syncTimeOnly())
// ===========================================================================
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
// Bring up STA-only Wi-Fi and obtain a valid NTP time. Returns true and sets
// nowUtc (TRUE UTC epoch seconds) on success; on failure returns false with a
// short readable reason in msg. The CALLER owns the radio lifecycle (holds the
// RAII WifiOffGuard), so this never leaves Wi-Fi on by itself.
//
// Clock convention: configTime()'s gmtOffset only steers localtime(); once NTP
// has fixed the clock, time(nullptr) returns TRUE UTC epoch seconds directly.
// We therefore use it with NO offset subtraction — this matches the true-UTC
// convention of the ICS parser and the core date seams (see header; this
// corrects a Round-2 latent bug that subtracted CAL_TZ_OFFSET_SEC here).
bool CalendarSync::staNtp(int64_t &nowUtc, char *msg, size_t msgLen) {
    // --- STA-only Wi-Fi ---
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
    Serial.println("[CalSync] joining Wi-Fi (STA)...");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < SYNC_WIFI_TRIES) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        if (msg) snprintf(msg, msgLen, "Wi-Fi connect failed");
        Serial.printf("[CalSync] Wi-Fi connect failed (status %d)\n", WiFi.status());
        return false;
    }
    Serial.printf("[CalSync] Wi-Fi up, IP %s\n", WiFi.localIP().toString().c_str());

    // --- NTP time sync (fixed UTC+8, no DST — Malaysia) ---
    configTime(CAL_TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
    time_t nowT = 0;
    for (int i = 0; i < SYNC_NTP_TRIES; i++) {
        nowT = time(nullptr);
        if ((int64_t)nowT > SYNC_NTP_MIN_EPOCH) break;
        delay(500);
    }
    if ((int64_t)nowT <= SYNC_NTP_MIN_EPOCH) {
        if (msg) snprintf(msg, msgLen, "NTP time sync failed");
        Serial.println("[CalSync] NTP gave no valid time");
        return false;
    }
    nowUtc = (int64_t)nowT;   // time() is already TRUE UTC (no offset subtraction)
    return true;
}
#endif // WIFI_STA_SSID && WIFI_STA_PASS

// ===========================================================================
//  Session body (runs on the dedicated sync task — NOT the loop stack)
// ===========================================================================
CalendarSyncResult CalendarSync::runInternal() {
    CalendarSyncResult r = {};   // zeroed: ok=false, counts 0, message ""

    // --- Wi-Fi coordination (see header): never fight the portal over the
    // radio. isRunning() covers both the auto-started boot portal and one the
    // user toggled on by hand (AppManager::webUserManaged() is then also
    // true); either way the async HTTP server is live and a WiFi.mode()
    // switch would break it. webUserManaged() with the portal STOPPED is
    // fine: the radio is off and ours to use.
    if (_ctx.portal && _ctx.portal->isRunning()) {
        snprintf(r.message, sizeof(r.message), "Wi-Fi portal active - stop it first");
        Serial.printf("[CalSync] aborted: WebPortal running (userManaged=%d)\n",
                      _ctx.manager ? (int)_ctx.manager->webUserManaged() : -1);
        return r;
    }

#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    // RAII guard: Wi-Fi is powered down on EVERY exit path below — success,
    // failure, early return — so the radio never stays hot after a sync.
    struct WifiOffGuard { ~WifiOffGuard() { WiFi.mode(WIFI_OFF); } } wifiGuard;

    // --- Feed table ---------------------------------------------------------
    // Each CAL_ICS_URL_n is individually optional; the ORIGINAL index n is
    // kept as the event category so CAL_ICS_LABEL_n lines up in the UI even
    // when only e.g. URL_0 and URL_2 are defined.
    struct Feed { const char *url; uint8_t idx; };
    Feed feeds[CAL_MAX_CALENDARS];
    int  feedCount = 0;
#if defined(CAL_ICS_URL_0)
    feeds[feedCount++] = { CAL_ICS_URL_0, 0 };
#endif
#if defined(CAL_ICS_URL_1)
    feeds[feedCount++] = { CAL_ICS_URL_1, 1 };
#endif
#if defined(CAL_ICS_URL_2)
    feeds[feedCount++] = { CAL_ICS_URL_2, 2 };
#endif
#if defined(CAL_ICS_URL_3)
    feeds[feedCount++] = { CAL_ICS_URL_3, 3 };
#endif
    (void)feeds;
    if (feedCount == 0) {
        snprintf(r.message, sizeof(r.message), "No CAL_ICS_URL_n in secrets.h");
        Serial.println("[CalSync] aborted: no ICS feeds defined");
        return r;
    }

    // --- 1+2. STA-only Wi-Fi + NTP (shared plumbing) -------------------------
    // staNtp() brings up WIFI_STA, joins the network and runs configTime()/NTP,
    // returning a valid TRUE-UTC "now" (see header: time() is UTC already, so
    // there is no offset subtraction). On any failure it fills r.message with a
    // readable reason and returns false; the WifiOffGuard powers the radio down.
    int64_t nowUtc = 0;
    if (!staNtp(nowUtc, r.message, sizeof(r.message))) return r;
    Serial.printf("[CalSync] time synced, nowUtc=%lld\n", (long long)nowUtc);

    // --- 3. Fetch + parse each feed ------------------------------------------
    // Heap-backed buffers (vectors): the event arrays are ~6.5 KB each and
    // must NOT live on any task stack; vectors also guarantee no leaks across
    // the many early-return paths.
    std::vector<core::CalendarEvent> feedBuf(CAL_MAX_EVENTS);
    std::vector<core::CalendarEvent> merged;
    merged.reserve(CAL_MAX_EVENTS);

    for (int fi = 0; fi < feedCount; fi++) {
        bool feedOk = false;
        WiFiClientSecure client;
        // SECURITY NOTE: setInsecure() skips certificate validation; the
        // secret ICS URL itself (with its embedded private API key) is the
        // credential, so it lives only in git-ignored src/secrets.h.
        client.setInsecure();
        HTTPClient http;
        http.setConnectTimeout(SYNC_HTTP_CONNECT_MS);
        http.setTimeout(SYNC_HTTP_TIMEOUT_MS);
        if (http.begin(client, feeds[fi].url)) {
            int code = http.GET();
            // R4: when Content-Length is known, reject oversized bodies
            // BEFORE allocating them — getString() would otherwise pull the
            // whole (possibly multi-MB) body onto the heap first. Chunked or
            // unknown sizes (-1) are still caught by the post-fetch check.
            int declared = http.getSize();
            if (code == HTTP_CODE_OK && declared > (int)SYNC_BODY_MAX) {
                Serial.printf("[CalSync] feed %d: body too large (%d bytes)\n",
                              feeds[fi].idx, declared);
            } else if (code == HTTP_CODE_OK) {
                String body = http.getString();
                if (body.length() > 0 && body.length() <= SYNC_BODY_MAX) {
                    // NOTE: parseIcsFeed unfolds into its own 8 KB buffer and
                    // truncates longer feeds; a 14-day slice of a Google
                    // calendar is well under that.
                    int n = core::parseIcsFeed(body.c_str(), feeds[fi].idx,
                                               feedBuf.data(), CAL_MAX_EVENTS,
                                               CAL_TZ_OFFSET_SEC);
                    for (int j = 0; j < n && (int)merged.size() < CAL_MAX_EVENTS; j++)
                        merged.push_back(feedBuf[j]);
                    Serial.printf("[CalSync] feed %d: %d events parsed\n", feeds[fi].idx, n);
                    feedOk = true;
                } else {
                    Serial.printf("[CalSync] feed %d: bad body (%u bytes)\n",
                                  feeds[fi].idx, body.length());
                }
            } else {
                Serial.printf("[CalSync] feed %d: HTTP error %d\n", feeds[fi].idx, code);
            }
            http.end();
        } else {
            Serial.printf("[CalSync] feed %d: begin() failed\n", feeds[fi].idx);
        }
        if (feedOk) r.feedsOk++;
        else        r.feedsFailed++;
    }

    if (r.feedsOk == 0) {
        snprintf(r.message, sizeof(r.message), "All %d feed(s) failed", r.feedsFailed);
        return r;
    }

    // --- 4. Expand recurrence over the sync window + persist ------------------
    int64_t winStart = core::todayStartUtc(nowUtc, CAL_TZ_OFFSET_SEC);
    int64_t winEnd   = core::nextNDaysUtc(nowUtc, CAL_SYNC_WINDOW_DAYS, CAL_TZ_OFFSET_SEC);
    std::vector<core::CalendarEvent> expanded(CAL_MAX_EVENTS);
    int nOut = core::expandAndCollect(merged.data(), (int)merged.size(),
                                      winStart, winEnd,
                                      expanded.data(), CAL_MAX_EVENTS,
                                      CAL_TZ_OFFSET_SEC);

    CalendarStore store(_ctx.storage.fs());
    if (!store.save(expanded.data(), nOut, nowUtc)) {
        snprintf(r.message, sizeof(r.message), "Cache write failed");
        return r;
    }

    r.eventsFetched = nOut;
    r.ok = true;
    if (r.feedsFailed == 0)
        snprintf(r.message, sizeof(r.message), "%d events from %d feed(s)", nOut, r.feedsOk);
    else
        snprintf(r.message, sizeof(r.message), "%d events, %d feed(s) failed", nOut, r.feedsFailed);
    Serial.printf("[CalSync] done: %s\n", r.message);
    return r;   // wifiGuard: WiFi.mode(WIFI_OFF) here
#else
    // No STA credentials compiled in: fail loudly and readably, never crash.
    snprintf(r.message, sizeof(r.message), "No Wi-Fi secrets (src/secrets.h)");
    Serial.println("[CalSync] WIFI_STA_SSID/PASS not defined - sync disabled");
    return r;
#endif
}

// ===========================================================================
//  Time-only session body (STA + NTP, no feed fetch — Round 3)
// ===========================================================================
//  Gives the scheduler a valid clock after a power cycle (no battery-backed
//  RTC) without paying for a full ICS fetch. Mirrors runInternal()'s portal
//  guard and RAII Wi-Fi-off contract exactly; only the feed loop is omitted.
CalendarSyncResult CalendarSync::syncTimeOnlyInternal() {
    CalendarSyncResult r = {};   // zeroed: ok=false, counts 0, message ""

    // Never fight the portal over the radio (same rule as the full sync).
    if (_ctx.portal && _ctx.portal->isRunning()) {
        snprintf(r.message, sizeof(r.message), "Wi-Fi portal active - stop it first");
        Serial.printf("[CalSync] time-only aborted: WebPortal running (userManaged=%d)\n",
                      _ctx.manager ? (int)_ctx.manager->webUserManaged() : -1);
        return r;
    }

#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    struct WifiOffGuard { ~WifiOffGuard() { WiFi.mode(WIFI_OFF); } } wifiGuard;

    int64_t nowUtc = 0;
    if (!staNtp(nowUtc, r.message, sizeof(r.message))) return r;

    r.ok = true;
    snprintf(r.message, sizeof(r.message), "Time synced (NTP)");
    Serial.printf("[CalSync] time-only synced, nowUtc=%lld\n", (long long)nowUtc);
    return r;   // wifiGuard: WiFi.mode(WIFI_OFF) here
#else
    snprintf(r.message, sizeof(r.message), "No Wi-Fi secrets (src/secrets.h)");
    Serial.println("[CalSync] WIFI_STA_SSID/PASS not defined - time sync disabled");
    return r;
#endif
}

// ===========================================================================
//  Blocking entry points (loop-stack safe: work happens on a 24 KB task)
// ===========================================================================
namespace {
struct SyncJob {
    CalendarSync       *sync;
    CalendarSyncResult *out;
    SemaphoreHandle_t   done;
    bool                timeOnly;   // false -> runInternal, true -> syncTimeOnlyInternal
};

void syncTaskEntry(void *arg) {
    SyncJob *job = static_cast<SyncJob *>(arg);
    *job->out = job->timeOnly ? job->sync->syncTimeOnlyInternal()
                              : job->sync->runInternal();
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);   // task frees itself; job is owned by the caller
}
} // namespace

// Common trampoline shared by run() and syncTimeOnly(): spawn the session body
// on the dedicated high-stack task and block the caller until it completes.
CalendarSyncResult CalendarSync::runOnTask(bool timeOnly) {
    CalendarSyncResult r = {};

    SyncJob job;
    job.sync     = this;
    job.out      = &r;
    job.timeOnly = timeOnly;
    job.done     = xSemaphoreCreateBinary();
    if (!job.done) {
        snprintf(r.message, sizeof(r.message), "Sync task alloc failed");
        return r;
    }

    // NOTE: on ESP-IDF, xTaskCreate's stack size is in BYTES (unlike vanilla
    // FreeRTOS words). 24 KB covers parseIcsFeed's 8 KB buffer + mbedTLS for the
    // full sync; the time-only path reuses the same task for identical
    // stack-safety and Wi-Fi-lifecycle behaviour (a little over-provisioned,
    // but only for the few seconds the pass runs).
    BaseType_t started = xTaskCreate(syncTaskEntry, "calSync",
                                     SYNC_TASK_STACK_BYTES, &job, 1, nullptr);
    if (started != pdPASS) {
        vSemaphoreDelete(job.done);
        snprintf(r.message, sizeof(r.message), "Sync task create failed");
        return r;
    }

    // The wait is bounded in practice: the session has internal timeouts
    // (Wi-Fi <= 15 s, NTP <= 15 s, HTTP <= ~18 s per feed), so it cannot hang.
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return r;
}

CalendarSyncResult CalendarSync::run()          { return runOnTask(false); }
CalendarSyncResult CalendarSync::syncTimeOnly() { return runOnTask(true);  }
