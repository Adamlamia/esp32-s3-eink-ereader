#pragma once
// ===========================================================================
//  CalendarApp  —  networked calendar application (Round 2)
// ===========================================================================
//  Renders the synced calendar cache (CalendarStore) in three views and
//  refreshes it on demand via CalendarSync. All parsing / date math / sync
//  logic lives in the existing seams; this class is pure presentation +
//  gesture routing, modelled on ReaderApp.
//
//  Views (Tap cycles):  Today  ->  Next 3 days  ->  Week (Monday-start)
//
//  Gestures (decoded by AppManager, delivered as ButtonEvent):
//    Views:  Tap = cycle view        MediumHold = scroll list (page, wrap)
//            LongHold = open menu
//    Menu:   Tap = move highlight    LongHold = select item
//            (matches ReaderApp's menu convention exactly)
//
//  Menu items: "Sync now" (CalendarSync, then reload cache + redraw) and
//  "Back to Home" (AppManager::requestHome()).
//
//  Clock note: the ESP32-S3 has no battery-backed RTC, so between boots the
//  wall clock is unknown until a sync runs NTP. The views anchor "today" at
//  max(time(nullptr), lastSyncUtc) — the cache was materialised at the last
//  sync instant, so the UI stays coherent even before the next NTP fix.
//
//  Round 3 adds the scheduled layer on top of that clock model:
//    - first onLoop: if the clock is invalid (post power-cycle) and STA secrets
//      exist, run a time-only NTP pass (CalendarSync::syncTimeOnly) so the
//      scheduling math has a valid "now" to work with.
//    - onLoop: core::shouldAutoSync() triggers the daily CAL_SYNC_HOUR (06:00)
//      sync when due — portal- and battery-guarded, debounced per sync-day.
//    - sleepWakeupSec(): asks AppManager for a timer wakeup until the next
//      06:00 so the device wakes to sync even from light sleep.
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"
#include "core/CalendarEvent.h"

class CalendarApp : public App {
public:
    explicit CalendarApp(SystemContext &ctx);

    // --- Identity ---
    const char* name() const override { return "Calendar"; }
    const char* icon() const override { return "[##]"; }   // small grid glyph

    // --- Lifecycle ---
    void onEnter() override;
    void onExit()  override;

    // --- Input ---
    void onButton(ButtonEvent ev) override;

    // --- Optional hooks ---
    void onLoop(uint32_t nowMs) override;     // scheduled sync tick + boot NTP (R3)
    bool wantsSleep() override { return !_syncing; }   // stay awake mid-sync

private:
    // --- View / screen state machines ---
    enum class View   : uint8_t { Today = 0, Next3 = 1, Week = 2 };
    enum class Screen : uint8_t { Views = 0, Menu = 1 };

    // --- Cache ---
    void loadCache();                 // CalendarStore -> _events / _lastSyncUtc

    // --- Rendering ---
    void renderCurrent();             // dispatch on _view
    void renderToday();
    void renderNext3();
    void renderWeek();
    void renderList(const String &title, const String *rows, int rowCount,
                    int highlight = -1);
    void renderMenu();

    // --- Menu / actions ---
    void openMenu();
    void menuSelect();
    void runSync();

    // --- Helpers ---
    int64_t uiNowUtc() const;         // clock source for the views (see header)
    int64_t clockNowUtc() const;      // real clock for scheduling (0 == invalid)
    void    maybeBootNtp();           // one-shot NTP pass on an invalid boot clock (R3)
    void    maybeAutoSync();          // daily scheduled sync when due (R3)
    String  lastSyncLine() const;

    // --- State ---
    SystemContext      &_ctx;
    core::CalendarEvent _events[CAL_MAX_EVENTS];   // in-memory copy of the cache
    int      _eventCount   = 0;
    int64_t  _lastSyncUtc  = 0;
    View     _view         = View::Today;
    Screen   _screen       = Screen::Views;
    int      _page         = 0;       // first visible row in Today / Next3
    bool     _scrollRequest = false;  // MediumHold: advance one page on redraw
    int      _menuSel      = 0;
    bool     _syncing      = false;   // true only while CalendarSync::run() blocks

    // --- Round 3 scheduling state ---
    int64_t  _lastAutoSyncBoundary = 0;  // debounce: last sync-day boundary attempted
    bool     _bootNtpTried = false;      // one-shot boot-NTP latch

    static const int MENU_COUNT = 2;  // Sync now, Back to Home
};
