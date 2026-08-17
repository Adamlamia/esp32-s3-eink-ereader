// ===========================================================================
//  AppManager.cpp  —  Registry, launcher UI, lifecycle and button dispatch
// ===========================================================================
//  See AppManager.h for the design overview. Implements the top-level state
//  machine, the launcher (home screen), GPIO21 hold-band gesture decoding,
//  event routing to the active app, and system tasks (battery, Wi-Fi, sleep).
// ===========================================================================
#include "app/AppManager.h"
#include "core/ButtonClassify.h"
#include "core/CalendarDate.h"              // todayStartUtc / civilFromUtc (agenda panel)
#include "core/UiStyle.h"                   // ui:: shared baseline anchors (STD·R1)
#include "apps/calendar/CalendarStore.h"    // /calendar.json cache load (no network)
#include <time.h>                           // time(nullptr) clock source
#include <esp_sleep.h>   // ext0 + timer wakeup sources for light sleep

// --- Small formatting helpers for the agenda panel (fixed UTC+8) -----------
static String pad2(unsigned v) {
    return v < 10 ? String("0") + String(v) : String(v);
}

// "HH:MM" local wall-clock for a UTC epoch instant.
static String agendaHM(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return pad2(hh) + ":" + pad2(mm);
}

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
    _burstActive = false;
    _burstDelta = 0;
    drawLauncher();
}

void AppManager::loop() {
    handleButtonRaw();

    // Close the burst window if it has expired (batch 3).
    if (_burstActive && !core::isInBurstWindow(millis(), _burstStartMs, BTN_BURST_WINDOW_MS)) {
        // Dispatch accumulated taps
        if (_state == State::Launcher) {
            _launcherSel = core::wrapSelection(_launcherSel, _burstDelta, _appCount);
            drawLauncher();
        } else if (_activeApp) {
            _activeApp->setTapCount(_burstDelta);
            _activeApp->onButton(ButtonEvent::Tap);
            _activeApp->setTapCount(1);  // reset for next time
        }
        _burstActive = false;
        _burstDelta = 0;
    }

    // An app can ask to return to the launcher at any time (checked each tick).
    if (_homeRequested) {
        returnToLauncher();
    }

    // Let the active app do periodic work (network polling, timers, etc.).
    if (_state == State::AppActive && _activeApp) {
        _activeApp->onLoop(millis());
    }
    // NOTE: removed launcher background tick (P0-2) — it was causing CalendarApp
    // to run blocking NTP/sync operations every second from the launcher, which
    // triggered unwanted sync attempts on every button press. The 6 AM daily sync
    // will only trigger when the user opens the Calendar app, or via timer wake
    // from light sleep (handled by sleepWakeupSec()).

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

    // Re-read the calendar cache + re-merge today's timeline on every draw
    // (boot, return-to-launcher, selection tap) so the right panel is fresh.
    refreshAgenda();

    d.clearBuffer();

    // ===== Left panel: app list (x: MARGIN_X .. ~356) =======================
    d.drawText(MARGIN_X, ui::TITLE_Y, "Apps");

    // App list with selection box on the highlighted item, vertically
    // centred inside the panel body (y 100..480).
    int lh    = d.lineHeightFor(1) + 16;
    int block = _appCount * lh;
    int x     = MARGIN_X + 24;
    int y     = 100 + (380 - block) / 2 + 36;
    for (int i = 0; i < _appCount; i++) {
        String label = String(_apps[i]->name());
        if (i == _launcherSel) {
            int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label);
        y += lh;
    }

    // ===== Vertical divider ==================================================
    d.fillRect(370, 60, 2, 440);           // thin 2 px rule, y 60..500

    // ===== Right panel: today's agenda (read-only) ===========================
    drawAgendaPanel();

    // ===== Status + hint lines (bottom left) =================================
    String status = String("Wi-Fi: ") + (wifi ? "ON" : "OFF")
                  + "       Battery: " + String(_ctx.batteryPct) + "%";
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 40, status);
    d.drawBookText(MARGIN_X, ui::MENU_FOOTER_Y,
        "Tap = move    Hold = open");
    d.flush(true);
}

