#pragma once
// ===========================================================================
//  core/AgendaMerge.h  —  calendar + todo → ordered-timeline merge (seam)
// ===========================================================================
//  Header-only, heap-free merge layer behind the split-view launcher's
//  "Today" panel (AGD·R1, docs/PROJECT_BRIEF.md §2.3 Feature 4). It is a
//  thin merge over the two EXISTING caches' in-memory forms — no new network
//  code anywhere:
//
//    - core::CalendarEvent[]  — materialised from /calendar.json
//    - core::TodoTask[]       — from /todo.json (the Todo store)
//
//  and produces one ordered timeline for TODAY:
//
//    1. ALL-DAY section first: calendar all-day events overlapping today
//       (VALUE=DATE semantics) PLUS every NON-DONE todo task dated today,
//       sorted alphabetically by title for a stable display order.
//    2. TIMED section next: calendar timed events overlapping today, sorted
//       by startUtc ascending.
//
//  The seam also reports the "next up" item: the first timed item whose
//  start is strictly in the future (timeUtc > nowUtc); the launcher draws a
//  highlight box around it. All-day items are never "next" (no time of day).
//
//  Todo slot (USER DECISION 2026-08-02): the Todo app is disabled in the
//  launcher pending a backend decision (TODO(TODO-BACKEND) in AppRegistry.h),
//  so the firmware passes todoTasks = nullptr / todoN = 0 for now. The merge
//  fully supports tasks anyway — the path is covered by the native tests —
//  so wiring the todo store in later is a call-site change, never a rewrite.
//
//  No HAL, no Arduino.h, no heap, no exceptions: fixed buffers only, sized
//  by the AGENDA_* / CAL_* / TODO_* constants in config.h (a pure macro
//  header), so the seam stays host-testable under `pio test -e native`.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "core/CalendarEvent.h"   // core::CalendarEvent (pulls in config.h sizing)
#include "core/TodoModel.h"       // core::TodoTask (the clean Todo slot)

