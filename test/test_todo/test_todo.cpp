// ===========================================================================
//  test_todo  —  Unit tests for the TodoModel seam (TODO·R1)
// ===========================================================================
//  Hardware-free tests for the pure seam in src/core/TodoModel.h (namespace
//  core), plus pins for the shared-parser UID capture it depends on
//  (src/core/IcsParser.h + CalendarEvent.h). The std::string stands in for
//  the on-disk /todo.json file, exactly like test_calendar_store does for
//  CalendarStore: ArduinoJson is header-only and identical on the host.
//
//  Covers the locked spec invariants:
//    - tasks = ALL-DAY events only (timed events are excluded — negative);
//    - done-state identity is the ICS UID, stable across a phone-side re-sync
//      even when the task is RENAMED (the reason UID capture was added), with
//      a (title + date) fallback key for feeds that omit UID;
//    - stale done-keys are pruned when a task is deleted on the phone;
//    - the store seam tolerates empty / garbage / truncated documents and
//      bounds hostile input (never crashes);
//    - the full-set toggle guard fails loudly (returns false, set untouched).
//
//  All fixtures use SYNTHETIC ICS + synthetic UIDs — never the user's real
//  calendar data or URLs (secrets stay out of the test tree).
// ===========================================================================
#include <unity.h>
#include <cstring>
#include <cstdio>
#include <string>
#include "config.h"
#include "core/CalendarEvent.h"
#include "core/CalendarDate.h"
#include "core/IcsParser.h"
#include "core/TodoModel.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// --- Fixture helpers ---------------------------------------------------------
static CalendarEvent mkEvent(int64_t s, int64_t e, const char *title,
                             const char *uid, bool allDay) {
    CalendarEvent ev;
    calEventClear(ev);
    ev.startUtc = s;
    ev.endUtc   = e;
    strncpy(ev.title, title, CAL_TITLE_MAX - 1);
    ev.title[CAL_TITLE_MAX - 1] = '\0';
    strncpy(ev.uid, uid, CAL_UID_MAX - 1);
    ev.uid[CAL_UID_MAX - 1] = '\0';
    ev.allDay = allDay;
    return ev;
}

// Local-midnight UTC epoch of a civil date at CAL_TZ_OFFSET_SEC.
static int64_t dayUtc(int64_t y, unsigned m, unsigned d) {
    return dayStartUtc(y, m, d, CAL_TZ_OFFSET_SEC);
}

// Synthetic two-event Tasks-calendar feed: one ALL-DAY task + one timed event.
static const char *ICS_MIXED =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Google Inc//Google Calendar 70.9054//EN\r\n"
    "BEGIN:VEVENT\r\n"
    "DTSTART;VALUE=DATE:20260810\r\n"
    "DTEND;VALUE=DATE:20260811\r\n"
    "SUMMARY:Buy milk\r\n"
    "UID:task-1@google.com\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\n"
    "DTSTART:20260810T090000Z\r\n"
    "DTEND:20260810T100000Z\r\n"
    "SUMMARY:Standup meeting\r\n"
    "UID:evt-2@google.com\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n";

// ===========================================================================
//  Parser UID capture (pins the TODO-R1 extension of the shared parser)
// ===========================================================================
void test_parser_captures_uid(void) {
    CalendarEvent ev[4];
    int n = parseIcsFeed(ICS_MIXED, 0, ev, 4, CAL_TZ_OFFSET_SEC);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("task-1@google.com", ev[0].uid);
    TEST_ASSERT_EQUAL_STRING("evt-2@google.com", ev[1].uid);
    TEST_ASSERT_TRUE(ev[0].allDay);
    TEST_ASSERT_FALSE(ev[1].allDay);
}

