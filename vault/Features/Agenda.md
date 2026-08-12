---
title: Agenda App
tags: [feature, app]
status: planned
batch: "1"
---

# Agenda

> Split-view launcher: app list (left) + today's timeline (right). **Not yet built.**

## Overview

Reworks the launcher into a split view. Left panel = app list. Right panel = today's timeline merging calendar + todo caches, with the next item highlighted.

## Key Design

- **No new network code** — thin merge layer over existing caches
- Reads `/calendar.json` + `/todo.json`
- Merge seam: `src/core/AgendaMerge.h`

## Planned Files

| File | Role |
|---|---|
| `src/core/AgendaMerge.h` | Calendar + todo → ordered timeline (core seam) |
| Launcher rework | Split-view rendering in `AppManager` |

## Configuration

| Define | Value | Description |
|---|---|---|
| `AGENDA_TITLE_MAX` | 48 | Display title buffer |
| `AGENDA_MAX_ITEMS` | 24 | Max timeline items |

## Dependencies

- [[Features/Calendar]] — `/calendar.json` cache
- [[Features/Todo]] — `/todo.json` cache (currently deferred)

> [!warning] Blocked by Todo backend
> Agenda merges calendar + todo. With Todo deferred, the right panel would only show calendar events.

## Position in Build Order

Agenda is **last in batch 1** because it depends on both Calendar and Todo caches being present.

```
Weather → QR Toolkit → Todo → Agenda   ⟵ FULL PAUSE after Agenda
```

## See Also
- [[Architecture/Core Logic Seams]]
- [[Project/Project Overview]]
