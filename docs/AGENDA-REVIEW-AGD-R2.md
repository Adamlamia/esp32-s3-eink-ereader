# Agenda Feature — CSPMO Review & Fix (AGD·R2)

Review-and-fix round over the AGD·R1 Agenda (split-view launcher "Today"
panel) implementation on `main`, mirroring the TODO·R2 / QR·R2 / WTH·R2 bar:
**zero Critical open** at the end, every finding either fixed in-code or
ledgered with a disposition. Adversarial but fair — hunted for real logic /
memory / e-ink / observability defects in the merge seam and the launcher
wiring, not style. This round reviews and hardens the Agenda milestone.

**Scope reviewed:** `src/core/AgendaMerge.h` (the pure calendar+todo →
ordered-timeline merge seam), `src/app/AppManager.{h,cpp}` (the split-view
launcher: `drawLauncher` / `refreshAgenda` / `drawAgendaPanel`), the Agenda
block of `src/config.h` (`AGENDA_TITLE_MAX=48`, `AGENDA_MAX_ITEMS=24`), and
`test/test_agenda/test_agenda.cpp`. `CalendarStore.h`, `TodoModel.h`,
`IcsParser.h`, `CalendarEvent.h` treated READ-ONLY per the round contract
(reviewed as dependencies, not modified).

**Baseline (AGD·R1, PM-verified):** 228/228 native tests, firmware SUCCESS.
This round: **229/229** native (228 + 1 AGD-R2 regression), firmware SUCCESS
(RAM 19.7% / Flash 31.7%), degraded-mode (no `src/secrets.h`) green, marker
census unchanged at 16 (no new marker classes).

---

## Verdict

The AgendaMerge seam is **correct and well-pinned**. The window rule, the
stable alphabetical all-day sort (calendar wins cross-source ties), the
strict-future `nextIdx`, the capacity clamp, the null/zero tolerance and the
title truncation were all hunted adversarially and found sound — most are
already pinned by the AGD·R1 suite. Two gaps were closed this round:

- **A1 (Observability, fixed):** `refreshAgenda()` was silent on the chosen
  clock anchor / day window / merge result, so an "empty timeline" was not
  diagnosable from serial alone. Added one `[Agenda]` log line.
- **A5 (Correctness coverage, pinned):** the spec's "alphabetically-first
  all-day + earliest timed win" rule was reviewed-correct but **unpinned** for
  the case where BOTH sections compete for a short buffer. Added a regression
  test, proven to FAIL when the timed ordering is broken.

No Critical, no Safety findings. The merge is heap-free, the fixed marker
buffers cannot overflow (scans bounded to `CAL_MAX_EVENTS` / `TODO_MAX_TASKS`),
and the agenda arrays live in static storage, not on a task stack.

---

## Findings ledger (CSPMO)

