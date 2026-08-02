// ===========================================================================
//  TodoApp.cpp  —  Todo checklist UI implementation (TODO·R1)
// ===========================================================================
//  Presentation + gesture routing only. Reuses:
//    TodoStore        — /todo.json cache load/save (tasks + done + sync)
//    TodoSync         — on-demand Wi-Fi/NTP/HTTPS refresh of the Tasks feed
//    core::TodoModel  — todoMergeDone / todoDoneToggle / todoMakeKey
//    core::CalendarDate — civilFromUtc for the last-sync stamp (fixed UTC+8)
//  Rendering mirrors CalendarApp: clearBuffer -> draw -> flush(true) (full
//  refresh on every screen change keeps the e-ink panel ghost-free).
// ===========================================================================
#include "TodoApp.h"
#include "TodoStore.h"
#include "TodoSync.h"
#include "app/AppManager.h"
#include "core/CalendarDate.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

// --- Small formatting helpers (fixed-offset UTC+8, mirrors CalendarApp) -----
static String pad2(unsigned v) {
    return v < 10 ? String("0") + String(v) : String(v);
}

// "YYYY-MM-DD HH:MM" local date/time for the last-sync stamp.
static String fmtDateTime(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return String((long)y) + "-" + pad2(m) + "-" + pad2(d) + " " + pad2(hh) + ":" + pad2(mm);
}

// One checklist row: "[ ] Title" outstanding / "[x] Title" completed. Title
// truncated to keep the row on one line at the reading font.
static String taskLine(const core::TodoTask &t) {
    String title = String(t.title[0] ? t.title : "(untitled)");
    if (title.length() > 44) title = title.substring(0, 41) + "...";
    return String(t.done ? "[x] " : "[ ] ") + title;
}

// --- Construction ----------------------------------------------------------
TodoApp::TodoApp(SystemContext &ctx)
    : _ctx(ctx) {
    core::todoDoneClear(_doneSet);
}

// --- Lifecycle -------------------------------------------------------------
void TodoApp::onEnter() {
    loadCache();
    _screen  = Screen::Main;
    _menuSel = 0;
    _sel     = 0;
    _page    = 0;

    // On-open resync (see header): only when stale/empty AND the cheap
    // pre-checks pass. shouldAutoSyncOnEnter() never touches the radio
    // itself, so a declined auto-sync simply renders the cache/empty state.
    if (shouldAutoSyncOnEnter()) {
        Serial.printf("[Todo] on-open resync (lastSync=%lld batt=%d%%)\n",
                      (long long)_lastSyncUtc, _ctx.batteryPct);
        runSync();          // splash -> sync -> reload -> renderMain
    } else {
        renderMain();
    }
}

void TodoApp::onExit() {
    // Cache stays in memory; just reset navigation for a clean re-entry.
    _screen = Screen::Main;
    _page   = 0;
}

// --- On-open auto-resync decision -------------------------------------------
bool TodoApp::shouldAutoSyncOnEnter() const {
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS) && defined(TODO_ICS_URL)
    // Cheap pre-checks (mirror WeatherApp::shouldAutoSyncOnEnter): battery is
    // read from AppManager's cached _ctx.batteryPct — ADC2 is unreadable while
    // Wi-Fi is up, so it MUST be sampled before, never during, the sync.
    if (_ctx.portal && _ctx.portal->isRunning()) return false;        // radio in use
    if (_ctx.batteryPct < TODO_MIN_BATTERY_FOR_SYNC) return false;    // battery floor (AUTO only)

    if (_lastSyncUtc == 0) return true;              // never synced -> try now

    // Staleness needs a valid clock. The ESP32-S3 has no battery-backed RTC:
    // before an NTP fix time() is garbage, and we would rather show a stale
    // cache than light up the radio on every boot just to judge freshness
    // (the user's manual "Sync now" still works, and any calendar/weather NTP
    // pass fixes the clock for the next open).
    int64_t t = (int64_t)time(nullptr);
    if (t < CAL_CLOCK_MIN_EPOCH) return false;
    return (t - _lastSyncUtc) > TODO_STALE_SEC;
#else
    return false;   // no STA secrets / no TODO_ICS_URL -> nothing to sync against
#endif
}

