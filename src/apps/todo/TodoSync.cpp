// ===========================================================================
//  TodoSync.cpp  —  Wi-Fi + NTP + HTTPS Tasks-calendar sync session (TODO·R1)
// ===========================================================================
//  See TodoSync.h for the design notes (portal coordination, task stack,
//  secrets convention). Everything here reuses the existing seams:
//    core::parseIcsFeed     — RFC 5545 parsing (shared with the Calendar app;
//                             TODO·R1 added UID capture to it)
//    core::todoExtractTasks — all-day events -> tasks (pure, host-tested)
//    core::todoDonePrune    — drop done-keys for deleted tasks (pure)
//    TodoStore              — /todo.json persistence (tasks + done + sync)
//  The session shape mirrors CalendarSync.cpp / WeatherSync.cpp line for line
//  where it matters (portal guard -> RAII WifiOffGuard -> wifiSessionStaNtp
//  -> HTTPS GET -> persist) so the three files stay diffable. Wi-Fi lifecycle
//  bugs fixed once in the calendar (true-UTC clock, pre-alloc body-size
//  check) are inherited here.
// ===========================================================================
#include "TodoSync.h"
#include "TodoStore.h"
#include "app/AppManager.h"
#include "app/WifiSession.h"
#include "core/IcsParser.h"
#include "core/TodoModel.h"

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

// --- Session tuning (same envelope as CalendarSync) --------------------------
// Wi-Fi-join / NTP-wait tries, the clock-sane floor and the 24 KB task stack
// live in app/WifiSession (WIFI_SESSION_*), shared with CalendarSync +
// WeatherSync. The HTTP timeouts stay here: they are todo-specific.
static const int     SYNC_HTTP_TIMEOUT_MS = 10000;
static const int     SYNC_HTTP_CONNECT_MS = 8000;

