// ===========================================================================
//  AppManager.cpp  —  Registry, launcher UI, lifecycle and button dispatch
// ===========================================================================
//  See AppManager.h for the design overview. Implements the top-level state
//  machine, the launcher (home screen), GPIO21 hold-band gesture decoding,
//  event routing to the active app, and system tasks (battery, Wi-Fi, sleep).
// ===========================================================================
#include "app/AppManager.h"
#include "core/BatteryMath.h"
#include "core/ButtonClassify.h"

// --- Construction ----------------------------------------------------------
AppManager::AppManager(SystemContext &ctx)
    : _ctx(ctx) {}

// --- Registration ----------------------------------------------------------
void AppManager::addApp(App *app) {
    if (_appCount >= APP_MAX_COUNT) {
        Serial.println("[AppManager] addApp: registry full, ignoring");
        return;
    }
    _apps[_appCount++] = app;
}

// --- Lifecycle -------------------------------------------------------------
void AppManager::begin() {
    _bootMs = _lastActivityMs = millis();
    _state = State::Launcher;
    _launcherSel = 0;
    drawLauncher();
}

void AppManager::loop() {
    handleButtonRaw();

    // An app can ask to return to the launcher at any time (checked each tick).
    if (_homeRequested) {
        returnToLauncher();
    }

    // Let the active app do periodic work (network polling, timers, etc.).
    if (_state == State::AppActive && _activeApp) {
        _activeApp->onLoop(millis());
    }

    systemTasks(millis());
}

// --- App → Manager communication -------------------------------------------
void AppManager::requestHome() {
    _homeRequested = true;
}

// --- Internal: launcher UI -------------------------------------------------
void AppManager::drawLauncher() {
    DisplayManager &d = _ctx.display;
    bool wifi = _ctx.portal && _ctx.portal->isRunning();
    d.setWifiState(wifi);
    d.setBattery(_ctx.batteryPct);

    d.clearBuffer();
    d.drawTextCentered(56, APP_LAUNCHER_TITLE, 2);

    // App list with selection box on the highlighted item.
    int lh = d.lineHeightFor(1) + 16;
    int x  = DISPLAY_WIDTH / 2 - 220;
    int y  = 160;
    for (int i = 0; i < _appCount; i++) {
        String label = String(_apps[i]->name());
        if (i == _launcherSel) {
            int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label, 1);
        y += lh;
    }

    // Status line: Wi-Fi + battery.
    String status = String("Wi-Fi: ") + (wifi ? "ON" : "OFF")
                  + "       Battery: " + String(_ctx.batteryPct) + "%";
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 40, status);

    // Hint line.
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16,
        "Tap = move    Hold = open");
    d.flush(true);
}

// --- Internal: button decode + dispatch ------------------------------------
void AppManager::handleButtonRaw() {
    const uint32_t now     = millis();
    const bool     nowDown = (digitalRead(BTN_BOOT) == LOW);

    if (nowDown && !_btnDown) {
        // ---- Press begins ----
        _btnDown    = true;
        _btnPressMs = now;
        _longFired  = false;

    } else if (nowDown && _btnDown) {
        // ---- Being held: check long-press threshold (seam: ButtonClassify) ----
        if (!_longFired && core::isLongPress(now - _btnPressMs, BTN_LONGPRESS_MS)) {
            _longFired = true;
            _lastActivityMs = now;
            if (_state == State::Launcher) {
                activateApp(_launcherSel);
            } else if (_activeApp) {
                _activeApp->onButton(ButtonEvent::LongHold);
            }
        }

    } else if (!nowDown && _btnDown) {
        // ---- Release: classify via seam ----
        _btnDown = false;
        uint32_t held = now - _btnPressMs;
        if (!_longFired) {
            core::Gesture g = core::classifyRelease(held, BTN_DEBOUNCE_MS,
                                                    BTN_PREVHOLD_MS, BTN_LONGPRESS_MS);
            if (g != core::Gesture::None) {
                _lastActivityMs = now;
                if (_state == State::Launcher) {
                    // Launcher only uses Tap (move selection); MediumHold ignored.
                    if (g == core::Gesture::Tap) {
                        _launcherSel = core::wrapSelection(_launcherSel, 1, _appCount);
                        drawLauncher();
                    }
                } else if (_activeApp) {
                    if (g == core::Gesture::MediumHold)
                        _activeApp->onButton(ButtonEvent::MediumHold);
                    else
                        _activeApp->onButton(ButtonEvent::Tap);
                }
            }
        }
    }
}

// --- Internal: state transitions -------------------------------------------
void AppManager::activateApp(int index) {
    if (index < 0 || index >= _appCount) return;
    if (_activeApp) _activeApp->onExit();

    _activeApp = _apps[index];
    _state     = State::AppActive;
    Serial.printf("[AppManager] launched: %s\n", _activeApp->name());
    _activeApp->onEnter();
}

void AppManager::returnToLauncher() {
    if (_activeApp) {
        Serial.printf("[AppManager] exiting: %s\n", _activeApp->name());
        _activeApp->onExit();
    }
    _activeApp     = nullptr;
    _state         = State::Launcher;
    _homeRequested = false;
    Serial.println("[AppManager] returned to launcher");
    drawLauncher();
}

// --- Internal: system tasks ------------------------------------------------
void AppManager::systemTasks(uint32_t now) {
    // 1. Battery refresh every 15 s (ADC2 unavailable while Wi-Fi is on).
    if (now - _lastBattMs > 15000UL) {
        _lastBattMs = now;
        if (!_ctx.portal || !_ctx.portal->isRunning()) {
            _ctx.batteryPct = _ctx.readBattery();
        }
    }

    // 2. Wi-Fi auto-shutoff after WEB_ACTIVE_MINUTES (unless user toggled it).
    if (!_webUserManaged && _ctx.portal && _ctx.portal->isRunning() &&
        now - _bootMs > (uint32_t)WEB_ACTIVE_MINUTES * 60000UL) {
        _ctx.portal->stop();
        _ctx.display.setWifiState(false);
        Serial.println("[AppManager] Wi-Fi auto-shutoff");
    }

    // 3. Light-sleep after inactivity (only if portal off and app allows it).
    if (now - _lastActivityMs > (uint32_t)IDLE_SLEEP_SECONDS * 1000UL) {
        bool portalOff = !_ctx.portal || !_ctx.portal->isRunning();
        bool canSleep  = (_state == State::Launcher) ||
                         (_activeApp && _activeApp->wantsSleep());
        if (portalOff && canSleep) {
            Serial.println("[AppManager] idle -> light sleep");
            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_BOOT, 0);
            esp_light_sleep_start();
            _lastActivityMs = millis();   // reset on wake
        }
    }
}