void test_parser_uid_truncated_safely(void) {
    // A hostile UID longer than CAL_UID_MAX must be truncated to the buffer
    // (NUL-terminated), never overran.
    std::string longUid(CAL_UID_MAX + 19, 'u');
    std::string ics =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20260810\r\n"
        "SUMMARY:Big UID\r\n"
        "UID:" + longUid + "\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n";

    CalendarEvent ev[1];
    int n = parseIcsFeed(ics.c_str(), 0, ev, 1, CAL_TZ_OFFSET_SEC);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(CAL_UID_MAX - 1, (int)strlen(ev[0].uid));   // truncated
    TEST_ASSERT_EQUAL_UINT8('u', (uint8_t)ev[0].uid[CAL_UID_MAX - 2]);
}

void test_parser_missing_uid_is_empty(void) {
    const char *ics =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20260810\r\n"
        "SUMMARY:No UID here\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n";
    CalendarEvent ev[1];
    int n = parseIcsFeed(ics, 0, ev, 1, CAL_TZ_OFFSET_SEC);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("", ev[0].uid);     // empty, not garbage
}

// ===========================================================================
//  Task extraction (all-day only)
// ===========================================================================
void test_extract_all_day_only(void) {
    // NEGATIVE pin: timed events are calendar events, NOT tasks — excluded.
    CalendarEvent ev[3];
    int parsed = parseIcsFeed(ICS_MIXED, 0, ev, 3, CAL_TZ_OFFSET_SEC);
    TEST_ASSERT_EQUAL_INT(2, parsed);

    TodoTask tasks[TODO_MAX_TASKS];
    int n = todoExtractTasks(ev, parsed, tasks, TODO_MAX_TASKS);
    TEST_ASSERT_EQUAL_INT(1, n);                 // only the all-day event
    TEST_ASSERT_EQUAL_STRING("Buy milk", tasks[0].title);
    TEST_ASSERT_EQUAL_STRING("task-1@google.com", tasks[0].uid);
    TEST_ASSERT_EQUAL_INT64(dayUtc(2026, 8, 10), tasks[0].dayUtc);
    TEST_ASSERT_FALSE(tasks[0].done);
}

void test_extract_copies_fields(void) {
    CalendarEvent ev[2];
    ev[0] = mkEvent(dayUtc(2026, 8, 1), dayUtc(2026, 8, 2), "Task A", "uid-a", true);
    ev[1] = mkEvent(dayUtc(2026, 8, 2), dayUtc(2026, 8, 3), "Task B", "uid-b", true);

    TodoTask tasks[2];
    int n = todoExtractTasks(ev, 2, tasks, 2);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("Task A", tasks[0].title);
    TEST_ASSERT_EQUAL_STRING("uid-a", tasks[0].uid);
    TEST_ASSERT_EQUAL_INT64(dayUtc(2026, 8, 1), tasks[0].dayUtc);
    TEST_ASSERT_EQUAL_STRING("Task B", tasks[1].title);
    TEST_ASSERT_EQUAL_INT64(dayUtc(2026, 8, 2), tasks[1].dayUtc);
}

void test_extract_bounds_to_max(void) {
    CalendarEvent ev[40];
    for (int i = 0; i < 40; i++) {
        char title[16]; snprintf(title, sizeof(title), "T%d", i);
        char uid[16];   snprintf(uid, sizeof(uid), "u%d", i);
        ev[i] = mkEvent(dayUtc(2026, 8, 1) + i, dayUtc(2026, 8, 1) + i + 1,
                        title, uid, true);
    }
    TodoTask tasks[40];
    // Caller capacity clamps first...
    TEST_ASSERT_EQUAL_INT(5, todoExtractTasks(ev, 40, tasks, 5));
    // ...and the global TODO_MAX_TASKS cap clamps even an oversized request.
    TEST_ASSERT_EQUAL_INT(TODO_MAX_TASKS, todoExtractTasks(ev, 40, tasks, 40));
}

void test_extract_null_and_zero_args(void) {
    CalendarEvent ev[1] = { mkEvent(1, 2, "X", "u", true) };
    TodoTask tasks[1];
    TEST_ASSERT_EQUAL_INT(0, todoExtractTasks(nullptr, 1, tasks, 1));
    TEST_ASSERT_EQUAL_INT(0, todoExtractTasks(ev, 1, nullptr, 1));
    TEST_ASSERT_EQUAL_INT(0, todoExtractTasks(ev, 0, tasks, 1));
    TEST_ASSERT_EQUAL_INT(0, todoExtractTasks(ev, 1, tasks, 0));
}