| ID | Dim | Severity | Finding | Disposition |
|----|-----|----------|---------|-------------|
| A1 | O | Suggestion (fixed) | `refreshAgenda()` emitted **no serial diagnostics** for the chosen clock anchor, the `[d0,d1)` day window, or the merge result. `CalendarStore::load` logs the raw event count (`[CalStore] loaded N events`), but nothing logged what the merge made of it. A developer diagnosing an empty timeline could not tell from serial alone whether the cache was empty (`events=0`), the clock was unfixed (`now=0` → window `[0,0)` excludes everything), or a busy day simply merged to N. Every other component (`[AppManager]`, `[CalStore]`, `[TodoSync]`, `[Todo]`) logs; the agenda path was the lone silent one. | **Fixed**: one `[Agenda] now=… window=[…,…) events=… items=… next=…` line per refresh in `AppManager::refreshAgenda()`, matching the house `[Component]` convention. Firmware-only (reviewed by inspection; the merge logic it reports is native-tested). Commit `1170a33`. |
| A2 | C | None | **Window rule** `startUtc < dayEndUtc && endUtc > dayStartUtc` is correct: an event starting exactly at `dayEndUtc` (`start < dayEnd` false) or ending exactly at `dayStartUtc` (`end > dayStart` false) is OUT; multi-day all-day events spanning today are IN. | No action — correct; pinned by `test_agenda_day_boundaries`, `test_agenda_outside_window_excluded`, `test_agenda_multiday_allday_spanning`. |
| A3 | C | None | **All-day alphabetical sort** is stable and correct: selection sort with strict `strcmp < 0` keeps the FIRST minimum; calendar is scanned before todo so calendar wins cross-source ties; equal titles within a source keep call order. Verified adversarially with a mixed-case cross-source tie ("Banana"/"apple" cal + "apple" todo → `Banana, apple(cal), apple(todo)`). | No action — correct; pinned by `test_agenda_allday_only_sorted_alpha`, `test_agenda_mixed_allday_first`. |
| A4 | C | None | **`nextIdx`** is the first timed item with `timeUtc > nowUtc` (strictly future; an event starting exactly at `now` is NOT next), scanning the ascending timed section so it is the soonest; all-day items are skipped (`!allDay`); `-1` when all timed are past or none. | No action — correct; pinned by `test_agenda_nextidx_between_events`, `test_agenda_nextidx_all_past`, `test_agenda_nextidx_strictly_future`. |
| A5 | C | Suggestion (pinned) | **Capacity clamp, combined case**: `nAllDay` is clamped to `maxOut` (all-day fills first) and `nTimed` to `maxOut - written` (earliest timed take the remainder) — reviewed-correct and overflow-free (`written ≤ maxOut` always; `maxOut - written ≥ 0`), but the AGD·R1 suite pinned timed-only and all-day-only overflow **separately**, not the case where BOTH sections compete for a short buffer. | **Pinned**: new regression `test_agenda_capacity_allday_fills_first` — (1) 2 all-day + 5 timed into room-for-4 keeps the 2 alphabetical all-day then the 2 earliest timed, `nextIdx` on the first displayed future timed; (2) all-day alone meeting capacity leaves no timed slot (`nextIdx=-1`). **Proven to FAIL** when the timed ordering is broken ("Expected 'T-past' Was 'T-last'"), PASS with the correct seam. Commit `5686f75`. |
| A6 | S | None | **Stack footprint** of `agendaMergeToday`: five fixed marker buffers — `calAllDay[64]` + `calTimed[64]` + `takenCal[64]` (3×64 B) + `todoElig[32]` + `takenTodo[32]` (2×32 B) = **256 B** + a handful of scalars. Trivial for the ESP32-S3 loopTask stack (≥8 KB). The scans are bounded to `CAL_MAX_EVENTS` / `TODO_MAX_TASKS` (lines 97–98) so these buffers can never overflow even if a caller passes a larger count. | No action — within envelope. |
| A7 | S | None | **`AgendaItem _agendaItems[24]` + `CalendarEvent _agendaEvents[64]` RAM**: `AppManager` is a function-local **static** in `main.cpp` (`static AppManager mgr(ctx)`), so both arrays live in `.bss`, NOT on any task stack. Measured firmware RAM is 19.7% (64712 B) vs the 15.2% (49832 B) pre-agenda baseline — a ≈14.9 KB static footprint for the whole agenda feature (`_agendaEvents[64]` ≈ 12.8 KB + `_agendaItems[24]` ≈ 1.5 KB + scalars), well within the 327680 B budget. | No action — quantified, within budget. |
| A8 | S | None | **`strncpy` + explicit NUL** at all three copy sites (all-day cal, all-day todo, timed): copies `AGENDA_TITLE_MAX-1` then forces `[AGENDA_TITLE_MAX-1]='\0'` — no overflow; over-long cache titles truncate, never overrun. | No action — correct; pinned by `test_agenda_title_truncated_safely`. |
| A9 | S | None | **Null / zero tolerance**: `out==nullptr`/`maxOut<=0` → 0; null `calEv`/`todoTasks` or negative counts → treated as empty; null `nextIdx` guarded at both write sites. Never a crash. | No action — correct; pinned by `test_agenda_empty_inputs`. |
| A10 | S | None | **`time(nullptr)` before SNTP** returns garbage near 0 on the ESP32-S3 (no battery-backed RTC); the `CAL_CLOCK_MIN_EPOCH` fallback chain (NTP → `_agendaLastSyncUtc` → 0) handles it. When `now==0`, `d0=d1=0` so the window is `[0,0)` and every real (positive-epoch) event is excluded → the panel renders "No events today" rather than a garbage date. With A1's log line this state is now visible on serial (`now=0 window=[0,0) … items=0`). | No action — correct; now observable (A1). |
| A11 | P | None | **Selection sort O(n²)** over ≤64 events is ~4096 comparisons — trivial for an e-ink device refreshing ≤1/sec. **SD re-read on every `drawLauncher()`** (boot + return + tap) is ~50–100 ms for a ≤64 KB cache, dwarfed by the ~800 ms e-ink flush — acceptable, and keeps the panel fresh without any new network code. **No heap** in the seam (fixed buffers only); the only heap use is transient Arduino `String` in the UI render helpers, standard for the framework. | No action — within envelope. |
| A12 | M | None | **House style**: `// ===` header blocks, `namespace core`, `#pragma once`, config coupling documented; the Todo slot is documented at every touch-point (seam header, `AppManager.h`, `refreshAgenda`, panel placeholder) as a clean `nullptr/0` call-site change pending `TODO(TODO-BACKEND)`. `pad2`/`agendaHM` mirror `CalendarApp`'s `pad2`/`fmtHM` and the `TodoApp`/`WeatherApp` `pad2` exactly. Layout constants are annotated (`rx=396` "panel left edge past divider", `yTop=112` "first text baseline", `yMax=464` "room below for the Todo slot line", divider `x=370` "thin 2 px rule, y 60..500"). | No action — consistent. |
| A13 | C | Note | **`nextIdx` under capacity truncation**: when a busy day has more timed items than fit, the soonest-future item may be among the truncated ones; `nextIdx` then highlights the first *displayed* future item or `-1`. This is correct — the highlight can only point at a visible row, and the overflow is surfaced as "+ N more". | No action — intended behaviour (informational). |
| A14 | M/O | Deferred | On-panel layout coordinates (selection box, `drawBookText` baselines, "+ N more" placement) untested on hardware (needs a device in download mode). `DisplayManager::drawText` ignoring `fontSize` is a pre-existing limitation (out of scope). | **Deferred** to on-panel validation — ledgered (no code marker; process gate, same convention as TODO·R2 T15 / QR·R2 D1). |
| A15 | O | Deferred / BLOCKED | Live on-device verification of the agenda panel against a real synced cache. Two independent gates: (a) the calendar cache must be populated by a live sync (gated by ICS secrets + the Calendar app); (b) upload needs a physical BOOT hold on COM7. | **Deferred** 🔴-gated by the parallel-build phase — **DO NOT flash COM7**. Stays BLOCKED; never faked. The merge logic is fully native-tested; only the on-panel render is device-gated. |

