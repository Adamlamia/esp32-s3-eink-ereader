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
// ===========================================================================
#include <Arduino.h>
#include "config.h"
#include "app/App.h"
#include "app/SystemContext.h"

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

    // --- Accessors ---
    int  appCount() const { return _appCount; }
    bool inLauncher() const { return _state == State::Launcher; }

private:
    // --- Top-level state machine ---
    enum class State { Launcher, AppActive };

    // --- Internal helpers ---
    void drawLauncher();            // render the home screen
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

    // --- System task timers ---
    uint32_t       _lastActivityMs = 0;
    uint32_t       _bootMs         = 0;
    uint32_t       _lastBattMs     = 0;
    bool           _webUserManaged = false; // true once user toggles Wi-Fi by hand
};
