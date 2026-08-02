// ===========================================================================
//  test_agenda  —  Unit tests for the AgendaMerge seam (AGD·R1, Feature 4)
// ===========================================================================
//  Hardware-free tests for the pure calendar+todo → ordered-timeline merge
//  in src/core/AgendaMerge.h (namespace core), the testable seam behind the
//  split-view launcher's "Today" panel (docs/PROJECT_BRIEF.md §2.3 Feature 4).
//
//  Covers the locked spec invariants:
//    - all-day section first (alphabetical by title, stable), timed section
//      next (ascending startUtc, stable);
//    - the day-window overlap rule (start < dayEnd && end > dayStart) with
//      exact-boundary behaviour (start == dayEnd OUT, end == dayStart OUT);
//    - multi-day all-day events spanning today are included;
//    - the clean Todo slot: non-done tasks dated today appear as all-day
//      source=Todo items; done tasks and out-of-day tasks are excluded
//      (this path is NOT wired in firmware yet — nullptr/0 — but MUST work);
//    - *nextIdx = first timed item strictly after nowUtc, -1 when all timed
//      items are past or there are none (an event exactly at now is NOT next);
//    - capacity clamping (more candidates than maxOut → deterministic keep,
//      no overflow) and null / zero argument tolerance (never a crash);
//    - over-long titles are truncated to AGENDA_TITLE_MAX-1, always NUL-term.
//
//  All fixtures use SYNTHETIC events + tasks — never the user's real
//  calendar data (secrets stay out of the test tree).
// ===========================================================================
#include <unity.h>
#include <cstring>
#include <cstdio>
#include "config.h"
#include "core/CalendarEvent.h"
#include "core/CalendarDate.h"
#include "core/TodoModel.h"
#include "core/AgendaMerge.h"

using namespace core;

// --- Fixture: a fixed "today" (2026-08-03 local, UTC+8) ---------------------
static int64_t DAY0;    // local midnight today, UTC epoch seconds
static int64_t DAY1;    // local midnight tomorrow
static int64_t NOW;     // anchor "now": noon today

void setUp(void) {
    DAY0 = epochFromCivil(2026, 8, 3, 0, 0, 0, CAL_TZ_OFFSET_SEC);
    DAY1 = DAY0 + 86400;
    NOW  = DAY0 + 12 * 3600;                 // 12:00 local
}
void tearDown(void) {}

// --- Fixture helpers ---------------------------------------------------------
static CalendarEvent mkEvent(int64_t s, int64_t e, const char *title, bool allDay) {
    CalendarEvent ev;
    calEventClear(ev);
    ev.startUtc = s;
    ev.endUtc   = e;
    strncpy(ev.title, title, CAL_TITLE_MAX - 1);
    ev.title[CAL_TITLE_MAX - 1] = '\0';
    ev.allDay = allDay;
    return ev;
}

static TodoTask mkTodo(const char *title, int64_t dayUtc, bool done) {
    TodoTask t;
    todoTaskClear(t);
    strncpy(t.title, title, TODO_TITLE_MAX - 1);
    t.title[TODO_TITLE_MAX - 1] = '\0';
    t.dayUtc = dayUtc;
    t.done   = done;
    return t;
}

