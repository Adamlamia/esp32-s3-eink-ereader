#pragma once
// ===========================================================================
//  WeatherApp  —  Open-Meteo weather application (WTH·R1)
// ===========================================================================
//  Renders the cached Open-Meteo snapshot (WeatherStore) on a single screen
//  and refreshes it on demand via WeatherSync. All parsing / URL building /
//  sync logic lives in the existing seams; this class is pure presentation +
//  gesture routing, modelled on CalendarApp.
//
//  Screen (single — no view cycle):  location label, current temperature +
//  WMO glyph, feels-like, humidity, wind, a 3-day forecast row (day-of-week
//  labels derived from fetchedUtc via core::CalendarDate), last-fetch line.
//
//  Gestures (decoded by AppManager, delivered as ButtonEvent):
//    Main:   Tap = manual refresh (always allowed — the battery floor
//                WEATHER_MIN_BATTERY_FOR_SYNC guards AUTO resyncs only)
//            MediumHold = deliberate NO-OP (single screen: nothing to scroll
//                or cycle; kept in the gesture map so behaviour is explicit)
//            LongHold = open menu
//    Menu:   Tap = move highlight    LongHold = select item
//            (matches CalendarApp / ReaderApp's menu convention exactly)
//
//  Menu items: "Refresh now" (WeatherSync, then reload cache + redraw) and
//  "Back to Home" (AppManager::requestHome()).
//
//  On-open resync: onEnter() loads the cache, then auto-refreshes (blocking,
//  "Fetching..." splash) iff the snapshot is missing OR stale (older than
//  WEATHER_STALE_SEC) AND the battery is above the floor AND the portal is
//  not running AND STA secrets exist. Otherwise it renders the cache, or the
//  empty state ("No weather yet - Tap to refresh").
//
//  No scheduled sync (deliberate): unlike CalendarApp there is NO
//  sleepWakeupSec() override and no onLoop() auto-sync. Weather has no fixed
//  daily sync hour worth waking the device for; the on-open stale check keeps
//  the data fresh whenever the user actually looks at it, and waking the
//  radio unsolicited would only burn battery. The base-class default
//  (button-only wakeup) is therefore correct for this app.
//
//  Timezone note: WEATHER_TZ ("Asia/Kuala_Lumpur") is UTC+8 with no DST —
//  exactly CAL_TZ_OFFSET_SEC — so the weekday/date math reuses the calendar's
//  core::CalendarDate seams with that offset instead of forking them.
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"
#include "core/OpenMeteo.h"

class WeatherApp : public App {
public:
    explicit WeatherApp(SystemContext &ctx);

    // --- Identity ---
    const char* name() const override { return "Weather"; }
    const char* icon() const override { return "[~~]"; }   // small cloud glyph

    // --- Lifecycle ---
    void onEnter() override;
    void onExit()  override;

    // --- Input ---
    void onButton(ButtonEvent ev) override;

    // --- Optional hooks ---
    // No onLoop() override: no scheduled sync (see header).
    bool wantsSleep() override { return !_syncing; }   // stay awake mid-sync
    // No sleepWakeupSec() override: button-only wakeup is correct here (see
    // header — the on-open stale check replaces any scheduled wakeup).

private:
    // --- Screen state machine ---
    enum class Screen : uint8_t { Main = 0, Menu = 1 };

    // --- Cache ---
    void loadCache();                 // WeatherStore -> _snap

    // --- Auto-resync decision (on open) ---
    bool shouldAutoSyncOnEnter() const;

    // --- Rendering ---
    void renderMain();
    void renderMenu();

    // --- Menu / actions ---
    void openMenu();
    void menuSelect();
    void runSync();

    // --- Helpers ---
    String lastSyncLine() const;

    // --- State ---
    SystemContext        &_ctx;
    core::WeatherSnapshot _snap;      // in-memory copy of the cache
    Screen _screen     = Screen::Main;
    int      _menuSel  = 0;
    bool     _syncing  = false;       // true only while WeatherSync::run() blocks

    static const int MENU_COUNT = 2;  // Refresh now, Back to Home
};
