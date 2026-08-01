// ===========================================================================
//  WeatherSync.cpp  —  Wi-Fi + NTP + HTTPS Open-Meteo sync session (WTH·R1)
// ===========================================================================
//  See WeatherSync.h for the design notes (portal coordination, task stack,
//  secrets convention). Everything here reuses the WTH·R1 seams:
//    core::buildOpenMeteoUrl  — request URL (pure, host-tested)
//    core::parseOpenMeteo     — tolerant response parser (pure, host-tested)
//    WeatherStore             — /weather.json persistence
//  The session shape mirrors CalendarSync.cpp line for line where it matters
//  (portal guard -> RAII WifiOffGuard -> staNtp -> HTTPS GET -> persist) so
//  the two files stay diffable. Wi-Fi lifecycle bugs fixed once in the
//  calendar (true-UTC clock, pre-alloc body-size check) are inherited here.
// ===========================================================================
#include "WeatherSync.h"
#include "WeatherStore.h"
#include "app/AppManager.h"
#include "core/OpenMeteo.h"
#include "core/CalendarEvent.h"   // CAL_CLOCK_MIN_EPOCH lives in config.h (shared floor)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <string.h>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// --- Session tuning (same envelope as CalendarSync) --------------------------
static const int     SYNC_WIFI_TRIES   = 30;    // x 500 ms  -> <= 15 s Wi-Fi join
static const int     SYNC_NTP_TRIES    = 30;    // x 500 ms  -> <= 15 s NTP wait
static const int64_t SYNC_NTP_MIN_EPOCH = CAL_CLOCK_MIN_EPOCH;  // shared "clock is sane" floor (config.h)
static const int     SYNC_HTTP_TIMEOUT_MS = 10000;
static const int     SYNC_HTTP_CONNECT_MS = 8000;
static const size_t  SYNC_TASK_STACK_BYTES = 24 * 1024;  // mbedTLS + JSON, see header

// ===========================================================================
//  STA + NTP plumbing (parallel to CalendarSync::staNtp — see TODO(WTH-R2))
// ===========================================================================
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
// Bring up STA-only Wi-Fi and obtain a valid NTP time. Returns true and sets
// nowUtc (TRUE UTC epoch seconds) on success; on failure returns false with a
// short readable reason in msg. The CALLER owns the radio lifecycle (holds the
// RAII WifiOffGuard), so this never leaves Wi-Fi on by itself.
//
// Clock convention: configTime()'s gmtOffset only steers localtime(); once NTP
// has fixed the clock, time(nullptr) returns TRUE UTC epoch seconds directly.
// We therefore use it with NO offset subtraction — identical to CalendarSync.
bool WeatherSync::staNtp(int64_t &nowUtc, char *msg, size_t msgLen) {
    // --- STA-only Wi-Fi ---
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
    Serial.println("[WthSync] joining Wi-Fi (STA)...");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < SYNC_WIFI_TRIES) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        if (msg) snprintf(msg, msgLen, "Wi-Fi connect failed");
        Serial.printf("[WthSync] Wi-Fi connect failed (status %d)\n", WiFi.status());
        return false;
    }
    Serial.printf("[WthSync] Wi-Fi up, IP %s\n", WiFi.localIP().toString().c_str());

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
        Serial.println("[WthSync] NTP gave no valid time");
        return false;
    }
    nowUtc = (int64_t)nowT;   // time() is already TRUE UTC (no offset subtraction)
    return true;
}
#endif // WIFI_STA_SSID && WIFI_STA_PASS