void test_extract_title_and_uid_truncated_safe(void) {
    // Full-length (buffer-filling) title + UID copy safely, always NUL-term.
    CalendarEvent ev;
    calEventClear(ev);
    ev.startUtc = 1000; ev.endUtc = 2000; ev.allDay = true;
    memset(ev.title, 't', CAL_TITLE_MAX - 1); ev.title[CAL_TITLE_MAX - 1] = '\0';
    memset(ev.uid,   'u', CAL_UID_MAX - 1);   ev.uid[CAL_UID_MAX - 1]   = '\0';

    TodoTask tasks[1];
    int n = todoExtractTasks(&ev, 1, tasks, 1);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(TODO_TITLE_MAX - 1, (int)strlen(tasks[0].title));
    TEST_ASSERT_EQUAL_INT(TODO_UID_MAX - 1, (int)strlen(tasks[0].uid));
}

// ===========================================================================
//  Stable done-state identity (todoMakeKey)
// ===========================================================================
void test_key_prefers_uid(void) {
    TodoTask t;
    todoTaskClear(t);
    strcpy(t.title, "Buy milk");
    strcpy(t.uid, "task-1@google.com");
    t.dayUtc = dayUtc(2026, 8, 10);

    char key[TODO_UID_MAX];
    todoMakeKey(t, key, sizeof(key));
    TEST_ASSERT_EQUAL_STRING("task-1@google.com", key);   // UID wins over title+date
}

void test_key_fallback_synthesized(void) {
    // No UID (non-Google feed): the key falls back to "d<dayUtc>#<title>" —
    // the (title + date) identity the spec allows, stable while both hold.
    TodoTask t;
    todoTaskClear(t);
    strcpy(t.title, "Sweep floor");
    t.dayUtc = INT64_C(1786636800);

    char key[TODO_UID_MAX];
    todoMakeKey(t, key, sizeof(key));
    TEST_ASSERT_EQUAL_STRING("d1786636800#Sweep floor", key);

    char key2[TODO_UID_MAX];
    todoMakeKey(t, key2, sizeof(key2));
    TEST_ASSERT_EQUAL_STRING(key, key2);                  // deterministic
}

void test_key_fallback_stable_and_bounded(void) {
    // Over-long title with no UID: snprintf truncates the synthesized key
    // safely; two tasks with the same (truncated) prefix + date still share
    // a key, and the result is always NUL-terminated within the buffer.
    TodoTask t;
    todoTaskClear(t);
    memset(t.title, 'x', TODO_TITLE_MAX - 1);
    t.title[TODO_TITLE_MAX - 1] = '\0';
    t.dayUtc = 12345;

    char key[TODO_UID_MAX];
    todoMakeKey(t, key, sizeof(key));
    TEST_ASSERT_TRUE(strlen(key) < (size_t)TODO_UID_MAX);
    TEST_ASSERT_TRUE(strncmp(key, "d12345#", 7) == 0);
}

// ===========================================================================
//  Done-set operations
// ===========================================================================
void test_merge_flags_done(void) {
    TodoTask tasks[3];
    todoTaskClear(tasks[0]); todoTaskClear(tasks[1]); todoTaskClear(tasks[2]);
    strcpy(tasks[0].uid, "a"); strcpy(tasks[0].title, "A");
    strcpy(tasks[1].uid, "b"); strcpy(tasks[1].title, "B");
    strcpy(tasks[2].uid, "c"); strcpy(tasks[2].title, "C");

    TodoDoneSet set;
    todoDoneClear(set);
    TEST_ASSERT_TRUE(todoDoneToggle(set, "a"));
    TEST_ASSERT_TRUE(todoDoneToggle(set, "c"));

    todoMergeDone(tasks, 3, set);
    TEST_ASSERT_TRUE(tasks[0].done);
    TEST_ASSERT_FALSE(tasks[1].done);
    TEST_ASSERT_TRUE(tasks[2].done);
}

