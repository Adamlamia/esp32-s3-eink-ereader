// ===========================================================================
//  AppManager.cpp  —  Registry, launcher UI, lifecycle and button dispatch
// ===========================================================================
//  See AppManager.h for the design overview. All method bodies are stubs in
//  Round 1 (scaffolding); full implementation arrives in Round 2.
// ===========================================================================
#include "app/AppManager.h"

// --- Construction ----------------------------------------------------------
AppManager::AppManager(SystemContext &ctx)
    : _ctx(ctx) {}

// --- Registration ----------------------------------------------------------
void AppManager::addApp(App *app) {
    // TODO(R2): implement — store in _apps[], guard overflow against APP_MAX_COUNT
}

// --- Lifecycle -------------------------------------------------------------
void AppManager::begin() {
    // TODO(R2): implement — record _bootMs, _lastActivityMs, draw the launcher
    _bootMs = _lastActivityMs = millis();
    _state = State::Launcher;
    drawLauncher();
}

void AppManager::loop() {
    // TODO(R2): implement — full loop body:
    //   1. handleButtonRaw()   — read GPIO21, classify gesture, dispatch
    //   2. Check _homeRequested flag → returnToLauncher()
    //   3. If AppActive: call _activeApp->onLoop(millis())
    //   4. systemTasks(millis())
}

// --- App → Manager communication -------------------------------------------
void AppManager::requestHome() {
    _homeRequested = true;
}

// --- Internal: launcher UI -------------------------------------------------
void AppManager::drawLauncher() {
    // TODO(R2): implement — render the home screen:
    //   - Title APP_LAUNCHER_TITLE centred at top
    //   - App list with selection box on _launcherSel
    //   - Status line: Wi-Fi state + battery %
    //   - Hint line: "Tap = move    Hold = open"
    //   - display.flush(true) for full refresh
}

// --- Internal: button decode + dispatch ------------------------------------
void AppManager::handleButtonRaw() {
    // TODO(R2): implement — GPIO21 hold-band gesture decode:
    //
    //   const uint32_t now = millis();
    //   const bool nowDown = (digitalRead(BTN_BOOT) == LOW);
    //
    //   if (nowDown && !_btnDown) {
    //       // Press begins
    //       _btnDown = true;
    //       _btnPressMs = now;
    //       _longFired = false;
    //   } else if (nowDown && _btnDown) {
    //       // Being held — check long-press threshold
    //       if (!_longFired && now - _btnPressMs >= BTN_LONGPRESS_MS) {
    //           _longFired = true;
    //           _lastActivityMs = now;
    //           // Dispatch ButtonEvent::LongHold
    //       }
    //   } else if (!nowDown && _btnDown) {
    //       // Release
    //       _btnDown = false;
    //       uint32_t held = now - _btnPressMs;
    //       if (!_longFired && held >= BTN_DEBOUNCE_MS) {
    //           _lastActivityMs = now;
    //           if (held >= BTN_PREVHOLD_MS)
    //               // Dispatch ButtonEvent::MediumHold
    //           else
    //               // Dispatch ButtonEvent::Tap
    //       }
    //   }
    //
    // Dispatch logic:
    //   - In Launcher state:
    //       Tap      → move _launcherSel (wrap around), redraw
    //       LongHold → activateApp(_launcherSel)
    //   - In AppActive state:
    //       Route ButtonEvent to _activeApp->onButton(ev)
}

// --- Internal: state transitions -------------------------------------------
void AppManager::activateApp(int index) {
    // TODO(R2): implement —
    //   Guard: index in [0, _appCount)
    //   If _activeApp: call _activeApp->onExit()
    //   Set _activeApp = _apps[index]
    //   Set _state = State::AppActive
    //   Call _activeApp->onEnter()
    //   Serial.println("[AppManager] launched: <name>")
}

void AppManager::returnToLauncher() {
    // TODO(R2): implement —
    //   If _activeApp: call _activeApp->onExit()
    //   Set _activeApp = nullptr
    //   Set _state = State::Launcher
    //   Clear _homeRequested
    //   drawLauncher()
    //   Serial.println("[AppManager] returned to launcher")
}

// --- Internal: system tasks ------------------------------------------------
void AppManager::systemTasks(uint32_t now) {
    // TODO(R2): implement —
    //   1. Battery refresh every 15s (skip if Wi-Fi portal running):
    //      if (now - _lastBattMs > 15000UL) { ... _ctx.batteryPct = _ctx.readBattery(); }
    //
    //   2. Wi-Fi auto-shutoff after WEB_ACTIVE_MINUTES (unless _webUserManaged):
    //      if (!_webUserManaged && portal running && now - _bootMs > threshold) { stop portal }
    //
    //   3. Light-sleep after IDLE_SLEEP_SECONDS if:
    //      - portal is off
    //      - active app's wantsSleep() returns true (or we're in launcher)
    //      esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_BOOT, 0);
    //      esp_light_sleep_start();
}
