#pragma once
// ===========================================================================
//  core/TodoModel.h  —  Todo task extraction + device-local done-state (seam)
// ===========================================================================
//  Header-only, heap-free pure logic for the Todo app (TODO·R1), host-tested
//  under `pio test -e native`. No HAL, no Arduino.h, no heap: every capacity
//  is a fixed static buffer sized by the TODO_* / CAL_* constants in config.h.
//
//  Model (locked spec, docs/PROJECT_BRIEF.md §2.3 Feature 3):
//    - Tasks live in a dedicated "Tasks" Google Calendar; each task is an
//      ALL-DAY event (title = task text). The firmware fetches the calendar's
//      ICS via the EXISTING calendar mechanism (zero new auth) and hands the
//      parsed CalendarEvent[] to todoExtractTasks(), which keeps the all-day
//      events only — timed events are NOT tasks and are skipped.
//    - Done-state is DEVICE-LOCAL: it lives in a bounded TodoDoneSet, is
//      persisted at /todo.json (src/apps/todo/TodoStore), and is NEVER pushed
//      back to Google. The user edits tasks on the phone; the device only
//      records "done" locally.
//
//  Done-state identity (DESIGN DECISION — TODO·R1):
//    Done-keys are the event's ICS UID, captured by core::parseIcsFeed into
//    CalendarEvent::uid (TODO·R1 extended the shared parser for this; the
//    baseline calendar/weather/qr tests stay green — the field is additive).
//    Google keeps the UID stable across phone-side edits and re-syncs, so a
//    task renamed on the phone keeps its done flag — matching by (title +
//    date) would have lost it. FALLBACK: an event with no UID (non-Google
//    feeds) gets a synthesized key "d<dayUtc>#<title>" — stable while the
//    title + date are unchanged, which is exactly the (title + date) identity
//    the spec allows as the safe fallback. todoMakeKey() encapsulates the
//    choice so the merge / prune / store code never cares which kind a key is.
//
//  Store seam (mirrors CalendarStore's pattern): serializeTodoDone /
//  deserializeTodoDone operate on a std::string standing in for the file, so
//  they are host-testable (ArduinoJson 7 is header-only and identical on the
//  host). serializeTodoCache / deserializeTodoCache add the task list +
//  lastSyncUtc around the same done array — that full document is what
//  /todo.json holds (and what the later Agenda feature reads). Deserialization
//  is tolerant by contract: empty / corrupt / truncated input yields an empty
//  set (0 tasks), never a crash.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <ArduinoJson.h>
#include "core/CalendarEvent.h"   // pulls in config.h (CAL_* / TODO_* constants)