// --- Input -----------------------------------------------------------------
void TodoApp::onButton(ButtonEvent ev) {
    if (_screen == Screen::Menu) {
        // Menu convention matches CalendarApp/WeatherApp: Tap moves, LongHold selects.
        if (ev == ButtonEvent::Tap) {
            _menuSel = (_menuSel + 1) % MENU_COUNT;
            renderMenu();
        } else if (ev == ButtonEvent::LongHold) {
            menuSelect();
        }
        return;
    }

    // --- Main screen (see header for the documented gesture map) ---
    if (ev == ButtonEvent::Tap) {
        // Move the highlight (wraps, skips the group separator). The list
        // auto-pages to follow in renderMain().
        if (_rowCount > 0) {
            int next = nextSelectableRow(_sel + 1);
            if (next >= 0) _sel = next;
            renderMain();
        }
    } else if (ev == ButtonEvent::MediumHold) {
        toggleSelected();   // secondary-action band: toggle done + persist NOW
    } else if (ev == ButtonEvent::LongHold) {
        openMenu();         // house convention: long hold opens the menu
    }
}

// --- Cache -----------------------------------------------------------------
void TodoApp::loadCache() {
    TodoStore store(_ctx.storage.fs());
    _taskCount = store.load(_tasks, TODO_MAX_TASKS, &_doneSet, &_lastSyncUtc);
    if (_taskCount < 0) _taskCount = 0;              // defensive; load() never does
    core::todoMergeDone(_tasks, _taskCount, _doneSet);   // flag done from the set
    rebuildRows();
}

// --- Display-order rows ------------------------------------------------------
// Outstanding tasks first (feed order), then one "--- Completed (N) ---"
// separator (only when there ARE completed tasks), then the completed tasks.
// _rowTask[] maps each row back to its _tasks[] index (-1 = separator) so the
// toggle gesture knows which task the highlight is on.
void TodoApp::rebuildRows() {
    _rowCount = 0;
    int completed = 0;
    for (int i = 0; i < _taskCount; i++) if (_tasks[i].done) completed++;

    for (int i = 0; i < _taskCount && _rowCount < TODO_MAX_TASKS + 1; i++) {
        if (_tasks[i].done) continue;
        _rows[_rowCount]    = taskLine(_tasks[i]);
        _rowTask[_rowCount] = i;
        _rowCount++;
    }
    if (completed > 0 && _rowCount < TODO_MAX_TASKS + 1) {
        _rows[_rowCount]    = String("--- Completed (") + String(completed) + ") ---";
        _rowTask[_rowCount] = -1;
        _rowCount++;
    }
    for (int i = 0; i < _taskCount && _rowCount < TODO_MAX_TASKS + 1; i++) {
        if (!_tasks[i].done) continue;
        _rows[_rowCount]    = taskLine(_tasks[i]);
        _rowTask[_rowCount] = i;
        _rowCount++;
    }

    // Keep the highlight on a task row (clamp + skip the separator).
    if (_rowCount == 0) { _sel = 0; return; }
    if (_sel >= _rowCount) _sel = _rowCount - 1;
    if (_rowTask[_sel] < 0) {
        int next = nextSelectableRow(_sel + 1);
        _sel = (next >= 0) ? next : 0;
    }
}

// First selectable (task) row at or after `from`, wrapping; -1 when there are
// no task rows at all (empty list).
int TodoApp::nextSelectableRow(int from) const {
    if (_rowCount == 0) return -1;
    for (int step = 0; step < _rowCount; step++) {
        int r = (from + step) % _rowCount;
        if (_rowTask[r] >= 0) return r;
    }
    return -1;
}

String TodoApp::lastSyncLine() const {
    if (_lastSyncUtc <= 0)
        return "Never synced - Hold -> menu -> Sync now";
    int done = 0;
    for (int i = 0; i < _taskCount; i++) if (_tasks[i].done) done++;
    return String("Last sync: ") + fmtDateTime(_lastSyncUtc) + "   "
         + String(_taskCount) + " task(s), " + String(done) + " done";
}

