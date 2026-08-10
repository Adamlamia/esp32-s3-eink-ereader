// ===========================================================================
//  WeatherSync.cpp  —  Wi-Fi + NTP + HTTPS Open-Meteo sync session (WTH·R1)
// ===========================================================================
//  See WeatherSync.h for the design notes (portal coordination, task stack,
//  secrets convention). Everything here reuses the WTH·R1 seams:
//    core::buildOpenMeteoUrl  — request URL (pure, host-tested)
//    core::parseOpenMeteo     — tolerant response parser (pure, host-tested)
//    WeatherStore             — /weather.json persistence
//  The session shape mirrors CalendarSync.cpp line for line where it matters
//  (portal guard -> RAII WifiOffGuard -> wifiSessionStaNtp -> HTTPS GET ->
//  persist) so
//  the two files stay diffable. Wi-Fi lifecycle bugs fixed once in the
//  calendar (true-UTC clock, pre-alloc body-size check) are inherited here.
// ===========================================================================
#include "WeatherSync.h"
#include "WeatherStore.h"
#include "app/AppManager.h"
#include "app/WifiSession.h"
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
// Wi-Fi-join / NTP-wait tries, the clock-sane floor and the 24 KB task stack
// now live in app/WifiSession (WIFI_SESSION_*), shared with CalendarSync (WTH-R2).
// The HTTP timeouts stay here: they are weather-specific.
static const int     SYNC_HTTP_TIMEOUT_MS = 10000;
static const int     SYNC_HTTP_CONNECT_MS = 8000;

// ===========================================================================
//  STA + NTP plumbing now lives in app/WifiSession (wifiSessionStaNtp),
//  shared verbatim with CalendarSync; see WifiSession.h for the clock
//  convention (WTH-R2 extraction).
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
    WifiOffGuard wifiGuard;   // shared app/WifiSession RAII Wi-Fi-off (WTH-R2)

    // --- 1+2. STA-only Wi-Fi + NTP (shared app/WifiSession plumbing) --------
    int64_t nowUtc = 0;
    if (!wifiSessionStaNtp(nowUtc, r.message, sizeof(r.message), "[WthSync]")) return r;
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
        // P0-3: TLS CA validation. Apply the CA cert for api.open-meteo.com;
        // if no cert is available, fall back to setInsecure() with a warning.
        if (!wifiSessionApplyCa(client, "api.open-meteo.com", "WthSync")) {
            client.setInsecure();  // fallback with warning
            Serial.println("[WthSync] WARNING: TLS without CA validation (no cert for host)");
        }
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
//  WTH-R2: the semaphore + xTaskCreate trampoline is shared with CalendarSync
//  (app/WifiSession::wifiSessionRunOnTask); run() just supplies the weather
//  session body. Stack-safety + Wi-Fi-lifecycle behaviour are unchanged.
WeatherSyncResult WeatherSync::run() {
    return wifiSessionRunOnTask<WeatherSyncResult>("wthSync",
        [this]() { return runInternal(); });
}