// --- Internal: agenda data (split-view launcher, AGD·R1) -------------------
void AppManager::refreshAgenda() {
    // 1. Load the calendar cache from the active FS — SD/LittleFS read only,
    //    NO network (the Calendar app owns syncing). CalendarStore's load()
    //    contract: a missing / corrupt / oversized file yields 0 events.
    CalendarStore store(_ctx.storage.fs());
    _agendaEventCount = store.load(_agendaEvents, CAL_MAX_EVENTS, &_agendaLastSyncUtc);
    if (_agendaEventCount < 0) _agendaEventCount = 0;      // defensive; load() never does

    // 2. "Now": true UTC once NTP has fixed the clock; otherwise anchor at
    //    the last-sync instant (the CalendarApp::uiNowUtc convention — the
    //    cache was materialised at exactly that "now", so the day boundary
    //    stays coherent). Never synced + no clock → 0 → the timeline renders
    //    empty ("No events today") rather than at a garbage date.
    int64_t now = (int64_t)time(nullptr);
    if (now < CAL_CLOCK_MIN_EPOCH) now = _agendaLastSyncUtc;
    if (now < CAL_CLOCK_MIN_EPOCH) now = 0;
    _agendaNowUtc = now;

    int64_t d0 = 0, d1 = 0;
    if (now > 0) {
        d0 = core::todayStartUtc(now, CAL_TZ_OFFSET_SEC);  // local midnight today (UTC)
        d1 = d0 + 86400;                                   // local midnight tomorrow
    }

    // 3. Merge: calendar only for now. The Todo slot stays clean (nullptr/0)
    //    until the backend decision (TODO(TODO-BACKEND)); the seam already
    //    accepts tasks (native-tested), so wiring /todo.json in later is a
    //    call-site change, never a rewrite.
    _agendaItemCount = core::agendaMergeToday(
        _agendaEvents, _agendaEventCount,
        nullptr, 0,                        // Todo slot — not wired yet (AGD·R1)
        now, d0, d1,
        _agendaItems, AGENDA_MAX_ITEMS, &_agendaNextIdx);

    // Observability (AGD·R2): one line per refresh so an "empty timeline" is
    // diagnosable from serial alone. CalendarStore::load already logs the raw
    // event count; this logs what the merge made of it — distinguishing "cache
    // empty" (events=0), "clock not fixed" (now=0 → window [0,0) excludes
    // everything) and "busy day merged to N items, next highlight at K".
    Serial.printf("[Agenda] now=%lld window=[%lld,%lld) events=%d items=%d next=%d\n",
                  (long long)_agendaNowUtc, (long long)d0, (long long)d1,
                  _agendaEventCount, _agendaItemCount, _agendaNextIdx);
}