void test_toggle_add_then_remove(void) {
    TodoDoneSet set;
    todoDoneClear(set);
    TEST_ASSERT_TRUE(todoDoneToggle(set, "k1"));
    TEST_ASSERT_EQUAL_INT(1, set.count);
    TEST_ASSERT_TRUE(todoDoneContains(set, "k1"));

    TEST_ASSERT_TRUE(todoDoneToggle(set, "k2"));
    TEST_ASSERT_EQUAL_INT(2, set.count);

    TEST_ASSERT_TRUE(todoDoneToggle(set, "k1"));          // removes
    TEST_ASSERT_EQUAL_INT(1, set.count);
    TEST_ASSERT_FALSE(todoDoneContains(set, "k1"));
    TEST_ASSERT_TRUE(todoDoneContains(set, "k2"));        // gap-fill kept k2
}

void test_toggle_full_set_fails_loudly(void) {
    // NEGATIVE guard: adding to a FULL set must return false and leave the
    // set untouched (the UI surfaces "done list full"; never a silent drop).
    TodoDoneSet set;
    todoDoneClear(set);
    for (int i = 0; i < TODO_DONE_MAX; i++) {
        char k[16]; snprintf(k, sizeof(k), "k%d", i);
        TEST_ASSERT_TRUE(todoDoneToggle(set, k));
    }
    TEST_ASSERT_EQUAL_INT(TODO_DONE_MAX, set.count);

    TEST_ASSERT_FALSE(todoDoneToggle(set, "one-too-many"));
    TEST_ASSERT_EQUAL_INT(TODO_DONE_MAX, set.count);
    TEST_ASSERT_FALSE(todoDoneContains(set, "one-too-many"));

    // Removal still works on a full set...
    TEST_ASSERT_TRUE(todoDoneToggle(set, "k0"));
    TEST_ASSERT_EQUAL_INT(TODO_DONE_MAX - 1, set.count);
    // ...and frees a slot for the previously-rejected key.
    TEST_ASSERT_TRUE(todoDoneToggle(set, "one-too-many"));
    TEST_ASSERT_EQUAL_INT(TODO_DONE_MAX, set.count);
}

void test_toggle_empty_key_rejected(void) {
    TodoDoneSet set;
    todoDoneClear(set);
    TEST_ASSERT_FALSE(todoDoneToggle(set, ""));
    TEST_ASSERT_FALSE(todoDoneToggle(set, nullptr));
    TEST_ASSERT_EQUAL_INT(0, set.count);
    TEST_ASSERT_FALSE(todoDoneContains(set, ""));
}

void test_prune_drops_stale_keys(void) {
    // A task deleted on the phone must not leak its done-key forever.
    TodoTask tasks[2];
    todoTaskClear(tasks[0]); todoTaskClear(tasks[1]);
    strcpy(tasks[0].uid, "a"); strcpy(tasks[0].title, "A");
    strcpy(tasks[1].uid, "c"); strcpy(tasks[1].title, "C");

    TodoDoneSet set;
    todoDoneClear(set);
    todoDoneToggle(set, "a");
    todoDoneToggle(set, "b");        // task B no longer exists -> stale
    todoDoneToggle(set, "c");

    int pruned = todoDonePrune(set, tasks, 2);
    TEST_ASSERT_EQUAL_INT(1, pruned);
    TEST_ASSERT_EQUAL_INT(2, set.count);
    TEST_ASSERT_TRUE(todoDoneContains(set, "a"));
    TEST_ASSERT_TRUE(todoDoneContains(set, "c"));
    TEST_ASSERT_FALSE(todoDoneContains(set, "b"));
}