// ===========================================================================
//  Session body (runs on the dedicated sync task — NOT the loop stack)
// ===========================================================================
TodoSyncResult TodoSync::runInternal() {
    TodoSyncResult r = {};   // zeroed: ok=false, httpStatus=0, message ""

    // --- Wi-Fi coordination (see header): never fight the portal over the
    // radio. isRunning() covers both the auto-started boot portal and one the
    // user toggled on by hand; either way the async HTTP server is live and a
    // WiFi.mode() switch would break it.
    if (_ctx.portal && _ctx.portal->isRunning()) {
        snprintf(r.message, sizeof(r.message), "Wi-Fi portal active - stop it first");
        Serial.printf("[TodoSync] aborted: WebPortal running (userManaged=%d)\n",
                      _ctx.manager ? (int)_ctx.manager->webUserManaged() : -1);
        return r;
    }

#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    // RAII guard: Wi-Fi is powered down on EVERY exit path below — success,
    // failure, early return — so the radio never stays hot after a fetch.
    WifiOffGuard wifiGuard;   // shared app/WifiSession RAII Wi-Fi-off

    // --- Feed config (single "Tasks" calendar) ------------------------------
    // TODO_ICS_URL is optional: without it the firmware still builds and the
    // app shows its empty state; sync then fails loudly and readably here.
#if !defined(TODO_ICS_URL)
    snprintf(r.message, sizeof(r.message), "No Tasks calendar (set TODO_ICS_URL)");
    Serial.println("[TodoSync] aborted: TODO_ICS_URL not defined in secrets.h");
    return r;
#else
    // --- 1+2. STA-only Wi-Fi + NTP (shared app/WifiSession plumbing) --------
    int64_t nowUtc = 0;
    if (!wifiSessionStaNtp(nowUtc, r.message, sizeof(r.message), "[TodoSync]")) return r;
    Serial.printf("[TodoSync] time synced, nowUtc=%lld\n", (long long)nowUtc);

    // --- 3. HTTPS GET the Tasks ICS ------------------------------------------
    // Heap-backed buffers (vectors): the event array (~10 KB with UID fields),
    // the task array and the ~4.6 KB done-set must NOT live on the task stack;
    // vectors also guarantee no leaks across the many early-return paths.
    std::vector<core::CalendarEvent> evBuf(TODO_MAX_EVENTS);
    std::vector<core::TodoTask>      tasks(TODO_MAX_TASKS);
    std::vector<core::TodoDoneSet>   doneBuf(1);   // device-local done-state
    int nTasks = 0;
    bool fetched = false;
    {
        WiFiClientSecure client;
        // P0-3: TLS CA validation. Apply the CA cert for calendar.google.com;
        // if no cert is available, fall back to setInsecure() with a warning.
        if (!wifiSessionApplyCa(client, "calendar.google.com", "TodoSync")) {
            client.setInsecure();  // fallback with warning
            Serial.println("[TodoSync] WARNING: TLS without CA validation (no cert for host)");
        }
        HTTPClient http;
        http.setConnectTimeout(SYNC_HTTP_CONNECT_MS);
        http.setTimeout(SYNC_HTTP_TIMEOUT_MS);
        if (http.begin(client, TODO_ICS_URL)) {
            int code = http.GET();
            r.httpStatus = code;
            // Inherited from the calendar's R4 fix: when Content-Length is
            // known, reject oversized bodies BEFORE allocating them —
            // getString() would otherwise pull the whole body onto the heap
            // first. Chunked / unknown sizes (-1) are caught post-fetch.
            // TODO_BODY_MAX == the parser's unfold-buffer ceiling, so a larger
            // body could never fully parse anyway.
            int declared = http.getSize();
            if (code == HTTP_CODE_OK && declared > (int)TODO_BODY_MAX) {
                Serial.printf("[TodoSync] body too large (%d bytes)\n", declared);
                snprintf(r.message, sizeof(r.message), "Response too large");
            } else if (code == HTTP_CODE_OK) {
                String body = http.getString();
                if (body.length() > 0 && body.length() <= TODO_BODY_MAX) {
                    // NOTE: parseIcsFeed unfolds into its own 8 KB buffer and
                    // truncates longer feeds; a Tasks calendar is well under
                    // that. UID capture (TODO·R1) gives each event its stable
                    // done-state identity.
                    int nEv = core::parseIcsFeed(body.c_str(), 0, evBuf.data(),
                                                 TODO_MAX_EVENTS, CAL_TZ_OFFSET_SEC);
                    nTasks = core::todoExtractTasks(evBuf.data(), nEv,
                                                    tasks.data(), TODO_MAX_TASKS);
                    Serial.printf("[TodoSync] %d events parsed, %d all-day tasks\n",
                                  nEv, nTasks);
                    fetched = true;
                } else {
                    snprintf(r.message, sizeof(r.message), "Bad body (%u bytes)",
                             (unsigned)body.length());
                    Serial.printf("[TodoSync] bad body (%u bytes)\n",
                                  (unsigned)body.length());
                }
            } else {
                snprintf(r.message, sizeof(r.message), "HTTP error %d", code);
                Serial.printf("[TodoSync] HTTP error %d\n", code);
            }
            http.end();
        } else {
            snprintf(r.message, sizeof(r.message), "HTTPS begin() failed");
            Serial.println("[TodoSync] begin() failed");
        }
    }
    if (!fetched) {
        if (r.message[0] == '\0')
            snprintf(r.message, sizeof(r.message), "Fetch failed");
        return r;
    }

    // --- 4. Merge device-local done-state + prune stale keys ------------------
    // Done-state is NEVER pushed to Google: it is loaded from /todo.json,
    // pruned against the freshly-fetched task list (a task deleted on the
    // phone must not leak its done-key forever) and saved back alongside the
    // tasks. A missing / corrupt done-store simply loads empty (tolerant
    // seam) — the user re-toggles at most a few items.
    TodoStore store(_ctx.storage.fs());
    store.load(nullptr, 0, &doneBuf[0], nullptr);      // done-set + sync only
    int pruned = core::todoDonePrune(doneBuf[0], tasks.data(), nTasks);
    if (pruned > 0)
        Serial.printf("[TodoSync] pruned %d stale done-key(s)\n", pruned);

    // --- 5. Persist ------------------------------------------------------------
    if (!store.save(tasks.data(), nTasks, doneBuf[0], nowUtc)) {
        snprintf(r.message, sizeof(r.message), "Cache write failed");
        return r;
    }

    r.tasksFetched = nTasks;
    r.ok = true;
    snprintf(r.message, sizeof(r.message), "%d tasks (%d done)",
             nTasks, doneBuf[0].count);
    Serial.printf("[TodoSync] done: %s\n", r.message);
    return r;   // wifiGuard: WiFi.mode(WIFI_OFF) here
#endif // TODO_ICS_URL
#else
    // No STA credentials compiled in: fail loudly and readably, never crash.
    snprintf(r.message, sizeof(r.message), "No Wi-Fi secrets (src/secrets.h)");
    Serial.println("[TodoSync] WIFI_STA_SSID/PASS not defined - sync disabled");
    return r;
#endif
}

// ===========================================================================
//  Blocking entry point (loop-stack safe: work happens on a 24 KB task)
// ===========================================================================
//  The semaphore + xTaskCreate trampoline is shared with CalendarSync +
//  WeatherSync (app/WifiSession::wifiSessionRunOnTask); run() just supplies
//  the todo session body. Stack-safety + Wi-Fi-lifecycle behaviour are
//  unchanged from the calendar's live-verified implementation.
TodoSyncResult TodoSync::run() {
    return wifiSessionRunOnTask<TodoSyncResult>("todoSync",
        [this]() { return runInternal(); });
}
