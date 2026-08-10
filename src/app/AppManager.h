#pragma once
// ===========================================================================
//  AppManager  —  Registry, launcher UI, lifecycle and button dispatch
// ===========================================================================
//  Owns the top-level state machine:
//    [Launcher] --hold on app--> [AppActive]
//    [AppActive] --requestHome()--> [Launcher]
//
//  Responsibilities:
//    - Maintain a static array of registered App pointers.
//    - Draw the launcher (home screen) and handle its navigation.
//    - Decode GPIO21 hold-band gestures into ButtonEvent values.
//    - Route events to the active app or handle launcher selection.
//    - Run system tasks: battery refresh, Wi-Fi auto-shutoff, light-sleep.
//
//  Launcher layout (AGD·R1, split view): LEFT = app list (tap moves, hold
//  opens); RIGHT = today's agenda timeline merged from the calendar cache
//  by the pure seam core::agendaMergeToday (read-only, no interaction).
// ===========================================================================
#include <Arduino.h>
#include "config.h"
#include "app/App.h"
#include "app/SystemContext.h"
#include "core/AgendaMerge.h"   // agenda timeline seam (pure; CalendarEvent + TodoTask)

class AppManager {
public:
    explicit AppManager(SystemContext &ctx);

    // --- Registration (called once at boot from AppRegistry.h) ---
    void addApp(App *app);

    // --- Lifecycle ---
    void begin();       // draw the launcher, enter Launcher state
    void loop();        // call from Arduino loop(): buttons + dispatch + system

    // --- App → Manager communication ---
    void requestHome(); // active app asks to return to the launcher
    void setWebUserManaged(bool managed) { _webUserManaged = managed; }
    // True once the user has toggled Wi-Fi by hand (disables auto-shutoff).
    // Apps that want to use the radio themselves (e.g. CalendarSync) check
    // this + WebPortal::isRunning() so they never fight over WiFi.mode().
    bool webUserManaged() const { return _webUserManaged; }

    // --- Accessors ---
    int  appCount() const { return _appCount; }
    bool inLauncher() const { return _state == State::Launcher; }

private:
    // --- Top-level state machine ---
    enum class State { Launcher, AppActive };

    // --- Internal helpers ---
    void drawLauncher();            // render the home screen
    void refreshAgenda();           // reload /calendar.json + re-merge timeline
    void drawAgendaPanel();         // render the right-hand "Today" panel
    void handleButtonRaw();         // read GPIO, classify, dispatch
    void activateApp(int index);    // transition Launcher → AppActive
    void returnToLauncher();        // transition AppActive → Launcher
    void systemTasks(uint32_t now); // battery, Wi-Fi shutoff, sleep

    // --- State ---
    SystemContext &_ctx;
    State          _state = State::Launcher;
    App           *_apps[APP_MAX_COUNT] = {};
    int            _appCount    = 0;
    int            _launcherSel = 0;        // highlighted row in the launcher
    App           *_activeApp   = nullptr;  // current app (null in launcher)
    bool           _homeRequested = false;  // flag checked each loop iteration

    // --- Button decode state ---
    bool           _btnDown      = false;
    uint32_t       _btnPressMs   = 0;
    bool           _longFired    = false;

    // --- Multi-tap burst state (batch 3) ---
    bool     _burstActive  = false;   // true while accumulating taps in the burst window
    uint32_t _burstStartMs = 0;       // millis() of the first tap in the burst
    int      _burstDelta   = 0;       // accumulated selection delta

    // --- System task timers ---
    uint32_t       _lastActivityMs = 0;
    uint32_t       _bootMs         = 0;
    uint32_t       _lastBattMs     = 0;
    bool           _webUserManaged = false; // true once user toggles Wi-Fi by hand
    uint32_t       _lastLauncherTickMs = 0; // rate-limiter for launcher background tick

    // --- Agenda (split-view launcher, AGD·R1) ---
    // Refreshed on every drawLauncher() (boot + return-to-launcher + tap):
    // the calendar cache is re-read from the active FS (no network) and
    // merged into today's timeline by the pure seam core::agendaMergeToday.
    // The Todo slot is clean but unwired: the merge is passed nullptr/0
    // until the backend decision (TODO(TODO-BACKEND) in AppRegistry.h).
    core::CalendarEvent _agendaEvents[CAL_MAX_EVENTS];   // loaded cache
    int                 _agendaEventCount  = 0;
    int64_t             _agendaLastSyncUtc = 0;
    int64_t             _agendaNowUtc      = 0;          // clock anchor used for the merge
    core::AgendaItem    _agendaItems[AGENDA_MAX_ITEMS];  // merged timeline
    int                 _agendaItemCount   = 0;
    int                 _agendaNextIdx     = -1;         // "next up" highlight (-1 = none)
};
