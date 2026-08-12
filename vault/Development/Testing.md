---
title: Testing
tags: [development, testing, native, unity]
status: reference
---

# Testing

> Native Unity test suite — pure logic, no hardware.

## Running Tests

```powershell
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
python -m platformio test -e native
```

## Test Suites

| Suite | Tests | Covers |
|---|---|---|
| `test_pagination` | — | Paginator, PageLayout |
| `test_wordwrap` | — | Text wrapping/clipping |
| `test_calendar` | — | IcsParser, CalendarDate, CalendarEvent |
| `test_calendar_store` | — | CalendarStore JSON cache |
| `test_weather` | — | OpenMeteo parsing |
| `test_qr` | — | Emvco, QrPayload |
| `test_todo` | 31 | TodoModel extraction + done-state |
| `test_agenda` | — | AgendaMerge timeline merge |
| `test_format` | — | Format helpers |
| `test_bookmark` | — | BookmarkStore |
| `test_validation` | — | PathValidation |
| `test_sync_schedule` | — | SyncSchedule |
| `test_voicejournal` | — | VoiceModel |
| `test_devcompanion` | — | RefsIndex, GithubModel |
| `test_app_framework` | — | App base, AppManager |
| `test_bookmark` | — | BookmarkManager |

## Design Rules

1. **Hardware-free** — no Arduino, WiFi, filesystem, or EPD
2. **Deterministic** — same input → same output, no timing dependencies
3. **Host-compiled** — tests compile on the dev machine (GCC/Clang/MinGW-w64)
4. **Core seams only** — tests include `src/core/*.h` directly
5. **Stubs/fakes** — inject measurement and storage boundaries

## Total: 122+ passing tests

## See Also
- [[Architecture/Core Logic Seams]]
- [[Development/Build & Flash]]