void test_prune_keeps_fallback_keys(void) {
    // UID-less tasks match by their synthesized (title + date) key.
    TodoTask tasks[1];
    todoTaskClear(tasks[0]);
    strcpy(tasks[0].title, "Sweep floor");
    tasks[0].dayUtc = INT64_C(1786636800);

    char key[TODO_UID_MAX];
    todoMakeKey(tasks[0], key, sizeof(key));

    TodoDoneSet set;
    todoDoneClear(set);
    todoDoneToggle(set, key);
    todoDoneToggle(set, "d999#gone");        // stale fallback key

    int pruned = todoDonePrune(set, tasks, 1);
    TEST_ASSERT_EQUAL_INT(1, pruned);
    TEST_ASSERT_TRUE(todoDoneContains(set, key));

    todoMergeDone(tasks, 1, set);
    TEST_ASSERT_TRUE(tasks[0].done);
}

// ===========================================================================
//  Done-set serialization (the named store seam)
// ===========================================================================
void test_done_roundtrip(void) {
    TodoDoneSet in;
    todoDoneClear(in);
    todoDoneToggle(in, "task-1@google.com");
    todoDoneToggle(in, "d1786636800#Sweep floor");

    std::string json;
    serializeTodoDone(json, in);
    TEST_ASSERT_TRUE(json.find("\"done\":") != std::string::npos);

    TodoDoneSet out;
    int n = deserializeTodoDone(json, out);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_TRUE(todoDoneContains(out, "task-1@google.com"));
    TEST_ASSERT_TRUE(todoDoneContains(out, "d1786636800#Sweep floor"));
}

void test_done_empty_input(void) {
    TodoDoneSet out;
    todoDoneClear(out);
    todoDoneToggle(out, "leftover");         // must be cleared by the load
    TEST_ASSERT_EQUAL_INT(0, deserializeTodoDone("", out));
    TEST_ASSERT_EQUAL_INT(0, out.count);
}

void test_done_garbage_rejected(void) {
    // NEGATIVE: a corrupt store is rejected loudly (empty set), never crashed.
    TodoDoneSet out;
    todoDoneClear(out);
    todoDoneToggle(out, "leftover");
    TEST_ASSERT_EQUAL_INT(0, deserializeTodoDone("this is not json {{{", out));
    TEST_ASSERT_EQUAL_INT(0, out.count);
    TEST_ASSERT_EQUAL_INT(0, deserializeTodoDone("[1,2,3]", out));   // not an object
    TEST_ASSERT_EQUAL_INT(0, out.count);
}

void test_done_truncated_rejected(void) {
    TodoDoneSet in;
    todoDoneClear(in);
    todoDoneToggle(in, "task-1@google.com");
    todoDoneToggle(in, "task-2@google.com");
    std::string json;
    serializeTodoDone(json, in);
    TEST_ASSERT_TRUE(json.size() > 20);

    TodoDoneSet out;
    TEST_ASSERT_EQUAL_INT(0, deserializeTodoDone(json.substr(0, json.size() / 2), out));
    TEST_ASSERT_EQUAL_INT(0, out.count);
}

void test_done_bounds_to_max(void) {
    // Hostile document with more keys than TODO_DONE_MAX: clamp, don't crash.
    std::string json = "{\"v\":1,\"done\":[";
    for (int i = 0; i < TODO_DONE_MAX + 5; i++) {
        char k[24]; snprintf(k, sizeof(k), "%s\"k%d\"", i ? "," : "", i);
        json += k;
    }
    json += "]}";

    TodoDoneSet out;
    int n = deserializeTodoDone(json, out);
    TEST_ASSERT_EQUAL_INT(TODO_DONE_MAX, n);
    TEST_ASSERT_EQUAL_INT(TODO_DONE_MAX, out.count);
}

void test_done_skips_empty_and_nonstring(void) {
    const char *doc = "{\"v\":1,\"done\":[\"\",7,null,\"ok\",[1]]}";
    TodoDoneSet out;
    int n = deserializeTodoDone(doc, out);
    TEST_ASSERT_EQUAL_INT(1, n);             // only "ok" survives
    TEST_ASSERT_TRUE(todoDoneContains(out, "ok"));
}

