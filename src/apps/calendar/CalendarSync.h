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
// ===========================================================================
#include <stdint.h>
#include "app/SystemContext.h"
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

    // The session body. Public so the trampoline task can call it; not meant
    // to be called directly from the loop stack (stack overflow risk).
    CalendarSyncResult runInternal();

private:
    SystemContext &_ctx;
};
