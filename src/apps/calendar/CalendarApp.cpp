// ===========================================================================
//  CalendarApp.cpp  —  calendar UI implementation (Round 2)
// ===========================================================================
//  Presentation + gesture routing only. Reuses:
//    CalendarStore  — cache load/save
//    CalendarSync   — on-demand Wi-Fi/NTP/HTTPS refresh
//    core::CalendarDate — todayStartUtc / weekRangeUtc / civilFromUtc / ...
//  Rendering mirrors ReaderApp: clearBuffer -> draw -> flush(true) (full
//  refresh on every screen change keeps the e-ink panel ghost-free).
// ===========================================================================
#include "CalendarApp.h"
#include "CalendarStore.h"
#include "CalendarSync.h"
#include "app/AppManager.h"
#include "core/CalendarDate.h"
#include "core/SyncSchedule.h"

#include <time.h>

// --- Small formatting helpers (all pure, fixed-offset UTC+8) ---------------
static const char *WD_SHORT[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

static String pad2(unsigned v) {
    return v < 10 ? String("0") + String(v) : String(v);
}

// "HH:MM" local wall-clock for a UTC epoch instant.
static String fmtHM(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return pad2(hh) + ":" + pad2(mm);
}

// "YYYY-MM-DD" local date.
static String fmtDate(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return String((long)y) + "-" + pad2(m) + "-" + pad2(d);
}

// "YYYY-MM-DD HH:MM" local date/time (used for the last-sync stamp).
static String fmtDateTime(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return String((long)y) + "-" + pad2(m) + "-" + pad2(d) + " " + pad2(hh) + ":" + pad2(mm);
}

// "MM-DD" compact date for the week grid.
static String fmtMonthDay(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return pad2(m) + "-" + pad2(d);
}

// Category label: CAL_ICS_LABEL_n when defined in secrets.h, else "C0".."C3".
// Each case is individually guarded so undefined feeds fall through to the
// generic label without duplicate-case errors.
static String catLabel(uint8_t c) {
    switch (c) {
#if defined(CAL_ICS_LABEL_0)
        case 0: return CAL_ICS_LABEL_0;
#endif
#if defined(CAL_ICS_LABEL_1)
        case 1: return CAL_ICS_LABEL_1;
#endif
#if defined(CAL_ICS_LABEL_2)
        case 2: return CAL_ICS_LABEL_2;
#endif
#if defined(CAL_ICS_LABEL_3)
        case 3: return CAL_ICS_LABEL_3;
#endif
        default: return String("C") + String(c);
    }
}

// One event row: "HH:MM  Title  [Label]" — or "All-day  Title  [Label]" for
// VALUE=DATE events (no time of day). Title truncated to keep the row on
// one line at the reading font.
static String eventLine(const core::CalendarEvent &e) {
    String title = String(e.title);
    if (title.length() > 38) title = title.substring(0, 35) + "...";
    String tag = String("[") + catLabel(e.category) + "]";
    if (e.allDay) return String("All-day  ") + title + "  " + tag;
    return fmtHM(e.startUtc) + "  " + title + "  " + tag;
}

// --- Construction ----------------------------------------------------------
CalendarApp::CalendarApp(SystemContext &ctx)
    : _ctx(ctx) {}

// --- Lifecycle -------------------------------------------------------------
void CalendarApp::onEnter() {
    loadCache();
    _view   = View::Today;      // always land on Today
    _screen = Screen::Views;
    _page   = 0;
    renderCurrent();
}

void CalendarApp::onExit() {
    // Cache stays in memory; just reset navigation for a clean re-entry.
    _screen = Screen::Views;
    _page   = 0;
}

void CalendarApp::onLoop(uint32_t /*nowMs*/) {
    // Round 3: scheduled sync + boot NTP. Both are no-ops without STA secrets
    // (nothing to sync against), matching CalendarSync's own #if guards.
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    maybeBootNtp();      // one-shot NTP pass if the boot clock is invalid
    maybeAutoSync();     // daily CAL_SYNC_HOUR (06:00) sync when due
#endif
}

// --- Round 3: clock source for scheduling ----------------------------------
int64_t CalendarApp::clockNowUtc() const {
    // TRUE UTC seconds for scheduling decisions. Distinct from uiNowUtc(), which
    // falls back to lastSyncUtc for DISPLAY coherence: scheduling must use the
    // real clock only, because a stale lastSyncUtc would compute nonsense
    // boundaries. time() returns UTC once NTP has fixed the clock; before that it
    // reads garbage near epoch 0, reported here as 0 == "not valid yet" so the
    // caller defers to the boot-NTP path instead of syncing blindly.
    int64_t t = (int64_t)time(nullptr);
    return (t >= CAL_CLOCK_MIN_EPOCH) ? t : 0;
}

// --- Round 3: boot NTP (one-shot) ------------------------------------------
void CalendarApp::maybeBootNtp() {
    // One-shot: after a power cycle the RTC is empty, so the first loop tick
    // with an invalid clock runs a time-only NTP pass. This gives the scheduler
    // (shouldAutoSync / secondsUntilNextSync) a valid "now" without waiting for
    // a manual "Sync now" — and without paying for a full ICS fetch.
    if (_bootNtpTried) return;
    if (clockNowUtc() > 0) { _bootNtpTried = true; return; }   // clock already valid

    // Cheap pre-checks that do NOT consume the one-shot, so we retry next tick
    // rather than lighting up the radio into a busy portal / on a low battery:
    if (_ctx.portal && _ctx.portal->isRunning()) return;       // radio in use
    if (_ctx.batteryPct < CAL_MIN_BATTERY_FOR_SYNC) return;    // battery too low

    _bootNtpTried = true;                                      // consume the one-shot
    Serial.println("[Calendar] boot clock invalid -> NTP time-only pass");
    _ctx.display.showMessage("Calendar", "Syncing time (NTP)...");
    _syncing = true;                                           // wantsSleep() false mid-pass
    CalendarSync sync(_ctx);
    CalendarSyncResult r = sync.syncTimeOnly();                // blocking; own 24 KB task
    _syncing = false;
    Serial.printf("[Calendar] boot NTP: ok=%d msg='%s'\n", (int)r.ok, r.message);

    loadCache();                                               // re-read with a valid clock
    _page = 0;
    renderCurrent();                                           // re-anchor "today" on the fixed clock
}

// --- Round 3: scheduled auto-sync ------------------------------------------
void CalendarApp::maybeAutoSync() {
    int64_t now = clockNowUtc();
    if (now <= 0) return;                          // no valid clock -> maybeBootNtp handles it

    if (!core::shouldAutoSync(now, _lastSyncUtc, CAL_SYNC_HOUR,
                              CAL_TZ_OFFSET_SEC, CAL_SYNC_STALE_SEC))
        return;                                    // not due

    // Debounce: one auto-sync ATTEMPT per local sync-day boundary. After a
    // SUCCESSFUL sync, loadCache() advances _lastSyncUtc past the boundary and
    // shouldAutoSync() goes false on its own; this latch stops a FAILED sync
    // (no secrets / Wi-Fi down / all feeds failed) from re-triggering every
    // loop iteration — it retries at the next daily boundary, or the user runs
    // "Sync now" by hand.
    int64_t boundary = core::syncHourBoundaryUtc(now, CAL_SYNC_HOUR, CAL_TZ_OFFSET_SEC);
    if (now < boundary) boundary -= 86400;         // most recent boundary at/before now
    if (boundary == _lastAutoSyncBoundary) return; // already attempted this sync-day

    // Cheap pre-checks that must NOT consume the debounce slot, so we retry as
    // soon as the condition clears instead of waiting a whole day. Battery is
    // read from AppManager's cached _ctx.batteryPct (refreshed every 15 s) —
    // ADC2 is unreadable while Wi-Fi is up, so it MUST be sampled before, never
    // during, the sync.
    if (_ctx.portal && _ctx.portal->isRunning()) return;        // radio in use by portal
    if (_ctx.batteryPct < CAL_MIN_BATTERY_FOR_SYNC) return;     // battery too low

    _lastAutoSyncBoundary = boundary;              // consume the slot -> attempt now
    Serial.printf("[Calendar] auto-sync due (boundary=%lld lastSync=%lld batt=%d%%)\n",
                  (long long)boundary, (long long)_lastSyncUtc, _ctx.batteryPct);
    runSync();                                     // reuse the manual-sync path: RAII Wi-Fi,
                                                   // portal-guarded, always restores WIFI_OFF
}

// --- Round 3: light-sleep timer wakeup -------------------------------------
int32_t CalendarApp::sleepWakeupSec() {
    // Ask AppManager to wake us for the next scheduled sync so the daily 06:00
    // fetch happens even from light sleep. Without a valid clock we cannot
    // compute a wall-clock target, so return -1 (button-only) and let the
    // boot-NTP path fix the clock the next time we are opened/woken.
    int64_t now = clockNowUtc();
    if (now <= 0) return -1;

    int64_t sec = core::secondsUntilNextSync(now, CAL_SYNC_HOUR, CAL_TZ_OFFSET_SEC);
    // Cap the requested sleep (CAL_WAKE_CAP_SEC, 6 h): instead of sleeping
    // straight through to 06:00, the device wakes periodically, re-evaluates
    // the schedule + battery, and re-sleeps if not yet due. This bounds how long
    // the device is unreachable, lets the stale backstop fire if the exact
    // window is missed, and keeps the "wake for 06:00 then re-sleep" contract.
    if (sec > CAL_WAKE_CAP_SEC) sec = CAL_WAKE_CAP_SEC;
    return (int32_t)sec;   // secondsUntilNextSync() is always > 0 -> valid wakeup
}

// --- Input -----------------------------------------------------------------
void CalendarApp::onButton(ButtonEvent ev) {
    if (_screen == Screen::Menu) {
        // Menu convention matches ReaderApp: Tap moves, LongHold selects.
        if (ev == ButtonEvent::Tap) {
            _menuSel = (_menuSel + 1) % MENU_COUNT;
            renderMenu();
        } else if (ev == ButtonEvent::LongHold) {
            menuSelect();
        }
        return;
    }

    // --- Date views ---
    if (ev == ButtonEvent::Tap) {
        _view = (View)(((int)_view + 1) % 3);   // Today -> Next3 -> Week -> Today
        _page = 0;
        renderCurrent();                        // full refresh inside (view switch)
    } else if (ev == ButtonEvent::MediumHold) {
        _scrollRequest = true;                  // consumed by renderList()
        renderCurrent();
    } else if (ev == ButtonEvent::LongHold) {
        openMenu();
    }
}

// --- Cache -----------------------------------------------------------------
void CalendarApp::loadCache() {
    CalendarStore store(_ctx.storage.fs());
    _eventCount = store.load(_events, CAL_MAX_EVENTS, &_lastSyncUtc);
    if (_eventCount < 0) _eventCount = 0;       // defensive; load() never does
}

// --- Clock source for the views --------------------------------------------
int64_t CalendarApp::uiNowUtc() const {
    int64_t t = (int64_t)time(nullptr);
    // No battery-backed RTC: before the first NTP fix time() is garbage
    // (~ epoch 0 + uptime). Anchor at the last sync instant instead — the
    // cache was materialised at exactly that "now", so Today/Week stay
    // coherent. Never synced and no clock -> 0 -> views render empty with
    // the "Long-hold -> Sync now" hint.
    if (t < CAL_CLOCK_MIN_EPOCH) t = _lastSyncUtc;
    if (t < CAL_CLOCK_MIN_EPOCH) t = 0;
    return t;
}

String CalendarApp::lastSyncLine() const {
    if (_lastSyncUtc <= 0)
        return "Never synced - Long-hold -> Sync now";
    return String("Last sync: ") + fmtDateTime(_lastSyncUtc)
         + "   " + String(_eventCount) + " events cached";
}

// --- Rendering: dispatch ----------------------------------------------------
void CalendarApp::renderCurrent() {
    switch (_view) {
        case View::Today: renderToday(); break;
        case View::Next3: renderNext3(); break;
        case View::Week:  renderWeek();  break;
    }
}

// --- Rendering: Today -------------------------------------------------------
void CalendarApp::renderToday() {
    int64_t now = uiNowUtc();
    int64_t d0  = core::todayStartUtc(now, CAL_TZ_OFFSET_SEC);
    int64_t d1  = d0 + 86400;

    String rows[CAL_MAX_EVENTS];
    int n = 0;
    for (int i = 0; i < _eventCount && n < CAL_MAX_EVENTS; i++) {
        const core::CalendarEvent &e = _events[i];
        if (e.startUtc < d1 && e.endUtc > d0) rows[n++] = eventLine(e);  // overlaps today
    }
    renderList(String("Today  ") + fmtDate(now), rows, n);
}

// --- Rendering: Next 3 days ---------------------------------------------------
void CalendarApp::renderNext3() {
    int64_t now = uiNowUtc();
    int64_t d0  = core::todayStartUtc(now, CAL_TZ_OFFSET_SEC);

    // Rows = per-day separators ("--- Wed 08-03 ---") + that day's events.
    // The events are already sorted by start (expandAndCollect), so a single
    // linear scan per day is enough.
    String rows[CAL_MAX_EVENTS + 3];
    int n = 0;
    for (int day = 0; day < 3 && n < CAL_MAX_EVENTS + 3; day++) {
        int64_t ds = d0 + (int64_t)day * 86400;
        int64_t de = ds + 86400;
        int wd = core::weekdayFromUtc(ds, CAL_TZ_OFFSET_SEC);

        int dayEvents = 0;
        for (int i = 0; i < _eventCount; i++) {
            const core::CalendarEvent &e = _events[i];
            if (e.startUtc < de && e.endUtc > ds) dayEvents++;
        }
        if (dayEvents == 0) continue;           // skip empty days entirely

        rows[n++] = String("--- ") + WD_SHORT[wd] + " " + fmtMonthDay(ds)
                  + (day == 0 ? " (today) ---" : " ---");
        for (int i = 0; i < _eventCount && n < CAL_MAX_EVENTS + 3; i++) {
            const core::CalendarEvent &e = _events[i];
            if (e.startUtc < de && e.endUtc > ds) rows[n++] = eventLine(e);
        }
    }
    renderList("Next 3 Days", rows, n);
}

// --- Rendering: Week (Monday-start grid, 7 rows) ------------------------------
void CalendarApp::renderWeek() {
    int64_t now = uiNowUtc();
    int64_t ws, we;
    core::weekRangeUtc(now, CAL_TZ_OFFSET_SEC, ws, we);
    (void)we;   // window end implicit: ws + 7 days
    int64_t today0 = core::todayStartUtc(now, CAL_TZ_OFFSET_SEC);

    String rows[7];
    int highlight = -1;
    for (int wd = 0; wd < 7; wd++) {
        int64_t ds = ws + (int64_t)wd * 86400;
        int64_t de = ds + 86400;

        int count = 0;
        String first;
        for (int i = 0; i < _eventCount; i++) {
            const core::CalendarEvent &e = _events[i];
            if (e.startUtc < de && e.endUtc > ds) {
                if (count == 0) {
                    first = e.allDay ? String(e.title) : fmtHM(e.startUtc) + " " + String(e.title);
                    if (first.length() > 30) first = first.substring(0, 27) + "...";
                }
                count++;
            }
        }

        String row = String(WD_SHORT[wd]) + " " + fmtMonthDay(ds) + "   "
                   + String(count) + (count == 1 ? " event" : " events");
        if (count > 0) row += "   First: " + first;
        rows[wd] = row;
        if (ds == today0) highlight = wd;       // box today's row
    }
    renderList(String("This Week  ") + fmtMonthDay(ws), rows, 7, highlight);
}

// --- Rendering: generic paged list --------------------------------------------
void CalendarApp::renderList(const String &title, const String *rows,
                             int rowCount, int highlight) {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(50, title, 2);
    d.drawBookText(MARGIN_X, 92, lastSyncLine());

    const int y0 = 130;                                   // first text baseline
    int lh = d.readerLineHeight() + 8;
    int rowsPerPage = (DISPLAY_HEIGHT - 46 - y0) / lh;
    if (rowsPerPage < 1) rowsPerPage = 1;

    // MediumHold scroll: advance one page, wrapping back to the top.
    if (_scrollRequest) {
        _scrollRequest = false;
        if (rowCount > rowsPerPage) {
            _page += rowsPerPage;
            if (_page >= rowCount) _page = 0;             // wrap
        }
    }
    if (_page < 0 || _page >= rowCount) _page = 0;        // clamp (view switch etc.)

    int y = y0;
    if (rowCount == 0) {
        d.drawBookText(MARGIN_X, y + 12, "No events in this view.");
        if (_lastSyncUtc <= 0)
            d.drawBookText(MARGIN_X, y + 44, "Long-hold -> Sync now to fetch your calendars.");
    } else {
        for (int i = _page; i < rowCount && i < _page + rowsPerPage; i++, y += lh) {
            if (i == highlight)
                d.drawSelectionBox(MARGIN_X - 10, y - d.readerAscender() - 4,
                                   d.usableWidth() + 20, lh - 2);
            d.drawBookText(MARGIN_X, y, rows[i]);
        }
    }

    // Footer: gesture legend + page indicator when the list scrolls.
    String foot = "Tap=view   M-Hold=scroll   Hold=menu";
    if (rowCount > rowsPerPage) {
        int pages = (rowCount + rowsPerPage - 1) / rowsPerPage;
        foot += "      " + String(_page / rowsPerPage + 1) + "/" + String(pages);
    }
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14, foot);

    d.flush(true);   // full refresh: whole screen rewritten, kill ghosting
}