// ===========================================================================
//  Full cache serialization (/todo.json document)
// ===========================================================================
void test_cache_roundtrip(void) {
    TodoTask in[2];
    todoTaskClear(in[0]); todoTaskClear(in[1]);
    strcpy(in[0].title, "Buy milk");  strcpy(in[0].uid, "task-1@google.com");
    in[0].dayUtc = dayUtc(2026, 8, 10);
    strcpy(in[1].title, "Pay bills"); strcpy(in[1].uid, "task-2@google.com");
    in[1].dayUtc = dayUtc(2026, 8, 11);

    TodoDoneSet done;
    todoDoneClear(done);
    todoDoneToggle(done, "task-2@google.com");

    std::string json;
    serializeTodoCache(json, in, 2, done, INT64_C(1786600000));

    TodoTask out[4];
    TodoDoneSet odone;
    int64_t sync = -1;
    int n = deserializeTodoCache(json, out, 4, &odone, sync);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1786600000), sync);
    TEST_ASSERT_EQUAL_STRING("Buy milk", out[0].title);
    TEST_ASSERT_EQUAL_STRING("task-1@google.com", out[0].uid);
    TEST_ASSERT_EQUAL_INT64(dayUtc(2026, 8, 10), out[0].dayUtc);
    TEST_ASSERT_FALSE(out[0].done);          // merge fills it, not the load
    TEST_ASSERT_EQUAL_STRING("Pay bills", out[1].title);
    TEST_ASSERT_EQUAL_INT(1, odone.count);
    TEST_ASSERT_TRUE(todoDoneContains(odone, "task-2@google.com"));
}