// ===========================================================================
//  Empty / tolerant inputs
// ===========================================================================
void test_agenda_empty_inputs(void) {
    AgendaItem out[8];
    int next = 7;
    int n = agendaMergeToday(nullptr, 0, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT(-1, next);

    // Null event pointer with a non-zero count is treated as empty, not a crash.
    next = 7;
    n = agendaMergeToday(nullptr, 5, nullptr, 3, NOW, DAY0, DAY1, out, 8, &next);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT(-1, next);

    // Null output buffer / zero capacity → 0, and a null nextIdx is safe.
    TEST_ASSERT_EQUAL_INT(0, agendaMergeToday(nullptr, 0, nullptr, 0,
                                              NOW, DAY0, DAY1, nullptr, 8, nullptr));
    TEST_ASSERT_EQUAL_INT(0, agendaMergeToday(nullptr, 0, nullptr, 0,
                                              NOW, DAY0, DAY1, out, 0, &next));
}

// ===========================================================================
//  All-day only: alphabetical, stable, never "next"
// ===========================================================================
void test_agenda_allday_only_sorted_alpha(void) {
    CalendarEvent cal[3] = {
        mkEvent(DAY0, DAY1, "Charlie", true),
        mkEvent(DAY0, DAY1, "Alpha",   true),
        mkEvent(DAY0, DAY1, "Bravo",   true),
    };
    AgendaItem out[8];
    int next = 99;
    int n = agendaMergeToday(cal, 3, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("Alpha",   out[0].title);
    TEST_ASSERT_EQUAL_STRING("Bravo",   out[1].title);
    TEST_ASSERT_EQUAL_STRING("Charlie", out[2].title);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(out[i].allDay);
        TEST_ASSERT_EQUAL_INT((int)AgendaSource::Calendar, (int)out[i].source);
    }
    TEST_ASSERT_EQUAL_INT(-1, next);         // no timed items → nothing is "next"
}

// ===========================================================================
//  Timed only: ascending startUtc, nextIdx = first future item
// ===========================================================================
void test_agenda_timed_only_sorted_time(void) {
    CalendarEvent cal[3] = {
        mkEvent(NOW + 3 * 3600, NOW + 4 * 3600, "Late", false),
        mkEvent(NOW + 1 * 3600, NOW + 2 * 3600, "Soon", false),
        mkEvent(NOW - 1 * 3600, NOW - 0 * 3600, "Past", false),
    };
    AgendaItem out[8];
    int next = -2;
    int n = agendaMergeToday(cal, 3, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("Past", out[0].title);
    TEST_ASSERT_EQUAL_STRING("Soon", out[1].title);
    TEST_ASSERT_EQUAL_STRING("Late", out[2].title);
    TEST_ASSERT_FALSE(out[0].allDay);
    TEST_ASSERT_EQUAL_INT64(NOW - 3600, out[0].timeUtc);
    TEST_ASSERT_EQUAL_INT(1, next);          // "Soon" is the first future item
}

// ===========================================================================
//  Mixed: all-day section first, then timed sorted
// ===========================================================================
void test_agenda_mixed_allday_first(void) {
    CalendarEvent cal[3] = {
        mkEvent(NOW + 2 * 3600, NOW + 3 * 3600, "Beta meeting", false),
        mkEvent(DAY0, DAY1, "Zebra holiday", true),
        mkEvent(NOW + 1 * 3600, NOW + 2 * 3600, "Alpha call", false),
    };
    TodoTask todo[1] = { mkTodo("Milk", DAY0, false) };

    AgendaItem out[8];
    int next = -2;
    int n = agendaMergeToday(cal, 3, todo, 1, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(4, n);
    // All-day first, alphabetical across BOTH sources ("Milk" < "Zebra holiday").
    TEST_ASSERT_EQUAL_STRING("Milk", out[0].title);
    TEST_ASSERT_EQUAL_INT((int)AgendaSource::Todo, (int)out[0].source);
    TEST_ASSERT_TRUE(out[0].allDay);
    TEST_ASSERT_EQUAL_INT64(DAY0, out[0].timeUtc);
    TEST_ASSERT_EQUAL_STRING("Zebra holiday", out[1].title);
    TEST_ASSERT_EQUAL_INT((int)AgendaSource::Calendar, (int)out[1].source);
    // Then timed, ascending.
    TEST_ASSERT_EQUAL_STRING("Alpha call", out[2].title);
    TEST_ASSERT_EQUAL_STRING("Beta meeting", out[3].title);
    TEST_ASSERT_FALSE(out[2].allDay);
    TEST_ASSERT_EQUAL_INT(2, next);          // first timed future item
}

// ===========================================================================
//  Window filtering: events outside today are excluded
// ===========================================================================
void test_agenda_outside_window_excluded(void) {
    CalendarEvent cal[4] = {
        mkEvent(DAY0 - 2 * 86400, DAY0 - 86400, "Yesterday timed", false),  // past
        mkEvent(DAY1 + 3600, DAY1 + 7200, "Tomorrow timed", false),          // future day
        mkEvent(DAY1, DAY1 + 86400, "Starts at midnight tmr", true),         // all-day tomorrow
        mkEvent(NOW + 3600, NOW + 7200, "Today ok", false),                // kept
    };
    AgendaItem out[8];
    int next = -2;
    int n = agendaMergeToday(cal, 4, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("Today ok", out[0].title);
    TEST_ASSERT_EQUAL_INT(0, next);          // the only timed item is future (NOW+1h)
}

// ===========================================================================
//  Multi-day all-day event spanning today is included
// ===========================================================================
void test_agenda_multiday_allday_spanning(void) {
    CalendarEvent cal[2] = {
        mkEvent(DAY0 - 2 * 86400, DAY1 + 86400, "Conference", true),  // spans today
        mkEvent(DAY0 - 3 * 86400, DAY0, "Ended last night", true),    // end == dayStart → OUT
    };
    AgendaItem out[8];
    int next = 99;
    int n = agendaMergeToday(cal, 2, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("Conference", out[0].title);
    TEST_ASSERT_TRUE(out[0].allDay);
    TEST_ASSERT_EQUAL_INT(-1, next);
}

// ===========================================================================
//  Todo slot: non-done tasks dated today join as all-day source=Todo
// ===========================================================================
void test_agenda_todo_integration(void) {
    TodoTask todo[4] = {
        mkTodo("Buy milk",     DAY0,        false),  // kept
        mkTodo("Done already", DAY0,        true),   // done → excluded
        mkTodo("Tomorrow job", DAY1,        false),  // dayUtc == dayEnd → excluded
        mkTodo("Old task",     DAY0 - 86400, false), // before dayStart → excluded
    };
    AgendaItem out[8];
    int next = 99;
    int n = agendaMergeToday(nullptr, 0, todo, 4, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("Buy milk", out[0].title);
    TEST_ASSERT_EQUAL_INT((int)AgendaSource::Todo, (int)out[0].source);
    TEST_ASSERT_TRUE(out[0].allDay);
    TEST_ASSERT_EQUAL_INT64(DAY0, out[0].timeUtc);
    TEST_ASSERT_EQUAL_INT(-1, next);         // tasks are all-day → never "next"
}

// ===========================================================================
//  nextIdx when now sits between events
// ===========================================================================
void test_agenda_nextidx_between_events(void) {
    CalendarEvent cal[3] = {
        mkEvent(NOW - 3600, NOW - 1800, "Done already", false),
        mkEvent(NOW + 3600, NOW + 5400, "Next up", false),
        mkEvent(NOW + 7200, NOW + 9000, "Later", false),
    };
    AgendaItem out[8];
    int next = -2;
    int n = agendaMergeToday(cal, 3, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(1, next);
    TEST_ASSERT_EQUAL_STRING("Next up", out[next].title);
}

// ===========================================================================
//  nextIdx = -1 when every timed item is in the past
// ===========================================================================
void test_agenda_nextidx_all_past(void) {
    CalendarEvent cal[2] = {
        mkEvent(DAY0 + 3600, DAY0 + 7200, "Morning", false),   // 01:00..02:00
        mkEvent(NOW - 1800, NOW - 600, "Just ended", false),
    };
    AgendaItem out[8];
    int next = 99;
    int n = agendaMergeToday(cal, 2, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(-1, next);
}

// ===========================================================================
//  Capacity: more candidates than maxOut → clamped, deterministic, no overflow
// ===========================================================================
void test_agenda_capacity_clamped(void) {
    // Five timed events, room for three → the three EARLIEST win.
    CalendarEvent cal[5];
    for (int i = 0; i < 5; i++) {
        char title[16];
        snprintf(title, sizeof(title), "T%d", i);
        cal[i] = mkEvent(NOW + (int64_t)(i - 2) * 3600,
                         NOW + (int64_t)(i - 2) * 3600 + 1800, title, false);
    }
    AgendaItem out[3];
    int next = -2;
    int n = agendaMergeToday(cal, 5, nullptr, 0, NOW, DAY0, DAY1, out, 3, &next);

    TEST_ASSERT_EQUAL_INT(3, n);             // clamped to maxOut
    TEST_ASSERT_EQUAL_STRING("T0", out[0].title);   // NOW-2h (earliest)
    TEST_ASSERT_EQUAL_STRING("T1", out[1].title);   // NOW-1h
    TEST_ASSERT_EQUAL_STRING("T2", out[2].title);   // NOW+0h
    TEST_ASSERT_EQUAL_INT(-1, next);         // T2 starts AT now → not strictly future

    // All-day overflow keeps the alphabetically-first titles.
    CalendarEvent ad[3] = {
        mkEvent(DAY0, DAY1, "Zulu", true),
        mkEvent(DAY0, DAY1, "Mike", true),
        mkEvent(DAY0, DAY1, "Alpha", true),
    };
    AgendaItem out2[2];
    int next2 = 99;
    int n2 = agendaMergeToday(ad, 3, nullptr, 0, NOW, DAY0, DAY1, out2, 2, &next2);
    TEST_ASSERT_EQUAL_INT(2, n2);
    TEST_ASSERT_EQUAL_STRING("Alpha", out2[0].title);
    TEST_ASSERT_EQUAL_STRING("Mike", out2[1].title);
    TEST_ASSERT_EQUAL_INT(-1, next2);
}

// ===========================================================================
//  Exact day-window boundaries
// ===========================================================================
void test_agenda_day_boundaries(void) {
    CalendarEvent cal[4] = {
        mkEvent(DAY0, DAY0 + 3600, "Starts at midnight", false),     // start == dayStart → IN
        mkEvent(DAY1 - 3600, DAY1, "Ends at midnight", false),       // end == dayEnd → IN
        mkEvent(DAY1, DAY1 + 3600, "Starts at dayEnd", false),       // start == dayEnd → OUT
        mkEvent(DAY0 - 7200, DAY0, "Ended at dayStart", false),      // end == dayStart → OUT
    };
    AgendaItem out[8];
    int next = -2;
    int n = agendaMergeToday(cal, 4, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("Starts at midnight", out[0].title);
    TEST_ASSERT_EQUAL_STRING("Ends at midnight", out[1].title);
    // 00:00..01:00 is past (now = noon); 23:00..24:00 is still upcoming.
    TEST_ASSERT_EQUAL_INT(1, next);
}

// ===========================================================================
//  nextIdx strictness: an event starting exactly at now is NOT "next"
// ===========================================================================
void test_agenda_nextidx_strictly_future(void) {
    CalendarEvent cal[2] = {
        mkEvent(NOW, NOW + 3600, "Starting now", false),      // == now → not next
        mkEvent(NOW + 60, NOW + 3600, "One minute out", false),
    };
    AgendaItem out[8];
    int next = -2;
    int n = agendaMergeToday(cal, 2, nullptr, 0, NOW, DAY0, DAY1, out, 8, &next);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(1, next);
    TEST_ASSERT_EQUAL_STRING("One minute out", out[next].title);
}

// ===========================================================================
//  Title truncation: over-long cache titles never overflow AgendaItem
// ===========================================================================
void test_agenda_title_truncated_safely(void) {
    // 63-char title (fills CAL_TITLE_MAX-1) → clamped to AGENDA_TITLE_MAX-1.
    char longTitle[CAL_TITLE_MAX];
    memset(longTitle, 'x', CAL_TITLE_MAX - 1);
    longTitle[CAL_TITLE_MAX - 1] = '\0';
    TEST_ASSERT_TRUE(strlen(longTitle) > AGENDA_TITLE_MAX);   // precondition

    CalendarEvent cal[1] = { mkEvent(DAY0 + 3600, DAY0 + 7200, longTitle, false) };
    AgendaItem out[2];
    int next = -2;
    int n = agendaMergeToday(cal, 1, nullptr, 0, NOW, DAY0, DAY1, out, 2, &next);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(AGENDA_TITLE_MAX - 1, (int)strlen(out[0].title));
    TEST_ASSERT_EQUAL_CHAR('\0', out[0].title[AGENDA_TITLE_MAX - 1]);
}

// ===========================================================================
int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_agenda_empty_inputs);
    RUN_TEST(test_agenda_allday_only_sorted_alpha);
    RUN_TEST(test_agenda_timed_only_sorted_time);
    RUN_TEST(test_agenda_mixed_allday_first);
    RUN_TEST(test_agenda_outside_window_excluded);
    RUN_TEST(test_agenda_multiday_allday_spanning);
    RUN_TEST(test_agenda_todo_integration);
    RUN_TEST(test_agenda_nextidx_between_events);
    RUN_TEST(test_agenda_nextidx_all_past);
    RUN_TEST(test_agenda_capacity_clamped);
    RUN_TEST(test_agenda_day_boundaries);
    RUN_TEST(test_agenda_nextidx_strictly_future);
    RUN_TEST(test_agenda_title_truncated_safely);
    return UNITY_END();
}