// --- Internal: agenda panel rendering (right half of the launcher) ---------
void AppManager::drawAgendaPanel() {
    DisplayManager &d = _ctx.display;
    const int rx = 396;                            // panel left edge (past divider)
    const int rW = DISPLAY_WIDTH - MARGIN_X - rx;  // panel width (538 px)

    // Header: "Today" + the date when the clock is valid.
    String header = "Today";
    if (_agendaNowUtc > 0) {
        int64_t y; unsigned m, dd, hh, mm, ss;
        core::civilFromUtc(_agendaNowUtc, CAL_TZ_OFFSET_SEC, y, m, dd, hh, mm, ss);
        header += "  " + String((long)y) + "-" + pad2(m) + "-" + pad2(dd);
    }
    d.drawText(rx, ui::TITLE_Y, header);

    const int lh   = d.readerLineHeight() + ui::ROW_GAP;
    const int yTop = 112;                          // first text baseline
    const int yMax = 464;                          // room below for the Todo slot line
    int y = yTop;

    if (_agendaItemCount == 0) {
        String msg = "No events today";
        int w = d.textWidth(msg, true);
        int cy = (yTop + yMax) / 2;
        d.drawBookText(rx + (rW - w) / 2, cy, msg);        // centred in the panel
        if (_agendaLastSyncUtc <= 0)
            d.drawBookText(rx, cy + 30, "Open the Calendar app to sync.");
    } else {
        bool allDayLabel = false;
        for (int i = 0; i < _agendaItemCount; i++) {
            const core::AgendaItem &it = _agendaItems[i];
            // The panel is read-only (no scroll gesture): draw what fits and
            // count the overflow rather than running off the panel.
            int rowsNeeded = (it.allDay && !allDayLabel) ? 2 : 1;   // +section label
            if (y + rowsNeeded * lh > yMax) {
                d.drawBookText(rx, y, String("+ ") + String(_agendaItemCount - i) + " more");
                break;
            }
            if (i == _agendaNextIdx)               // "next up" highlight box
                d.drawSelectionBox(rx - 8, y - d.readerAscender() - 3, rW + 12, lh - 2);
            if (it.allDay) {
                if (!allDayLabel) {
                    d.drawBookText(rx, y, "All-day");
                    y += lh;
                    allDayLabel = true;
                }
                d.drawBookText(rx + 16, y, String("- ") + it.title);
            } else {
                d.drawBookText(rx, y, agendaHM(it.timeUtc) + "  " + String(it.title));
            }
            y += lh;
        }
    }

    // Todo slot placeholder (AGD·R1): the merge seam accepts tasks but the
    // firmware passes nullptr/0 until the backend decision (TODO(TODO-BACKEND)).
    // Small book font reads as "de-emphasised" on the 1-bit panel.
    d.drawBookText(rx, DISPLAY_HEIGHT - 40, "(Tasks: pending backend)");
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
            // Dispatch any pending burst before the LongHold (batch 3).
            if (_burstActive) {
                if (_state == State::Launcher) {
                    _launcherSel = core::wrapSelection(_launcherSel, _burstDelta, _appCount);
                    drawLauncher();
                } else if (_activeApp) {
                    _activeApp->setTapCount(_burstDelta);
                    _activeApp->onButton(ButtonEvent::Tap);
                    _activeApp->setTapCount(1);
                }
                _burstActive = false;
                _burstDelta = 0;
            }
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
                        // Multi-tap burst accumulation (batch 3):
                        if (!_burstActive) {
                            // Start a new burst
                            _burstActive = true;
                            _burstStartMs = now;
                            _burstDelta = 1;
                        } else if (core::isInBurstWindow(now, _burstStartMs, BTN_BURST_WINDOW_MS)) {
                            // Within burst window: accumulate
                            _burstDelta++;
                        } else {
                            // Burst window closed: dispatch old, start new
                            _launcherSel = core::wrapSelection(_launcherSel, _burstDelta, _appCount);
                            drawLauncher();
                            _burstStartMs = now;
                            _burstDelta = 1;
                        }
                    }
                } else if (_activeApp) {
                    if (g == core::Gesture::MediumHold) {
                        // Dispatch any pending burst before MediumHold (batch 3).
                        if (_burstActive) {
                            _activeApp->setTapCount(_burstDelta);
                            _activeApp->onButton(ButtonEvent::Tap);
                            _activeApp->setTapCount(1);
                            _burstActive = false;
                            _burstDelta = 0;
                        }
                        _activeApp->onButton(ButtonEvent::MediumHold);
                    } else if (g == core::Gesture::Tap) {
                        // Multi-tap burst accumulation (batch 3):
                        if (!_burstActive) {
                            // Start a new burst
                            _burstActive = true;
                            _burstStartMs = now;
                            _burstDelta = 1;
                        } else if (core::isInBurstWindow(now, _burstStartMs, BTN_BURST_WINDOW_MS)) {
                            // Within burst window: accumulate
                            _burstDelta++;
                        } else {
                            // Burst window closed: dispatch old, start new
                            _activeApp->setTapCount(_burstDelta);
                            _activeApp->onButton(ButtonEvent::Tap);
                            _activeApp->setTapCount(1);
                            _burstStartMs = now;
                            _burstDelta = 1;
                        }
                    }
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
    // Discard any pending burst on state change (batch 3).
    _burstActive = false;
    _burstDelta = 0;
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
            // Button wakeup is always armed (the original, default behaviour).
            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_BOOT, 0);

            // Optional timer wakeup: the soonest registered app that has
            // scheduled work (sleepWakeupSec() >= 0) gets a timer source IN
            // ADDITION to the button. Every app defaults to -1, so without an
            // opt-in (e.g. CalendarApp) this loop yields -1 and behaviour is
            // exactly the button-only sleep the framework always had. We take
            // the MIN over apps so background schedules fire even from the
            // launcher (the user need not keep the app open for it to sync).
            int32_t wakeSec = -1;
            for (int i = 0; i < _appCount; i++) {
                int32_t s = _apps[i]->sleepWakeupSec();
                if (s >= 0 && (wakeSec < 0 || s < wakeSec)) wakeSec = s;
            }
            if (wakeSec >= 0) {
                esp_sleep_enable_timer_wakeup((uint64_t)wakeSec * 1000000ULL);
                Serial.printf("[AppManager] timer wakeup armed in %ld s\n", (long)wakeSec);
            }
            // Only disable timer if we didn't just enable it (avoid error)

            esp_light_sleep_start();
            _lastActivityMs = millis();   // reset on wake
        }
    }
}