// --- Menu --------------------------------------------------------------------
static String menuLabelFor(int i) {
    return i == 0 ? String("Sync now") : String("Back to Home");
}

void CalendarApp::renderMenu() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(74, "Calendar Menu", 2);

    int lh = d.lineHeightFor(1) + 16;
    int x  = DISPLAY_WIDTH / 2 - 220;
    int y  = 230;
    for (int i = 0; i < MENU_COUNT; i++) {
        String label = menuLabelFor(i);
        if (i == _menuSel) {
            int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label, 1);
        y += lh;
    }
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16, "Tap = move    Hold = select");
    d.flush(true);
}

void CalendarApp::openMenu() {
    _menuSel = 0;
    _screen  = Screen::Menu;
    renderMenu();
}

void CalendarApp::menuSelect() {
    if (_menuSel == 0) {
        runSync();
        return;
    }
    // "Back to Home"
    if (_ctx.manager) _ctx.manager->requestHome();
}

// --- Actions -------------------------------------------------------------------
void CalendarApp::runSync() {
    _screen  = Screen::Views;
    _syncing = true;                    // wantsSleep() == false for the duration
    _ctx.display.showMessage("Calendar", "Syncing... (Wi-Fi + NTP)");

    CalendarSync sync(_ctx);
    CalendarSyncResult r = sync.run();  // blocking; safe stack (own 24 KB task)
    _syncing = false;

    loadCache();                        // pick up whatever the sync wrote
    _page = 0;

    Serial.printf("[Calendar] sync: ok=%d feeds=%d/%d events=%d msg='%s'\n",
                  (int)r.ok, r.feedsOk, r.feedsOk + r.feedsFailed,
                  r.eventsFetched, r.message);
    _ctx.display.showMessage(r.ok ? "Sync OK" : "Sync failed", String(r.message));
    delay(1600);                        // let the user read the result
    renderCurrent();                    // back to the current view
}