// ===========================================================================
//  Session body (runs on the dedicated sync task — NOT the loop stack)
// ===========================================================================
WeatherSyncResult WeatherSync::runInternal() {
    WeatherSyncResult r = {};   // zeroed: ok=false, httpStatus=0, message ""

    // --- Wi-Fi coordination (see header): never fight the portal over the
    // radio. isRunning() covers both the auto-started boot portal and one the
    // user toggled on by hand; either way the async HTTP server is live and a
    // WiFi.mode() switch would break it.
    if (_ctx.portal && _ctx.portal->isRunning()) {
        snprintf(r.message, sizeof(r.message), "Wi-Fi portal active - stop it first");
        Serial.printf("[WthSync] aborted: WebPortal running (userManaged=%d)\n",
                      _ctx.manager ? (int)_ctx.manager->webUserManaged() : -1);
        return r;
    }

#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    // RAII guard: Wi-Fi is powered down on EVERY exit path below — success,
    // failure, early return — so the radio never stays hot after a fetch.
    struct WifiOffGuard { ~WifiOffGuard() { WiFi.mode(WIFI_OFF); } } wifiGuard;

    // --- 1+2. STA-only Wi-Fi + NTP (parallel plumbing) -------------------------
    int64_t nowUtc = 0;
    if (!staNtp(nowUtc, r.message, sizeof(r.message))) return r;
    Serial.printf("[WthSync] time synced, nowUtc=%lld\n", (long long)nowUtc);

    // --- 3. Build the request URL (pure seam; KL defaults from config.h) -------
    char url[WEATHER_URL_MAX];
    core::buildOpenMeteoUrl(url, sizeof(url), WEATHER_LAT, WEATHER_LON, WEATHER_TZ);
    if (url[0] == '\0') {
        snprintf(r.message, sizeof(r.message), "URL build failed (buffer)");
        Serial.println("[WthSync] aborted: buildOpenMeteoUrl overflow");
        return r;
    }

    // --- 4. HTTPS GET -----------------------------------------------------------
    core::WeatherSnapshot snap;   // ~100 bytes; fine on the 24 KB task stack
    bool fetched = false;
    {
        WiFiClientSecure client;
        // SECURITY NOTE: setInsecure() skips certificate validation, matching
        // CalendarSync. Open-Meteo needs no credential (free, no API key); the
        // payload is public forecast data, so the risk is a tampered forecast,
        // not a leaked secret. (Proper CA validation: TODO(WTH-R2).)
        client.setInsecure();
        HTTPClient http;
        http.setConnectTimeout(SYNC_HTTP_CONNECT_MS);
        http.setTimeout(SYNC_HTTP_TIMEOUT_MS);
        if (http.begin(client, url)) {
            int code = http.GET();
            r.httpStatus = code;
            // Inherited from the calendar's R4 fix: when Content-Length is
            // known, reject oversized bodies BEFORE allocating them —
            // getString() would otherwise pull the whole body onto the heap
            // first. Chunked / unknown sizes (-1) are caught post-fetch.
            int declared = http.getSize();
            if (code == HTTP_CODE_OK && declared > (int)WEATHER_BODY_MAX) {
                Serial.printf("[WthSync] body too large (%d bytes)\n", declared);
                snprintf(r.message, sizeof(r.message), "Response too large");
            } else if (code == HTTP_CODE_OK) {
                String body = http.getString();
                if (body.length() > 0 && body.length() <= WEATHER_BODY_MAX) {
                    if (core::parseOpenMeteo(std::string(body.c_str()), snap)) {
                        fetched = true;
                    } else {
                        snprintf(r.message, sizeof(r.message), "Bad JSON from Open-Meteo");
                        Serial.println("[WthSync] parseOpenMeteo rejected the body");
                    }
                } else {
                    snprintf(r.message, sizeof(r.message), "Bad body (%u bytes)",
                             (unsigned)body.length());
                    Serial.printf("[WthSync] bad body (%u bytes)\n",
                                  (unsigned)body.length());
                }
            } else {
                snprintf(r.message, sizeof(r.message), "HTTP error %d", code);
                Serial.printf("[WthSync] HTTP error %d\n", code);
            }
            http.end();
        } else {
            snprintf(r.message, sizeof(r.message), "HTTPS begin() failed");
            Serial.println("[WthSync] begin() failed");
        }
    }
    if (!fetched) {
        if (r.message[0] == '\0')
            snprintf(r.message, sizeof(r.message), "Fetch failed");
        return r;
    }

    // --- 5. Stamp + persist ------------------------------------------------------
    // The parser leaves these sync-owned fields blank (it is pure and knows
    // neither "now" nor the configured location); stamp them here.
    snap.fetchedUtc = nowUtc;
    strncpy(snap.label, WEATHER_LABEL, WEATHER_LABEL_MAX - 1);
    snap.label[WEATHER_LABEL_MAX - 1] = '\0';

    WeatherStore store(_ctx.storage.fs());
    if (!store.save(snap)) {
        snprintf(r.message, sizeof(r.message), "Cache write failed");
        return r;
    }

    r.ok = true;
    snprintf(r.message, sizeof(r.message), "%.1fC, %d day(s) cached",
             (double)snap.cur.tempC, snap.dayCount);
    Serial.printf("[WthSync] done: %s\n", r.message);
    return r;   // wifiGuard: WiFi.mode(WIFI_OFF) here
#else
    // No STA credentials compiled in: fail loudly and readably, never crash.
    snprintf(r.message, sizeof(r.message), "No Wi-Fi secrets (src/secrets.h)");
    Serial.println("[WthSync] WIFI_STA_SSID/PASS not defined - sync disabled");
    return r;
#endif
}

// ===========================================================================
//  Blocking entry point (loop-stack safe: work happens on a 24 KB task)
// ===========================================================================
namespace {
struct SyncJob {
    WeatherSync       *sync;
    WeatherSyncResult *out;
    SemaphoreHandle_t  done;
};

void syncTaskEntry(void *arg) {
    SyncJob *job = static_cast<SyncJob *>(arg);
    *job->out = job->sync->runInternal();
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);   // task frees itself; job is owned by the caller
}
} // namespace

// Spawn the session body on the dedicated high-stack task and block the
// caller until it completes. Mirrors CalendarSync::runOnTask.
WeatherSyncResult WeatherSync::run() {
    WeatherSyncResult r = {};

    SyncJob job;
    job.sync = this;
    job.out  = &r;
    job.done = xSemaphoreCreateBinary();
    if (!job.done) {
        snprintf(r.message, sizeof(r.message), "Sync task alloc failed");
        return r;
    }

    // NOTE: on ESP-IDF, xTaskCreate's stack size is in BYTES (unlike vanilla
    // FreeRTOS words). 24 KB covers mbedTLS + the JSON body with headroom,
    // matching the calendar's proven sizing.
    BaseType_t started = xTaskCreate(syncTaskEntry, "wthSync",
                                     SYNC_TASK_STACK_BYTES, &job, 1, nullptr);
    if (started != pdPASS) {
        vSemaphoreDelete(job.done);
        snprintf(r.message, sizeof(r.message), "Sync task create failed");
        return r;
    }

    // The wait is bounded in practice: the session has internal timeouts
    // (Wi-Fi <= 15 s, NTP <= 15 s, HTTP <= ~18 s), so it cannot hang.
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return r;
}