**Critical findings: 0. Safety findings: 0.** A1 (observability) and A5
(coverage pin) were the two actionable items; both closed. Everything else
reviewed correct and pinned by the AGD·R1 suite.

---

## Degraded-mode / empty-state walkthrough (re-verified AGD·R2)

| Case | Path | Renders / reports | Verdict |
|------|------|-------------------|---------|
| (a) no `secrets.h` at all | calendar never synced → `/calendar.json` absent → `CalendarStore::load` returns 0 | Panel "No events today" + "Open the Calendar app to sync." (`_lastSyncUtc<=0`); serial `[Agenda] … events=0 items=0` | Sensible, loud |
| (b) clock unfixed (boot before NTP) | `time(nullptr) < CAL_CLOCK_MIN_EPOCH` → `now=_agendaLastSyncUtc`; if that too is 0 → `now=0` → window `[0,0)` | Empty timeline rather than a garbage date; serial `[Agenda] now=0 window=[0,0) …` makes the cause explicit (A1) | Sensible, loud |
| (c) corrupt `/calendar.json` | `deserializeCalendarCache` rejects (garbage/truncated/oversized) → 0 events | Empty state; next "Sync now" rewrites it | Sensible, loud |
| (d) busy day (>24 candidates) | merge clamps: alphabetical-first all-day + earliest timed win; panel draws what fits + "+ N more" | Deterministic, no overflow of `_agendaItems[24]` | Sensible (regression-pinned, A5) |
| (e) valid clock + synced cache | `now` valid → `[d0,d1)` window → merge → timeline + "next up" highlight | Full panel | Sensible |

