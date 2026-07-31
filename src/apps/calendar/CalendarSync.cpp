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
static const int64_t SYNC_NTP_MIN_EPOCH = INT64_C(1700000000);  // 2023-11: "clock is sane"
static const size_t  SYNC_BODY_MAX     = 32768; // reject absurdly large ICS bodies
static const int     SYNC_HTTP_TIMEOUT_MS = 10000;
static const int     SYNC_HTTP_CONNECT_MS = 8000;
static const size_t  SYNC_TASK_STACK_BYTES = 24 * 1024;  // see header (8 KB parse buf + TLS)

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

    // --- 1. STA-only Wi-Fi ---------------------------------------------------
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
    Serial.println("[CalSync] joining Wi-Fi (STA)...");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < SYNC_WIFI_TRIES) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(r.message, sizeof(r.message), "Wi-Fi connect failed");
        Serial.printf("[CalSync] Wi-Fi connect failed (status %d)\n", WiFi.status());
        return r;   // guard powers the radio down
    }
    Serial.printf("[CalSync] Wi-Fi up, IP %s\n", WiFi.localIP().toString().c_str());

    // --- 2. NTP time sync ----------------------------------------------------
    // NTP approach: configTime() with the FIXED UTC+8 offset and no DST
    // (Malaysia). After this call time(nullptr) returns LOCAL wall-clock
    // seconds (UTC + CAL_TZ_OFFSET_SEC), so the true UTC instant is recovered
    // by subtracting the offset. No timezone database is involved — this
    // matches the Round-1 parser's fixed-offset model exactly.
    configTime(CAL_TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
    time_t nowT = 0;
    for (int i = 0; i < SYNC_NTP_TRIES; i++) {
        nowT = time(nullptr);
        if ((int64_t)nowT > SYNC_NTP_MIN_EPOCH) break;
        delay(500);
    }
    if ((int64_t)nowT <= SYNC_NTP_MIN_EPOCH) {
        snprintf(r.message, sizeof(r.message), "NTP time sync failed");
        Serial.println("[CalSync] NTP gave no valid time");
        return r;
    }
    int64_t nowUtc = (int64_t)nowT - CAL_TZ_OFFSET_SEC;   // local -> UTC
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
            if (code == HTTP_CODE_OK) {
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
//  Blocking entry point (loop-stack safe: work happens on a 24 KB task)
// ===========================================================================
namespace {
struct SyncJob {
    CalendarSync       *sync;
    CalendarSyncResult *out;
    SemaphoreHandle_t   done;
};

void syncTaskEntry(void *arg) {
    SyncJob *job = static_cast<SyncJob *>(arg);
    *job->out = job->sync->runInternal();
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);   // task frees itself; job is owned by the caller
}
} // namespace

CalendarSyncResult CalendarSync::run() {
    CalendarSyncResult r = {};

    SyncJob job;
    job.sync = this;
    job.out  = &r;
    job.done = xSemaphoreCreateBinary();
    if (!job.done) {
        snprintf(r.message, sizeof(r.message), "Sync task alloc failed");
        return r;
    }

    // NOTE: on ESP-IDF, xTaskCreate's stack size is in BYTES (unlike vanilla
    // FreeRTOS words). 24 KB covers parseIcsFeed's 8 KB buffer + mbedTLS.
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