namespace core {

// Where a timeline item came from (drives rendering + future interaction).
enum class AgendaSource : uint8_t {
    Calendar = 0,   // materialised calendar event (/calendar.json)
    Todo     = 1,   // task from the Tasks cache (/todo.json) — slot, not wired yet
};

// One row of the merged "Today" timeline. Plain-old-data, fixed size, safe
// to hold in a static array and copy by value.
struct AgendaItem {
    int64_t      timeUtc;                  // event start (dayUtc for all-day/todo)
    char         title[AGENDA_TITLE_MAX];  // NUL-terminated display title
    AgendaSource source;                   // Calendar or Todo
    bool         allDay;                   // true → rendered in the "All-day" section
};

// Merge today's calendar events + todo tasks into an ordered timeline.
//
// out[] receives, in order:
//   1. ALL-DAY items — calendar all-day events overlapping the day window
//      [dayStartUtc, dayEndUtc) plus non-done tasks whose dayUtc falls in
//      [dayStartUtc, dayEndUtc) — sorted alphabetically by title (stable:
//      equal titles keep call order, calendar scanned before todo);
//   2. TIMED items — calendar events with !allDay overlapping the window —
//      sorted by timeUtc ascending (stable on equal starts).
//
// Window rule (identical to CalendarApp's "overlaps today"): an event is in
// today iff startUtc < dayEndUtc && endUtc > dayStartUtc. An event starting
// exactly at dayEndUtc, or ending exactly at dayStartUtc, is OUT.
//
// *nextIdx (optional, may be nullptr) is set to the index of the first timed
// item with timeUtc > nowUtc — the "next up" highlight — or -1 when every
// timed item is in the past or there are none. An event starting exactly at
// nowUtc is NOT "next" (strictly greater).
//
// Returns the number of items written (0..maxOut). maxOut is clamped to
// AGENDA_MAX_ITEMS; when a busy day has more candidates than capacity, the
// alphabetically-first all-day items and the earliest timed items win
// (deterministic, never overflows out[]).
//
// Tolerant by contract: null / negative input arrays are treated as empty;
// a null `out` or non-positive `maxOut` yields 0 (never a crash). No heap,
// no HAL, no exceptions.
inline int agendaMergeToday(const CalendarEvent *calEv, int calN,
                            const TodoTask *todoTasks, int todoN,
                            int64_t nowUtc, int64_t dayStartUtc, int64_t dayEndUtc,
                            AgendaItem *out, int maxOut, int *nextIdx) {
    if (nextIdx) *nextIdx = -1;
    if (!out || maxOut <= 0) return 0;
    if (maxOut > AGENDA_MAX_ITEMS) maxOut = AGENDA_MAX_ITEMS;
    if (!calEv || calN < 0) calN = 0;
    if (!todoTasks || todoN < 0) todoN = 0;
    // Bound the scans to the config capacities (the seam contract elsewhere:
    // serializeCalendarCache clamps to CAL_MAX_EVENTS, todoExtractTasks to
    // TODO_MAX_TASKS), so the fixed marker buffers below can never overflow.
    if (calN > CAL_MAX_EVENTS) calN = CAL_MAX_EVENTS;
    if (todoN > TODO_MAX_TASKS) todoN = TODO_MAX_TASKS;

    // --- Eligibility pass (fixed buffers, no heap) --------------------------
    bool calAllDay[CAL_MAX_EVENTS]  = {};   // all-day candidate flags
    bool calTimed[CAL_MAX_EVENTS]   = {};   // timed candidate flags
    bool todoElig[TODO_MAX_TASKS]   = {};   // non-done task-dated-today flags
    bool takenCal[CAL_MAX_EVENTS]   = {};   // consumed by the selection below
    bool takenTodo[TODO_MAX_TASKS]  = {};
    int nCalAllDay = 0, nCalTimed = 0, nTodo = 0;

    for (int i = 0; i < calN; ++i) {
        const CalendarEvent &e = calEv[i];
        if (!(e.startUtc < dayEndUtc && e.endUtc > dayStartUtc)) continue;  // not today
        if (e.allDay) { calAllDay[i] = true; ++nCalAllDay; }
        else          { calTimed[i]  = true; ++nCalTimed; }
    }
    for (int i = 0; i < todoN; ++i) {
        const TodoTask &t = todoTasks[i];
        if (t.done) continue;                              // done tasks never show
        if (t.dayUtc < dayStartUtc || t.dayUtc >= dayEndUtc) continue;  // not today
        todoElig[i] = true; ++nTodo;
    }

    int written = 0;

    // --- Section 1: all-day items, alphabetical by title (stable) -----------
    // Selection sort over the candidate flags: each round picks the smallest
    // untaken title. Strict < keeps the FIRST minimum, so equal titles hold
    // their call order and calendar wins ties over todo (scanned first).
    int nAllDay = nCalAllDay + nTodo;
    if (nAllDay > maxOut) nAllDay = maxOut;                // all-day fills first
    for (int k = 0; k < nAllDay; ++k) {
        int bestCal = -1, bestTodo = -1;
        const char *bestTitle = nullptr;
        for (int i = 0; i < calN; ++i) {
            if (!calAllDay[i] || takenCal[i]) continue;
            if (!bestTitle || strcmp(calEv[i].title, bestTitle) < 0) {
                bestTitle = calEv[i].title; bestCal = i; bestTodo = -1;
            }
        }
        for (int i = 0; i < todoN; ++i) {
            if (!todoElig[i] || takenTodo[i]) continue;
            if (!bestTitle || strcmp(todoTasks[i].title, bestTitle) < 0) {
                bestTitle = todoTasks[i].title; bestTodo = i; bestCal = -1;
            }
        }
        if (bestCal < 0 && bestTodo < 0) break;            // defensive: none left

        AgendaItem &it = out[written++];
        if (bestCal >= 0) {
            const CalendarEvent &e = calEv[bestCal];
            takenCal[bestCal] = true;
            it.timeUtc = e.startUtc;
            strncpy(it.title, e.title, AGENDA_TITLE_MAX - 1);
            it.title[AGENDA_TITLE_MAX - 1] = '\0';         // always NUL-terminated
            it.source = AgendaSource::Calendar;
        } else {
            const TodoTask &t = todoTasks[bestTodo];
            takenTodo[bestTodo] = true;
            it.timeUtc = t.dayUtc;
            strncpy(it.title, t.title, AGENDA_TITLE_MAX - 1);
            it.title[AGENDA_TITLE_MAX - 1] = '\0';
            it.source = AgendaSource::Todo;
        }
        it.allDay = true;
    }

    // --- Section 2: timed items, ascending by startUtc (stable) -------------
    int nTimed = nCalTimed;
    if (nTimed > maxOut - written) nTimed = maxOut - written;   // whatever fits
    for (int k = 0; k < nTimed; ++k) {
        int best = -1;
        int64_t bestTime = 0;
        for (int i = 0; i < calN; ++i) {
            if (!calTimed[i] || takenCal[i]) continue;
            if (best < 0 || calEv[i].startUtc < bestTime) {    // strict < → stable
                best = i; bestTime = calEv[i].startUtc;
            }
        }
        if (best < 0) break;                               // defensive: none left
        takenCal[best] = true;

        const CalendarEvent &e = calEv[best];
        AgendaItem &it = out[written++];
        it.timeUtc = e.startUtc;
        strncpy(it.title, e.title, AGENDA_TITLE_MAX - 1);
        it.title[AGENDA_TITLE_MAX - 1] = '\0';
        it.source = AgendaSource::Calendar;
        it.allDay = false;
    }

    // --- "Next up" highlight -------------------------------------------------
    // The timed section is ascending, so the first future item is the soonest.
    if (nextIdx) {
        for (int i = 0; i < written; ++i) {
            if (!out[i].allDay && out[i].timeUtc > nowUtc) { *nextIdx = i; break; }
        }
    }
    return written;
}

} // namespace core
