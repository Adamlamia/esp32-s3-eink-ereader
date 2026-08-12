---
title: Calendar App
tags: [feature, app, sync]
status: active
batch: "1"
---

# Calendar

> Google Calendar sync via ICS feeds (no OAuth).

## Overview

Fetches up to 4 Google Calendar ICS feeds, materializes events for a 14-day rolling window, caches to `/calendar.json`, and renders a day-view calendar screen.

## Key Files

| File | Role |
|---|---|
| `src/apps/calendar/CalendarApp.cpp` | App lifecycle + UI |
| `src/apps/calendar/CalendarStore.cpp` | JSON cache persistence |
| `src/apps/calendar/CalendarSync.cpp` | NTP + HTTPS fetch + RAII WiFi |
| `src/core/IcsParser.h` | ICS feed parser (core seam) |
| `src/core/CalendarDate.h` | Date math (core seam) |
| `src/core/CalendarEvent.h` | Event struct + recurrence (core seam) |
| `src/core/SyncSchedule.h` | Sync scheduling (core seam) |

## Configuration

| Define | Value | Description |
|---|---|---|
| `CAL_TZ_OFFSET_SEC` | 28800 | UTC+8 (Malaysia, no DST) |
| `CAL_MAX_EVENTS` | 64 | Max events in memory |
| `CAL_TITLE_MAX` | 64 | Event title buffer |
| `CAL_UID_MAX` | 96 | Event UID buffer |
| `CAL_SYNC_HOUR` | 6 | Daily sync at 06:00 local |
| `CAL_SYNC_WINDOW_DAYS` | 14 | Rolling window |
| `CAL_MAX_CALENDARS` | 4 | Max ICS feeds |
| `CAL_CACHE_FILE` | `/calendar.json` | Cache path |
| `CAL_MIN_BATTERY_FOR_SYNC` | 15% | Battery floor for auto sync |
| `CAL_SYNC_STALE_SEC` | 20h | Backstop resync threshold |
| `CAL_WAKE_CAP_SEC` | 6h | Max scheduled wakeup |

## Secrets

ICS URLs live in `src/secrets.h`:
```
#define CAL_ICS_URL_0   "https://calendar.google.com/..."
#define CAL_ICS_LABEL_0 "Work"
```

## Patterns Used

- RAII WiFi via `WifiSession`
- Portal-guarded sync
- NTP clock validation (`CAL_CLOCK_MIN_EPOCH`)
- Scheduled sync via `sleepWakeupSec()` override

## Known Debt

8 `TODO(R2)` items for MONTHLY/YEARLY recurrence expansion.

## Tests

Native Unity tests cover `IcsParser`, `CalendarDate`, `CalendarEvent`, `SyncSchedule`.

## See Also
- [[Features/Agenda]] — merges calendar + todo
- [[Architecture/Core Logic Seams]]
