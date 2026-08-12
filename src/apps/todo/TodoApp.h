#pragma once
// ===========================================================================
//  TodoApp  —  Tasks-calendar checklist application (TODO·R1)
// ===========================================================================
//  Renders the synced Tasks cache (TodoStore) as a checklist and lets the
//  user toggle "done" locally. All parsing / sync / store logic lives in the
//  existing seams; this class is pure presentation + gesture routing,
//  modelled on WeatherApp (single screen + menu) and CalendarApp (paged list
//  rendering).
//
//  Model (locked spec): tasks = ALL-DAY events of a dedicated "Tasks" Google
//  Calendar, fetched via the EXISTING ICS mechanism (zero new auth). Done-
//  state is DEVICE-LOCAL only: toggling done writes /todo.json immediately
//  and is NEVER pushed back to Google. The user edits tasks on the phone;
//  the device picks them up on the next sync.
//
//  Screen (single — no view cycle): title + calendar label, last-sync line,
//  then the checklist — OUTSTANDING tasks first ("[ ] title"), a
//  "--- Completed (N) ---" separator, then COMPLETED tasks ("[x] title").
//  The highlight box marks the selected task; the list auto-pages to follow
//  the selection (no separate scroll gesture needed).
//
//  Gestures (decoded by AppManager, delivered as ButtonEvent):
//    Main:   Tap        = move highlight (wraps; skips the group separator;
//                         the list auto-pages to keep the selection visible)
//            MediumHold = return to launcher (AppManager::requestHome())
//            LongHold   = open menu — the house convention shared by EVERY
//                         app's main screen (Calendar / Weather / QR / Reader).
//    Menu:   Tap = move highlight    LongHold = select item
//            (matches CalendarApp / WeatherApp's menu convention exactly)
//
//  Menu items: "Sync now" (TodoSync, then reload cache + redraw),
//  "Toggle done" (toggle done-state on the highlighted task) and
//  "Back to Home" (AppManager::requestHome()).
//
//  On-open resync: onEnter() loads the cache, then auto-refreshes (blocking,
//  "Syncing..." splash) iff the cache is missing OR stale (older than
//  TODO_STALE_SEC) AND the battery is above the floor AND the portal is not
//  running AND STA + TODO_ICS_URL secrets exist. Otherwise it renders the
//  cache, or the empty state.
//
//  Empty states: no TODO_ICS_URL compiled in -> "No Tasks calendar
//  (set TODO_ICS_URL)"; never synced -> "No tasks yet - Hold -> menu ->
//  Sync now"; synced but the calendar is empty -> "All clear".
//
//  No scheduled sync (deliberate): like WeatherApp there is NO onLoop()
//  auto-sync and no sleepWakeupSec() override — the on-open stale check
//  keeps the list fresh whenever the user looks at it, and waking the radio
//  unsolicited for a checklist would only burn battery.
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"
#include "core/TodoModel.h"

class TodoApp : public App {
public:
    explicit TodoApp(SystemContext &ctx);

    // --- Identity ---
    const char* name() const override { return "Todo"; }
    const char* icon() const override { return "[v]"; }   // small check glyph

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
    void loadCache();                 // TodoStore -> _tasks / _doneSet / _lastSyncUtc

    // --- Display-order rows (outstanding, separator, completed) ---
    void rebuildRows();               // _rows[] / _rowTask[] / _rowCount from _tasks
    int  nextSelectableRow(int from) const;   // Tap target (skips the separator)

    // --- Auto-resync decision (on open) ---
    bool shouldAutoSyncOnEnter() const;

    // --- Rendering ---
    void renderMain();
    void renderMenu();

    // --- Menu / actions ---
    void openMenu();
    void menuSelect();
    void runSync();
    void toggleSelected();            // Menu: toggle done + persist NOW

    // --- Helpers ---
    String lastSyncLine() const;

    // --- State ---
    SystemContext    &_ctx;
    core::TodoTask    _tasks[TODO_MAX_TASKS];   // in-memory copy of the cache
    core::TodoDoneSet _doneSet;                 // device-local done-state
    int      _taskCount    = 0;
    int64_t  _lastSyncUtc  = 0;

    // Display rows: task lines + at most one "--- Completed ---" separator.
    // _rowTask[i] is the _tasks[] index of row i, or -1 for the separator.
    String   _rows[TODO_MAX_TASKS + 1];
    int      _rowTask[TODO_MAX_TASKS + 1];
    int      _rowCount     = 0;

    Screen   _screen       = Screen::Main;
    int      _sel          = 0;       // highlighted row (always a task row)
    int      _page         = 0;       // first visible row
    int      _menuSel      = 0;
    bool     _syncing      = false;   // true only while TodoSync::run() blocks

    static const int MENU_COUNT = 3;  // Sync now, Toggle done, Back to Home
};
