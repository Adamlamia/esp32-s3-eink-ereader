---
title: Todo App
tags: [feature, app, sync, deferred]
status: deferred
batch: "1"
---

# Todo

> Tasks via Google Calendar ICS (zero new auth). **DEFERRED.**

## Status: ⏸️ Deferred (2026-08-02)

Built, tested (31 native tests), and merged to `main` (82fd48a), but **commented out of the launcher** (`AppRegistry.h`, marker `TODO(TODO-BACKEND)`).

**Reason:** Google Tasks (the product) exposes NO ICS/CalDAV feed — only an OAuth2 JSON API. The "Tasks Google Calendar (all-day events)" source only suits date-bounded tasks, and the owner's tasks are mostly **undated**.

**To re-enable:** Uncomment the two `TodoApp` lines in `AppRegistry.h`.

**Backend options to revisit:**
- **Option C:** Self-hosted/3rd-party ICS bridge for undated tasks
- **Option B:** Tasks API via OAuth2 (breaks zero-auth decision)

## Overview

Tasks = all-day events in a dedicated Google Calendar. Done-state stored locally on SD (never pushed back). Phone-editable via Google Calendar app.

## Key Files

| File | Role |
|---|---|
| `src/apps/todo/TodoApp.cpp` | App lifecycle + UI |
| `src/apps/todo/TodoStore.cpp` | JSON cache + done-state |
| `src/apps/todo/TodoSync.cpp` | ICS fetch + RAII WiFi |
| `src/core/TodoModel.h` | Task extraction + done-state merge (core seam) |

## Configuration

| Define | Value | Description |
|---|---|---|
| `TODO_CACHE_FILE` | `/todo.json` | Cache path |
| `TODO_MAX_TASKS` | 32 | Max tasks in memory |
| `TODO_MAX_EVENTS` | 48 | Parsed ICS events per sync |
| `TODO_DONE_MAX` | 48 | Max done-keys persisted |
| `TODO_STALE_SEC` | 6h | On-open resync threshold |
| `TODO_MIN_BATTERY_FOR_SYNC` | 15% | Battery floor (auto only) |

## Tests

31 native Unity tests covering `TodoModel` task extraction and done-state merge.

## See Also
- [[Features/Agenda]] — merges calendar + todo caches
- [[Features/Calendar]] — shared ICS mechanism
