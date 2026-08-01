#pragma once
// ===========================================================================
//  TodoStore  —  /todo.json cache persistence (TODO·R1)
// ===========================================================================
//  Persists the Todo cache (task list + device-local done-set + lastSyncUtc)
//  as a small JSON document at a fixed path on the active filesystem (SD or
//  LittleFS, via BookStorage::fs()). The task list is the materialised view
//  of the "Tasks" Google Calendar (all-day events only); the done-set is
//  DEVICE-LOCAL state, never pushed back to Google. The later Agenda feature
//  reads this same cache (/todo.json) to build today's timeline.
//
//  Cache format: schema v1 lives in core/TodoModel.h (serializeTodoCache /
//  deserializeTodoCache) — compact keys {v, sync, tasks:[{u,t,d}], done:[]}.
//
//  Robustness contract (mirrors CalendarStore): load() MUST tolerate a
//  missing, truncated, oversized or corrupt file — it returns 0 tasks (and
//  an empty done-set, lastSyncUtc = 0) rather than crashing. A power loss
//  mid-save can corrupt the file; the next load treats it as empty and the
//  next "Sync now" rewrites it.
//
//  Structure: the pure serialize / deserialize logic lives in namespace core
//  (core/TodoModel.h) and is host-tested under `pio test -e native`; this
//  header only declares the thin fs::FS wrapper, compiled for the firmware
//  (ARDUINO) alone — exactly the CalendarStore.h / WeatherStore.h pattern.
// ===========================================================================
#include <cstddef>
#include "core/TodoModel.h"   // core seam + config.h TODO_* constants

#ifndef TODO_CACHE_FILE
  #define TODO_CACHE_FILE "/todo.json"   // fixed path on the active FS
#endif

// ===========================================================================
//  Firmware-only fs::FS wrapper (excluded from the native test build)
// ===========================================================================
#ifdef ARDUINO
#include <FS.h>

class TodoStore {
public:
    explicit TodoStore(fs::FS &fs, const char *path = TODO_CACHE_FILE)
        : _fs(fs), _path(path) {}

    // Write the cache. count is bounded to [0, TODO_MAX_TASKS]. Returns true
    // only if every byte was written.
    bool save(const core::TodoTask *tasks, int count,
              const core::TodoDoneSet &done, int64_t lastSyncUtc);

    // Read the cache into tasks[] (<= max) and (optionally) the done-set +
    // lastSyncUtc. Missing / corrupt / oversized files yield 0 tasks + an
    // empty done-set (never a crash). Pass tasks == nullptr / max <= 0 to
    // load ONLY the done-set + sync (the sync session does this to recover
    // the done-keys before pruning). Returns the number of tasks loaded.
    int load(core::TodoTask *tasks, int max,
             core::TodoDoneSet *done = nullptr, int64_t *lastSyncUtc = nullptr);

private:
    fs::FS      &_fs;
    const char  *_path;
};
#endif // ARDUINO
