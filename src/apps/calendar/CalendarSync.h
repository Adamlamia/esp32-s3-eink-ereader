#pragma once
// ===========================================================================
//  CalendarSync  —  self-contained ICS sync session (Round 2)
// ===========================================================================
//  One blocking call brings up STA-only Wi-Fi, fetches the time via NTP,
//  HTTPS-GETs every configured ICS feed, parses + merges + expands them into
//  a CAL_SYNC_WINDOW_DAYS window anchored at "now" (reusing the Round-1 core
//  seams — no parsing/date logic is duplicated here), persists the result via
//  CalendarStore, and ALWAYS powers Wi-Fi back down before returning.
//
//  Wi-Fi coordination with the WebPortal (important):
//    main.cpp boots the WebPortal in WIFI_AP_STA mode, and the ReaderApp can
//    toggle it by hand. CalendarSync needs WIFI_STA only, and calling
//    WiFi.mode() while the portal's async HTTP server is live would yank the
//    radio out from under it. So run() REFUSES to start while the portal is
//    running (isRunning() covers both the auto-started boot portal and a
//    user-managed one) and returns a readable message instead of fighting
//    over WiFi.mode. Stop the portal first (E-Reader -> Wi-Fi upload: OFF),
//    then sync.
//
//  Stack design (why run() spawns a task):
//    Arduino's loopTask stack is only 8 KB, but core::parseIcsFeed() alone
//    keeps an 8 KB unfold buffer on the stack and mbedTLS (WiFiClientSecure)
//    needs several KB of headroom on top. Running the session on the loop
//    stack would overflow it. run() therefore executes runInternal() on a
//    dedicated 24 KB FreeRTOS task and blocks the caller on a semaphore —
//    from the UI's point of view it is still a simple synchronous call.
//
//  Secrets: WIFI_STA_SSID / WIFI_STA_PASS / CAL_ICS_URL_0..3 come from
//  src/secrets.h (git-ignored, #included by config.h if present). Every use
//  is #if-guarded so the firmware compiles and fails gracefully without them.
//
//  Time-only path (Round 3): syncTimeOnly() brings up STA + NTP and returns
//  once the clock is valid, WITHOUT fetching any ICS feed. The ESP32-S3 has no
//  battery-backed RTC, so after a power cycle the wall clock is unknown until
//  some NTP pass runs; CalendarApp calls this on boot so the scheduling math
//  (core/SyncSchedule.h) has a valid "now" to work with. It reuses exactly the
//  same STA-join + NTP plumbing (wifiSessionStaNtp) and the same 24 KB
//  trampoline task (wifiSessionRunOnTask) as the full run() - both now shared
//  with WeatherSync via app/WifiSession - so Wi-Fi lifecycle and stack-safety
//  behaviour are identical.
//
//  Clock convention (important): time(nullptr) returns TRUE UTC epoch seconds
//  once NTP has fixed the clock — configTime()'s gmtOffset argument only steers
//  localtime(), not time(). nowUtc is therefore time(nullptr) used DIRECTLY,
//  with no offset subtraction, matching the true-UTC convention of the ICS
//  parser and the core date seams. (Round 2 subtracted CAL_TZ_OFFSET_SEC here,
//  a latent bug that pre-dated any live sync; corrected in Round 3 so the
//  stored lastSyncUtc lines up with event times and the scheduler.)
// ===========================================================================
#include <stdint.h>
#include <stddef.h>
#include "app/SystemContext.h"
#include "app/WifiSession.h"      // WTH-R2: shared Wi-Fi/NTP/task lifecycle
#include "core/CalendarEvent.h"

// --- Result of one sync session --------------------------------------------
struct CalendarSyncResult {
    bool ok;               // true iff >= 1 feed succeeded AND the cache was written
    int  eventsFetched;    // concrete events saved (after recurrence expansion)
    int  feedsOk;          // feeds that returned parseable ICS
    int  feedsFailed;      // feeds that failed (connect / HTTP / bad body)
    char message[64];      // short human-readable status/error, always NUL-terminated
};

class CalendarSync {
public:
    explicit CalendarSync(SystemContext &ctx) : _ctx(ctx) {}

    // Blocking sync session (runs on a dedicated high-stack task; see header).
    CalendarSyncResult run();

    // Blocking time-only NTP pass (STA + NTP, no feed fetch). Same task/stack
    // and Wi-Fi lifecycle guarantees as run(); see header for the rationale.
    CalendarSyncResult syncTimeOnly();

    // The session bodies. Public so the trampoline task can call them; not
    // meant to be called directly from the loop stack (stack overflow risk).
    CalendarSyncResult runInternal();
    CalendarSyncResult syncTimeOnlyInternal();

private:
    // WTH-R2: the STA-join + NTP plumbing (wifiSessionStaNtp), the RAII
    // WifiOffGuard and the 24 KB trampoline (wifiSessionRunOnTask) now live in
    // app/WifiSession, shared verbatim with WeatherSync. run() / syncTimeOnly()
    // select the session body and forward to the shared trampoline.

    SystemContext &_ctx;
};
