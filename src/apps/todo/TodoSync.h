#pragma once
// ===========================================================================
//  TodoSync  —  self-contained Tasks-calendar ICS sync session (TODO·R1)
// ===========================================================================
//  One blocking call brings up STA-only Wi-Fi, fetches the time via NTP,
//  HTTPS-GETs the dedicated "Tasks" Google Calendar ICS feed (TODO_ICS_URL
//  from secrets.h), parses it via the EXISTING core::parseIcsFeed seam,
//  extracts the tasks (all-day events only — core::todoExtractTasks), merges
//  + prunes the device-local done-set (core::todoDonePrune), persists the
//  whole cache via TodoStore, and ALWAYS powers Wi-Fi back down before
//  returning. ZERO new auth: the secret ICS URL itself (with its embedded
//  private API key) is the credential, exactly like CAL_ICS_URL_n.
//
//  This class mirrors src/apps/weather/WeatherSync.h/.cpp and
//  src/apps/calendar/CalendarSync.h/.cpp deliberately — same portal guard,
//  same RAII WifiOffGuard, same 24 KB trampoline task (app/WifiSession),
//  same STA+NTP plumbing shape — so the three files stay diffable side by
//  side. Single feed (not a feed table): the Todo app reads exactly one
//  "Tasks" calendar.
//
//  Wi-Fi coordination with the WebPortal (important):
//    main.cpp can boot the WebPortal in WIFI_AP_STA mode, and the ReaderApp
//    can toggle it by hand. TodoSync needs WIFI_STA only, and calling
//    WiFi.mode() while the portal's async HTTP server is live would yank the
//    radio out from under it. So run() REFUSES to start while the portal is
//    running (isRunning() covers both the auto-started boot portal and a
//    user-managed one) and returns a readable message instead of fighting
//    over WiFi.mode. Stop the portal first (E-Reader -> Wi-Fi upload: OFF),
//    then sync.
//
//  Stack design (why run() spawns a task):
//    Arduino's loopTask stack is only 8 KB, but core::parseIcsFeed() keeps an
//    8 KB unfold buffer on the stack and mbedTLS (WiFiClientSecure) needs
//    several KB of headroom on top. run() therefore executes runInternal() on
//    the dedicated 24 KB FreeRTOS task (app/WifiSession) and blocks the
//    caller on a semaphore — from the UI's point of view it is still a simple
//    synchronous call. The event/task/done buffers are heap-backed (vectors)
//    so they never touch the task stack, exactly like CalendarSync.
//
//  Secrets: WIFI_STA_SSID / WIFI_STA_PASS / TODO_ICS_URL come from
//  src/secrets.h (git-ignored, #included by config.h if present). Every use
//  is #if-guarded so the firmware compiles and fails gracefully without them:
//  no TODO_ICS_URL -> "No Tasks calendar (set TODO_ICS_URL)"; no Wi-Fi
//  secrets -> "No Wi-Fi secrets (src/secrets.h)".
//
//  Clock convention: identical to CalendarSync / WeatherSync — time(nullptr)
//  returns TRUE UTC epoch seconds once NTP has fixed the clock; lastSyncUtc
//  is that value used DIRECTLY, with no offset subtraction. The NTP-validity
//  floor is the shared CAL_CLOCK_MIN_EPOCH from config.h.
// ===========================================================================
#include <stdint.h>
#include <stddef.h>
#include "app/SystemContext.h"

// --- Result of one sync session --------------------------------------------
struct TodoSyncResult {
    bool ok;               // true iff the fetch parsed AND the cache was written
    int  httpStatus;       // HTTP status of the GET (0 = never got a response)
    int  tasksFetched;     // tasks saved (all-day events extracted from the feed)
    char message[64];      // short human-readable status/error, always NUL-terminated
};

class TodoSync {
public:
    explicit TodoSync(SystemContext &ctx) : _ctx(ctx) {}

    // Blocking sync session (runs on a dedicated high-stack task; see header).
    TodoSyncResult run();

    // The session body. Public so the trampoline task can call it; not meant
    // to be called directly from the loop stack (stack overflow risk).
    TodoSyncResult runInternal();

private:
    // The STA-join + NTP plumbing (wifiSessionStaNtp), the RAII WifiOffGuard
    // and the 24 KB trampoline (wifiSessionRunOnTask) live in app/WifiSession,
    // shared verbatim with CalendarSync + WeatherSync. run() forwards to the
    // shared trampoline; runInternal() is the todo session body.

    SystemContext &_ctx;
};