void test_cache_garbage_rejected(void) {
    TodoTask out[2];
    TodoDoneSet done;
    todoDoneClear(done);
    todoDoneToggle(done, "leftover");
    int64_t sync = 42;
    int n = deserializeTodoCache("{{{ not json", out, 2, &done, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT64(0, sync);
    TEST_ASSERT_EQUAL_INT(0, done.count);    // cleared on reject
}

void test_cache_missing_arrays_keep_sync(void) {
    // A valid document with no arrays is an empty cache, NOT corruption.
    TodoTask out[2];
    TodoDoneSet done;
    int64_t sync = 0;
    int n = deserializeTodoCache("{\"v\":1,\"sync\":12345}", out, 2, &done, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT64(12345, sync);
    TEST_ASSERT_EQUAL_INT(0, done.count);
}

void test_cache_null_args_and_done_only_load(void) {
    TodoTask in[1];
    todoTaskClear(in[0]);
    strcpy(in[0].title, "X"); strcpy(in[0].uid, "ux");
    in[0].dayUtc = 555;
    TodoDoneSet done;
    todoDoneClear(done);
    todoDoneToggle(done, "ux");

    std::string json;
    serializeTodoCache(json, in, 1, done, 77);

    // tasks == nullptr: load ONLY the done-set + sync (the sync session does
    // this to recover the done-keys before pruning).
    TodoDoneSet odone;
    int64_t sync = 0;
    int n = deserializeTodoCache(json, nullptr, 0, &odone, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT64(77, sync);
    TEST_ASSERT_EQUAL_INT(1, odone.count);

    // Null / zero arguments are rejected cleanly, never crashed on.
    TEST_ASSERT_EQUAL_INT(0, deserializeTodoCache("{}", nullptr, 4, nullptr, sync));
    TodoTask out[1];
    TEST_ASSERT_EQUAL_INT(0, deserializeTodoCache("{}", out, 0, nullptr, sync));

    // serialize with null tasks -> empty task list, done + sync kept.
    std::string j2 = "sentinel";
    serializeTodoCache(j2, nullptr, 3, done, 42);
    TEST_ASSERT_TRUE(j2.find("\"tasks\":[]") != std::string::npos);
    TEST_ASSERT_TRUE(j2.find("\"sync\":42") != std::string::npos);
    TEST_ASSERT_TRUE(j2.find("\"done\":[\"ux\"]") != std::string::npos);
}

void test_cache_bounds_to_max(void) {
    TodoTask in[TODO_MAX_TASKS + 4];
    for (int i = 0; i < TODO_MAX_TASKS + 4; i++) {
        todoTaskClear(in[i]);
        snprintf(in[i].title, TODO_TITLE_MAX, "T%d", i);
        snprintf(in[i].uid, TODO_UID_MAX, "u%d", i);
        in[i].dayUtc = 1000 + i;
    }
    TodoDoneSet done;
    todoDoneClear(done);

    std::string json;
    serializeTodoCache(json, in, TODO_MAX_TASKS + 4, done, 0);   // clamped on save

    TodoTask out[TODO_MAX_TASKS + 4];
    int64_t sync = 0;
    int n = deserializeTodoCache(json, out, TODO_MAX_TASKS + 4, nullptr, sync);
    TEST_ASSERT_EQUAL_INT(TODO_MAX_TASKS, n);                    // capped

    n = deserializeTodoCache(json, out, 3, nullptr, sync);
    TEST_ASSERT_EQUAL_INT(3, n);                                 // caller capacity
}

void test_cache_skips_phantom_task_elements(void) {
    // NEGATIVE (TODO·R2 regression): a corrupt / hand-edited / adversarial
    // cache may carry non-object or field-less tasks[] entries. These must
    // NOT materialise as phantom "(untitled)" tasks — deserializeTodoCache
    // skips any element that yields neither a title nor a UID, mirroring how
    // CalendarStore drops events without a mandatory start and how the done
    // loop skips empty / non-string keys. Fails without the guard (n == 6).
    const char *doc =
        "{\"v\":1,\"sync\":99,\"tasks\":["
        "1,null,\"x\",{},{\"d\":5},"          // junk -> all phantom
        "{\"t\":\"Real task\",\"u\":\"u1\",\"d\":123}]}";
    TodoTask out[TODO_MAX_TASKS];
    TodoDoneSet done;
    int64_t sync = 0;
    int n = deserializeTodoCache(doc, out, TODO_MAX_TASKS, &done, sync);
    TEST_ASSERT_EQUAL_INT(1, n);                 // only the real task survives
    TEST_ASSERT_EQUAL_STRING("Real task", out[0].title);
    TEST_ASSERT_EQUAL_STRING("u1", out[0].uid);
    TEST_ASSERT_EQUAL_INT64(123, out[0].dayUtc);
    TEST_ASSERT_EQUAL_INT64(99, sync);           // sync kept despite junk tasks
}

// ===========================================================================
//  End-to-end pipeline pins (the identity design decision in action)
// ===========================================================================
void test_pipeline_resync_keeps_done_across_rename(void) {
    // THE UID pin: a task renamed on the phone keeps its done flag across a
    // re-sync, because done-state matches by UID, not by title.
    const char *icsV1 =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20260810\r\nSUMMARY:Buy milk\r\n"
        "UID:t1@google.com\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20260811\r\nSUMMARY:Pay bills\r\n"
        "UID:t2@google.com\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    CalendarEvent ev[4];
    int parsed = parseIcsFeed(icsV1, 0, ev, 4, CAL_TZ_OFFSET_SEC);
    TodoTask tasks[TODO_MAX_TASKS];
    int n = todoExtractTasks(ev, parsed, tasks, TODO_MAX_TASKS);
    TEST_ASSERT_EQUAL_INT(2, n);

    // User marks "Buy milk" done; persist the done-set.
    char key[TODO_UID_MAX];
    todoMakeKey(tasks[0], key, sizeof(key));
    TodoDoneSet done;
    todoDoneClear(done);
    TEST_ASSERT_TRUE(todoDoneToggle(done, key));
    std::string json;
    serializeTodoDone(json, done);

    // Phone-side: the task is RENAMED ("Buy oat milk"), same UID; re-sync.
    const char *icsV2 =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20260810\r\nSUMMARY:Buy oat milk\r\n"
        "UID:t1@google.com\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20260811\r\nSUMMARY:Pay bills\r\n"
        "UID:t2@google.com\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    parsed = parseIcsFeed(icsV2, 0, ev, 4, CAL_TZ_OFFSET_SEC);
    n = todoExtractTasks(ev, parsed, tasks, TODO_MAX_TASKS);
    TEST_ASSERT_EQUAL_INT(2, n);

    TodoDoneSet loaded;
    TEST_ASSERT_EQUAL_INT(1, deserializeTodoDone(json, loaded));
    todoDonePrune(loaded, tasks, n);         // nothing stale: both UIDs live
    todoMergeDone(tasks, n, loaded);

    TEST_ASSERT_EQUAL_STRING("Buy oat milk", tasks[0].title);
    TEST_ASSERT_TRUE(tasks[0].done);         // done state SURVIVED the rename
    TEST_ASSERT_FALSE(tasks[1].done);
}

void test_pipeline_deleted_task_pruned(void) {
    // A task deleted on the phone has its stale done-key pruned on re-sync.
    TodoTask tasks[3];
    for (int i = 0; i < 3; i++) {
        todoTaskClear(tasks[i]);
        snprintf(tasks[i].uid, TODO_UID_MAX, "t%d", i + 1);
        tasks[i].dayUtc = 1000 + i;
    }
    TodoDoneSet done;
    todoDoneClear(done);
    todoDoneToggle(done, "t1");
    todoDoneToggle(done, "t2");
    todoDoneToggle(done, "t3");

    // Re-sync: task t2 is gone from the calendar.
    TodoTask after[2] = { tasks[0], tasks[2] };
    int pruned = todoDonePrune(done, after, 2);
    TEST_ASSERT_EQUAL_INT(1, pruned);
    TEST_ASSERT_EQUAL_INT(2, done.count);

    todoMergeDone(after, 2, done);
    TEST_ASSERT_TRUE(after[0].done);
    TEST_ASSERT_TRUE(after[1].done);
}

// ===========================================================================
int main(int, char **) {
    UNITY_BEGIN();
    // Parser UID capture pins
    RUN_TEST(test_parser_captures_uid);
    RUN_TEST(test_parser_uid_truncated_safely);
    RUN_TEST(test_parser_missing_uid_is_empty);
    // Task extraction
    RUN_TEST(test_extract_all_day_only);
    RUN_TEST(test_extract_copies_fields);
    RUN_TEST(test_extract_bounds_to_max);
    RUN_TEST(test_extract_null_and_zero_args);
    RUN_TEST(test_extract_title_and_uid_truncated_safe);
    // Stable identity
    RUN_TEST(test_key_prefers_uid);
    RUN_TEST(test_key_fallback_synthesized);
    RUN_TEST(test_key_fallback_stable_and_bounded);
    // Done-set operations
    RUN_TEST(test_merge_flags_done);
    RUN_TEST(test_toggle_add_then_remove);
    RUN_TEST(test_toggle_full_set_fails_loudly);
    RUN_TEST(test_toggle_empty_key_rejected);
    RUN_TEST(test_prune_drops_stale_keys);
    RUN_TEST(test_prune_keeps_fallback_keys);
    // Done-set serialization
    RUN_TEST(test_done_roundtrip);
    RUN_TEST(test_done_empty_input);
    RUN_TEST(test_done_garbage_rejected);
    RUN_TEST(test_done_truncated_rejected);
    RUN_TEST(test_done_bounds_to_max);
    RUN_TEST(test_done_skips_empty_and_nonstring);
    // Full cache serialization
    RUN_TEST(test_cache_roundtrip);
    RUN_TEST(test_cache_garbage_rejected);
    RUN_TEST(test_cache_missing_arrays_keep_sync);
    RUN_TEST(test_cache_null_args_and_done_only_load);
    RUN_TEST(test_cache_bounds_to_max);
    RUN_TEST(test_cache_skips_phantom_task_elements);
    // End-to-end pipeline pins
    RUN_TEST(test_pipeline_resync_keeps_done_across_rename);
    RUN_TEST(test_pipeline_deleted_task_pruned);
    return UNITY_END();
}