// --- Rendering: main screen --------------------------------------------------
void TodoApp::renderMain() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();

    // Title: app name + calendar label (TODO_ICS_LABEL, default "Tasks").
    d.drawTextCentered(50, String("Todo - ") + String(TODO_ICS_LABEL), 2);
    d.drawBookText(MARGIN_X, 92, lastSyncLine());

    const int y0 = 130;                                   // first text baseline
    int lh = d.readerLineHeight() + 8;
    int rowsPerPage = (DISPLAY_HEIGHT - 46 - y0) / lh;
    if (rowsPerPage < 1) rowsPerPage = 1;

    if (_rowCount == 0) {
        // --- Empty states (see header) ---
#if !defined(TODO_ICS_URL)
        d.drawBookText(MARGIN_X, y0 + 12, "No Tasks calendar (set TODO_ICS_URL)");
        d.drawBookText(MARGIN_X, y0 + 44, "Add TODO_ICS_URL to src/secrets.h and rebuild.");
#else
        if (_lastSyncUtc <= 0) {
            d.drawBookText(MARGIN_X, y0 + 12, "No tasks yet - Hold -> menu -> Sync now.");
        } else {
            d.drawBookText(MARGIN_X, y0 + 12, "All clear - the Tasks calendar is empty.");
        }
#endif
        d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14,
                       "Tap=move   M-Hold=toggle   Hold=menu");
        d.flush(true);
        return;
    }

    // Auto-page: keep the highlighted row visible (no scroll gesture needed).
    if (_sel < _page) _page = _sel;
    if (_sel >= _page + rowsPerPage) _page = _sel - rowsPerPage + 1;
    if (_page < 0) _page = 0;

    int y = y0;
    for (int i = _page; i < _rowCount && i < _page + rowsPerPage; i++, y += lh) {
        if (i == _sel)
            d.drawSelectionBox(MARGIN_X - 10, y - d.readerAscender() - 4,
                               d.usableWidth() + 20, lh - 2);
        d.drawBookText(MARGIN_X, y, _rows[i]);
    }

    // Footer: gesture legend + page indicator when the list scrolls.
    String foot = "Tap=move   M-Hold=toggle   Hold=menu";
    if (_rowCount > rowsPerPage) {
        int pages = (_rowCount + rowsPerPage - 1) / rowsPerPage;
        foot += "      " + String(_page / rowsPerPage + 1) + "/" + String(pages);
    }
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14, foot);

    d.flush(true);   // full refresh: whole screen rewritten, kill ghosting
}

// --- Menu --------------------------------------------------------------------
static String menuLabelFor(int i) {
    return i == 0 ? String("Sync now") : String("Back to Home");
}

void TodoApp::renderMenu() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(74, "Todo Menu", 2);

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

void TodoApp::openMenu() {
    _menuSel = 0;
    _screen  = Screen::Menu;
    renderMenu();
}

void TodoApp::menuSelect() {
    if (_menuSel == 0) {
        runSync();
        return;
    }
    // "Back to Home"
    if (_ctx.manager) _ctx.manager->requestHome();
}

// --- Actions -------------------------------------------------------------------
void TodoApp::toggleSelected() {
    if (_rowCount == 0) return;
    int ti = _rowTask[_sel];
    if (ti < 0 || ti >= _taskCount) return;      // on the separator: no-op

    // Compute the task's stable done-key (UID, or the title+date fallback).
    char key[TODO_UID_MAX];
    core::todoMakeKey(_tasks[ti], key, (int)sizeof(key));

    int toggledTask = ti;
    if (!core::todoDoneToggle(_doneSet, key)) {
        // GUARD (fails loudly): the done-set is full (TODO_DONE_MAX). Surface
        // it instead of silently dropping the user's toggle.
        Serial.println("[Todo] toggle rejected: done-set full");
        _ctx.display.showMessage("Todo", "Done list full - sync to prune stale items");
        delay(1600);
        renderMain();
        return;
    }

    // Re-merge + rebuild so the task moves between the groups immediately...
    core::todoMergeDone(_tasks, _taskCount, _doneSet);
    rebuildRows();
    // ...keeping the highlight on the toggled task in its new position.
    for (int r = 0; r < _rowCount; r++) {
        if (_rowTask[r] == toggledTask) { _sel = r; break; }
    }

    // Persist NOW: done-state is device-local and must survive a power cycle.
    TodoStore store(_ctx.storage.fs());
    if (!store.save(_tasks, _taskCount, _doneSet, _lastSyncUtc)) {
        Serial.println("[Todo] done-toggle persist FAILED");
        _ctx.display.showMessage("Todo", "Save failed (SD full?)");
        delay(1600);
    }
    renderMain();
}

void TodoApp::runSync() {
    _screen  = Screen::Main;
    _syncing = true;                    // wantsSleep() == false for the duration
    _ctx.display.showMessage("Todo", "Syncing tasks... (Wi-Fi + NTP)");

    TodoSync sync(_ctx);
    TodoSyncResult r = sync.run();      // blocking; safe stack (own 24 KB task)
    _syncing = false;

    loadCache();                        // pick up whatever the sync wrote
    _sel  = 0;
    _page = 0;

    Serial.printf("[Todo] sync: ok=%d http=%d tasks=%d msg='%s'\n",
                  (int)r.ok, r.httpStatus, r.tasksFetched, r.message);
    _ctx.display.showMessage(r.ok ? "Sync OK" : "Sync failed", String(r.message));
    delay(1600);                        // let the user read the result
    renderMain();
}
