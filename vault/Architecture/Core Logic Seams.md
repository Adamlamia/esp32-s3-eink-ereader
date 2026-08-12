---
title: Core Logic Seams
tags: [architecture, core, testing]
status: reference
---

# Core Logic Seams

> Header-only, HAL-free pure logic in `src/core/`. All native-testable.

## Design Rules

1. **Header-only** — `#pragma once`, all inline/template
2. **No HAL** — no `Arduino.h`, no filesystem, no WiFi, no display
3. **No heap** — every capacity bounds a fixed static buffer
4. **Namespace `core`** — all seams live in `namespace core`
5. **Delegation-based** — apps delegate complex logic here

## Seam Inventory

### Calendar & Time
| File | Responsibility |
|---|---|
| `IcsParser.h` | Parse ICS feed text → concrete events |
| `CalendarDate.h` | Date math (year/month/day, weekday, days-in-month) |
| `CalendarEvent.h` | Event struct + recurrence expansion |
| `SyncSchedule.h` | Daily sync scheduling, staleness, battery floor |

### Weather
| File | Responsibility |
|---|---|
| `OpenMeteo.h` | Parse Open-Meteo JSON → weather structs |

### QR Toolkit
| File | Responsibility |
|---|---|
| `Emvco.h` | EMVCo TLV encode/decode, CRC validation |
| `QrPayload.h` | QR payload formatting (WiFi, URL, text, DuitNow) |

### Todo
| File | Responsibility |
|---|---|
| `TodoModel.h` | Task extraction from ICS + done-state merge |

### Voice Journal
| File | Responsibility |
|---|---|
| `VoiceModel.h` / `.cpp` | Voice entry model, queue management |

### Agenda
| File | Responsibility |
|---|---|
| `AgendaMerge.h` | Merge calendar + todo caches → ordered timeline |

### Reader / Display
| File | Responsibility |
|---|---|
| `Paginator.h` | Page break calculation |
| `PageLayout.h` | Line/word layout metrics |
| `TextTransform.h` | Text normalization, whitespace handling |
| `Format.h` | Number/date/string formatting helpers |

### System
| File | Responsibility |
|---|---|
| `BatteryMath.h` | ADC → voltage → percentage |
| `ButtonClassify.h` | Hold-duration band classification |
| `BookmarkStore.h` | Bookmark data model |
| `PathValidation.h` | Safe path checks (prevent traversal) |
| `RefsIndex.h` | Reference image index parsing |
| `GithubModel.h` | GitHub API response parsing |

## Testing

All seams are tested via native Unity tests in `test/`:

```
pio test -e native
```

No Arduino, WiFi, filesystem, or EPD is touched. Tests include core headers directly and inject stubs/fakes for measurement and storage boundaries.

## See Also
- [[Architecture/System Architecture]]
- [[Development/Testing]]