No Critical in any path; the launcher never bricks.

---

## Verification evidence (AGD·R2)

| Check | Command | Result |
|-------|---------|--------|
| Native suite | `python -m platformio test -e native` | **229/229 passed** (baseline 228 + 1 AGD-R2 regression) |
| Firmware build | `python -m platformio run -e lilygo_t5_47_s3` | **SUCCESS** — RAM **19.7%** (64712/327680 B), Flash **31.7%** (1331609/4194304 B) |
| Degraded native | rename `secrets.h`→`.bak`; `pio test -e native` | **229/229 passed** |
| Degraded firmware | `pio run -e lilygo_t5_47_s3` (no secrets) | **SUCCESS** — RAM 19.1% (62476 B), Flash 27.4% (1150249 B); then `secrets.h` restored, no commits while renamed |
| Negative proof (A5) | temporarily flip the timed selection sort to pick *latest* (`<`→`>`) → `pio test -f test_agenda` | `test_agenda_capacity_allday_fills_first` **FAILED** ("Expected 'T-past' Was 'T-last'"); 7 agenda tests fail with the break. Restore the seam → 14/14 PASS |
| On-device render | (flash COM7) | **BLOCKED** — parallel-build phase + live-sync gate; not flashed, not faked |

---

## Deferred-work ledger (markers)

| Marker | Count | Where | Reason |
|--------|-------|-------|--------|
| `TODO(AGD-R2)` deferral markers | 0 | — | No code-level deferrals: both findings (A1, A5) were fixable now and fixed. The on-device gates (A14/A15) are process gates, ledgered above rather than marked in code (same convention as TODO·R2 / QR·R2 / WTH·R2). |
| `TODO(TODO-BACKEND)` | 5 | `src/app/AppManager.cpp` ×2, `src/app/AppManager.h` ×1, `src/app/AppRegistry.h` ×1, `src/core/AgendaMerge.h` ×1 | The disabled Todo app + the agenda's clean `nullptr/0` Todo slot, pending the backend decision (inherited; unchanged). |
| `TODO(R2)` | 8 | `src/core/CalendarEvent.h` ×2, `src/core/IcsParser.h` ×5, `test/test_calendar/test_calendar.cpp` ×1 | Inherited calendar round-2 leftovers — unchanged (must stay 8). |
| `TODO(TLS)` | 3 | `src/apps/calendar/CalendarSync.cpp`, `src/apps/weather/WeatherSync.cpp`, `src/apps/todo/TodoSync.cpp` | `setInsecure()` → proper CA validation, all three sync apps together in a dedicated hardening round (unchanged). |

**Total: 16 markers, 3 classes — unchanged before/after; no new marker
classes introduced this round.**

**0 Critical findings open.** The Agenda milestone is closed pending only the
BLOCKED on-device render / live-cache verification (A15), gated by the
parallel-build phase and the live-sync requirement — not by any code defect.
