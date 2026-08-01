#pragma once
// ===========================================================================
//  WeatherSync  —  self-contained Open-Meteo sync session (WTH·R1)
// ===========================================================================
//  One blocking call brings up STA-only Wi-Fi, fetches the time via NTP,
//  HTTPS-GETs the Open-Meteo /v1/forecast endpoint (free, no API key), parses
//  the JSON via the pure core::parseOpenMeteo seam, stamps fetchedUtc +
//  location label, persists the snapshot via WeatherStore, and ALWAYS powers
//  Wi-Fi back down before returning.
//
//  This class mirrors src/apps/calendar/CalendarSync.h/.cpp deliberately —
//  same portal guard, same RAII WifiOffGuard, same 24 KB trampoline task,
//  same STA+NTP plumbing shape — so the two files stay diffable side by side.
//
//  Wi-Fi coordination with the WebPortal (important):
//    main.cpp can boot the WebPortal in WIFI_AP_STA mode, and the ReaderApp
//    can toggle it by hand. WeatherSync needs WIFI_STA only, and calling
//    WiFi.mode() while the portal's async HTTP server is live would yank the
//    radio out from under it. So run() REFUSES to start while the portal is
//    running (isRunning() covers both the auto-started boot portal and a
//    user-managed one) and returns a readable message instead of fighting
//    over WiFi.mode. Stop the portal first (E-Reader -> Wi-Fi upload: OFF),
//    then refresh.
//
//  Stack design (why run() spawns a task):
//    Arduino's loopTask stack is only 8 KB, but mbedTLS (WiFiClientSecure)
//    needs several KB of headroom plus the JSON parse buffers on top. run()
//    therefore executes runInternal() on a dedicated 24 KB FreeRTOS task and
//    blocks the caller on a semaphore — from the UI's point of view it is
//    still a simple synchronous call. (Same reasoning as CalendarSync.)
//
//  Secrets: WIFI_STA_SSID / WIFI_STA_PASS come from src/secrets.h (git-ignored,
//  #included by config.h if present) and are #if-guarded so the firmware
//  compiles and fails gracefully without them ("No Wi-Fi secrets"). The
//  location (WEATHER_LAT / WEATHER_LON / WEATHER_LABEL / WEATHER_TZ) has
//  Kuala Lumpur defaults compiled in, so NO weather-specific secret is
//  required for a fully functional fetch.
//
//  Clock convention: identical to CalendarSync — time(nullptr) returns TRUE
//  UTC epoch seconds once NTP has fixed the clock (configTime()'s gmtOffset
//  only steers localtime()). fetchedUtc is that value used DIRECTLY, with no
//  offset subtraction. The NTP-validity floor is the shared CAL_CLOCK_MIN_EPOCH
//  from config.h (deliberately NOT duplicated here).
//
//  TODO(WTH-R2): staNtp() + the trampoline below are near-verbatim parallels
//  of CalendarSync's; extract a shared Wi-Fi/NTP/task helper (e.g.
//  src/app/WifiSession.h) once both apps have settled, so the radio lifecycle
//  lives in exactly one place. Deferred to avoid destabilising the merged
//  calendar code in this round.
// ===========================================================================
#include <stdint.h>
#include <stddef.h>
#include "app/SystemContext.h"

// --- Result of one sync session --------------------------------------------
struct WeatherSyncResult {
    bool ok;               // true iff the fetch parsed AND the cache was written
    int  httpStatus;       // HTTP status of the GET (0 = never got a response)
    char message[64];      // short human-readable status/error, always NUL-terminated
};

class WeatherSync {
public:
    explicit WeatherSync(SystemContext &ctx) : _ctx(ctx) {}

    // Blocking sync session (runs on a dedicated high-stack task; see header).
    WeatherSyncResult run();

    // The session body. Public so the trampoline task can call it; not meant
    // to be called directly from the loop stack (stack overflow risk).
    WeatherSyncResult runInternal();

private:
    // STA-join + NTP plumbing (parallel to CalendarSync::staNtp). Returns true
    // and sets nowUtc (TRUE UTC seconds) on success; on failure returns false
    // with a short readable reason in msg. Portal-guarded by the caller. Does
    // NOT own the radio lifecycle: the caller holds the RAII WifiOffGuard so
    // Wi-Fi is always powered down on every exit path.
    bool staNtp(int64_t &nowUtc, char *msg, size_t msgLen);

    SystemContext &_ctx;
};