namespace core {

// Largest cache file we bother reading; a full cache (TODO_MAX_TASKS tasks
// with 64-char titles + TODO_DONE_MAX 96-char keys) is ~12 KB, so anything
// larger is treated as corrupt by the firmware store wrapper.
static constexpr size_t TODO_CACHE_MAX_BYTES = 32768;

// --- Data types (bounded, plain-old-data) -----------------------------------

// One todo task (an all-day event extracted from the Tasks calendar).
struct TodoTask {
    char    title[TODO_TITLE_MAX];   // NUL-terminated task text (ICS escapes decoded)
    char    uid[TODO_UID_MAX];       // NUL-terminated ICS UID ("" when the feed
                                     // omits it — todoMakeKey then synthesizes one)
    int64_t dayUtc;                  // all-day date: local-midnight UTC epoch seconds
    bool    done;                    // filled by todoMergeDone(); not persisted here
                                     // (the done-set is the source of truth)
};

// Device-local set of done-keys. Bounded to TODO_DONE_MAX keys of
// TODO_UID_MAX chars each; stale keys are pruned on every sync so the set
// tracks the live task list (todoDonePrune).
struct TodoDoneSet {
    char keys[TODO_DONE_MAX][TODO_UID_MAX];
    int  count;                      // 0..TODO_DONE_MAX
};

// Reset a task to safe defaults so partially-filled fields are never garbage.
inline void todoTaskClear(TodoTask &t) {
    t.title[0] = '\0';
    t.uid[0]   = '\0';
    t.dayUtc   = 0;
    t.done     = false;
}

// Reset a done-set to empty.
inline void todoDoneClear(TodoDoneSet &s) {
    s.count = 0;
}

// --- Task extraction ---------------------------------------------------------
// Extract the tasks from a parsed ICS event list: ALL-DAY events only
// (allDay == true); timed events are skipped — they are calendar events, not
// tasks. Copies title + UID + the all-day date into a bounded TodoTask[] in
// feed order; `done` starts false (call todoMergeDone() next). Returns the
// number written (<= maxOut, <= TODO_MAX_TASKS). NULL / non-positive arguments
// yield 0 (never a crash). Recurring all-day masters (freq != None) count as
// one task each: a recurring task is still one checklist entry.
inline int todoExtractTasks(const CalendarEvent *events, int n,
                            TodoTask *out, int maxOut) {
    if (!events || !out || n <= 0 || maxOut <= 0) return 0;
    if (maxOut > TODO_MAX_TASKS) maxOut = TODO_MAX_TASKS;

    int written = 0;
    for (int i = 0; i < n && written < maxOut; ++i) {
        const CalendarEvent &e = events[i];
        if (!e.allDay) continue;                 // timed event -> not a task

        TodoTask t;
        todoTaskClear(t);
        strncpy(t.title, e.title, TODO_TITLE_MAX - 1);
        t.title[TODO_TITLE_MAX - 1] = '\0';      // always NUL-terminated
        strncpy(t.uid, e.uid, TODO_UID_MAX - 1);
        t.uid[TODO_UID_MAX - 1] = '\0';
        t.dayUtc = e.startUtc;                   // all-day start == local midnight
        out[written++] = t;
    }
    return written;
}

// --- Stable done-state identity ----------------------------------------------
// Write the task's done-key to out (capacity cap incl. NUL): the ICS UID when
// present (stable across phone-side re-syncs — the preferred identity), else a
// synthesized "d<dayUtc>#<title>" key (the (title + date) fallback; stable
// while neither changes). Always NUL-terminates; snprintf truncates over-long
// fallback keys safely. The merge / prune / store code matches keys by plain
// string compare, so both kinds flow through the same path.
inline void todoMakeKey(const TodoTask &t, char *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (t.uid[0] != '\0') {
        strncpy(out, t.uid, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    // Fallback identity: the all-day date + title. snprintf bounds the write;
    // an over-long title simply yields a truncated (but still stable) key.
    snprintf(out, cap, "d%lld#%s", (long long)t.dayUtc, t.title);
}

// --- Done-set operations ------------------------------------------------------
// True iff `key` is in the set. NULL / empty keys never match.
inline bool todoDoneContains(const TodoDoneSet &s, const char *key) {
    if (!key || key[0] == '\0') return false;
    for (int i = 0; i < s.count; ++i) {
        if (strncmp(s.keys[i], key, TODO_UID_MAX) == 0) return true;
    }
    return false;
}

// Toggle `key`: remove it when present, add it when absent. Returns true when
// the change was applied. GUARD (fails loudly): adding to a FULL set returns
// false and leaves the set untouched — the caller surfaces "done list full"
// rather than silently dropping the user's toggle. NULL / empty keys are
// rejected the same way.
inline bool todoDoneToggle(TodoDoneSet &s, const char *key) {
    if (!key || key[0] == '\0') return false;
    for (int i = 0; i < s.count; ++i) {
        if (strncmp(s.keys[i], key, TODO_UID_MAX) == 0) {
            // Remove: move the last key into the gap (order is irrelevant).
            --s.count;
            if (i != s.count) {
                strncpy(s.keys[i], s.keys[s.count], TODO_UID_MAX - 1);
                s.keys[i][TODO_UID_MAX - 1] = '\0';
            }
            return true;
        }
    }
    if (s.count >= TODO_DONE_MAX) return false;  // full: fail loudly (see header)
    strncpy(s.keys[s.count], key, TODO_UID_MAX - 1);
    s.keys[s.count][TODO_UID_MAX - 1] = '\0';
    ++s.count;
    return true;
}

// Flag each task's `done` from the done-set (by todoMakeKey identity).
inline void todoMergeDone(TodoTask *tasks, int n, const TodoDoneSet &s) {
    if (!tasks || n <= 0) return;
    char key[TODO_UID_MAX];
    for (int i = 0; i < n; ++i) {
        todoMakeKey(tasks[i], key, (int)sizeof(key));
        tasks[i].done = todoDoneContains(s, key);
    }
}

// Prune done-keys that no longer match ANY task, so a task deleted on the
// phone does not leak stale done-state forever. Returns the number of keys
// pruned. Matching uses the same todoMakeKey identity as the merge, so both
// UID keys and synthesized fallback keys are handled.
inline int todoDonePrune(TodoDoneSet &s, const TodoTask *tasks, int n) {
    int pruned = 0;
    int i = 0;
    while (i < s.count) {
        bool matched = false;
        char key[TODO_UID_MAX];
        for (int j = 0; j < n && !matched; ++j) {
            todoMakeKey(tasks[j], key, (int)sizeof(key));
            if (strncmp(s.keys[i], key, TODO_UID_MAX) == 0) matched = true;
        }
        if (matched) { ++i; continue; }
        // Drop keys[i]: move the last key into the gap, re-test slot i.
        --s.count;
        if (i != s.count) {
            strncpy(s.keys[i], s.keys[s.count], TODO_UID_MAX - 1);
            s.keys[i][TODO_UID_MAX - 1] = '\0';
        }
        ++pruned;
    }
    return pruned;
}

// --- Pure serialization seam (host-testable) ----------------------------------
// Serialize the done-set alone: {"v":1,"done":["key",...]}. Bounded by
// construction (TODO_DONE_MAX keys). Never throws.
inline void serializeTodoDone(std::string &out, const TodoDoneSet &s) {
    JsonDocument doc;
    doc["v"] = 1;
    JsonArray arr = doc["done"].to<JsonArray>();
    for (int i = 0; i < s.count; ++i) arr.add(s.keys[i]);
    out.clear();
    serializeJson(doc, out);
}

// Deserialize a done-set document. Tolerant by design (the load() contract):
//   - empty / unparseable / non-object input -> empty set (corrupt rejected)
//   - valid object but missing "done" array  -> empty set
//   - non-string / empty entries are skipped; count is bounded to TODO_DONE_MAX
// Returns the number of keys loaded. Never crashes on hostile input.
inline int deserializeTodoDone(const std::string &in, TodoDoneSet &s) {
    todoDoneClear(s);
    if (in.empty()) return 0;

    JsonDocument doc;
    if (deserializeJson(doc, in)) return 0;      // corrupt / truncated -> empty
    if (!doc.is<JsonObject>()) return 0;

    JsonArray arr = doc["done"].as<JsonArray>();
    if (arr.isNull()) return 0;                  // valid doc, no done array

    for (JsonVariant v : arr) {
        if (s.count >= TODO_DONE_MAX) break;     // bound to capacity
        const char *k = v | "";                  // non-string -> "" -> skipped
        if (k[0] == '\0') continue;
        strncpy(s.keys[s.count], k, TODO_UID_MAX - 1);
        s.keys[s.count][TODO_UID_MAX - 1] = '\0';
        ++s.count;
    }
    return s.count;
}

// Serialize the FULL /todo.json cache: schema v1, compact keys (mirrors
// CalendarStore's format conventions):
//   { "v": 1, "sync": lastSyncUtc,
//     "tasks": [ { "u": uid, "t": title, "d": dayUtc }, ... ],
//     "done":  [ "key", ... ] }
// Inputs are bounded: a null task pointer or negative count yields an empty
// task list; n is clamped to TODO_MAX_TASKS. Never throws.
inline void serializeTodoCache(std::string &out, const TodoTask *tasks, int n,
                               const TodoDoneSet &done, int64_t lastSyncUtc) {
    if (!tasks || n < 0) n = 0;
    if (n > TODO_MAX_TASKS) n = TODO_MAX_TASKS;

    JsonDocument doc;
    doc["v"]    = 1;
    doc["sync"] = lastSyncUtc;
    JsonArray ts = doc["tasks"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        const TodoTask &t = tasks[i];
        JsonObject o = ts.add<JsonObject>();
        o["u"] = t.uid;
        o["t"] = t.title;
        o["d"] = t.dayUtc;
    }
    JsonArray ds = doc["done"].to<JsonArray>();
    for (int i = 0; i < done.count; ++i) ds.add(done.keys[i]);

    out.clear();
    serializeJson(doc, out);
}

// Deserialize the full cache into tasks[] (<= max) and (optionally) the
// done-set + lastSyncUtc. Returns the number of tasks written. Tolerant by
// design — exactly the deserializeTodoDone contract, extended:
//   - empty / unparseable / non-object input -> 0 tasks, empty done, sync = 0
//   - valid object but missing "tasks"       -> 0 tasks (sync + done kept)
//   - per-task fields are sanitised (bounded copies, always NUL-terminated)
// Pass tasks == nullptr / max <= 0 to load ONLY the done-set + sync (the sync
// session does this to recover the done-keys before pruning).
inline int deserializeTodoCache(const std::string &in, TodoTask *tasks, int max,
                                TodoDoneSet *done, int64_t &lastSyncUtc) {
    lastSyncUtc = 0;
    if (done) todoDoneClear(*done);
    if (!tasks || max <= 0) { tasks = nullptr; max = 0; }
    if (in.empty()) return 0;

    JsonDocument doc;
    if (deserializeJson(doc, in)) return 0;      // corrupt / truncated -> empty
    if (!doc.is<JsonObject>()) return 0;

    lastSyncUtc = doc["sync"] | (int64_t)0;      // valid doc: keep the timestamp

    int n = 0;
    JsonArray ts = doc["tasks"].as<JsonArray>();
    if (!ts.isNull()) {
        for (JsonObject o : ts) {
            if (tasks && n >= max) break;        // bound to caller capacity
            TodoTask t;
            todoTaskClear(t);
            const char *u = o["u"] | "";
            strncpy(t.uid, u, TODO_UID_MAX - 1);
            t.uid[TODO_UID_MAX - 1] = '\0';
            const char *tt = o["t"] | "";
            strncpy(t.title, tt, TODO_TITLE_MAX - 1);
            t.title[TODO_TITLE_MAX - 1] = '\0';
            t.dayUtc = o["d"] | (int64_t)0;
            t.done   = false;                    // merge fills it from the done-set
            if (tasks) tasks[n] = t;
            ++n;
        }
    }
    if (!tasks) n = 0;                           // done-only load reports 0 tasks

    if (done) {
        JsonArray ds = doc["done"].as<JsonArray>();
        if (!ds.isNull()) {
            for (JsonVariant v : ds) {
                if (done->count >= TODO_DONE_MAX) break;
                const char *k = v | "";
                if (k[0] == '\0') continue;
                strncpy(done->keys[done->count], k, TODO_UID_MAX - 1);
                done->keys[done->count][TODO_UID_MAX - 1] = '\0';
                ++done->count;
            }
        }
    }
    return n;
}

} // namespace core
