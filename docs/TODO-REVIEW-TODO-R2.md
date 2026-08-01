# Todo Feature — CSPMO Review & Fix (TODO·R2)

Review-and-fix round over the TODO·R1 Todo (Tasks-calendar checklist)
implementation on `feature/todo`, mirroring the WTH·R2 / QR·R2 bar: **zero
Critical open** at the end, every finding either fixed in-code (pinned by a
regression test) or ledgered with a disposition. Adversarial but fair — hunted
for real logic / security / memory / e-ink / sync-lifecycle defects, not style.
This round CLOSES the Todo milestone (M11).

**Scope reviewed:** `git diff ce6b413...HEAD` — `src/core/TodoModel.h`,
`src/apps/todo/TodoStore.{h,cpp}`, `src/apps/todo/TodoSync.{h,cpp}`,
`src/apps/todo/TodoApp.{h,cpp}`, `test/test_todo/test_todo.cpp`, the Todo block
of `src/config.h`, the `src/app/AppRegistry.h` launcher delta, the README
section — **plus the shared-seam extension** it depends on: `CalendarEvent::uid`
(`src/core/CalendarEvent.h`) and the ICS `UID` capture (`src/core/IcsParser.h`),
which touch calendar code the whole 184-test baseline depends on.

**Baseline (TODO·R1, PM-verified):** 214/214 native tests, firmware SUCCESS
(RAM 15.2%, Flash 31.9%), degraded-mode (no `src/secrets.h`) green,
0 `TODO(TODO-R2)` markers, inherited `TODO(R2)` ×8 + `TODO(TLS)` ×3 (TodoSync
extended the TLS tag).

---

## Shared-seam verdict (the emphasized risk)

The `CalendarEvent::uid` + `IcsParser` UID-capture change is **truly additive
and cannot corrupt calendar behaviour** — verified, not assumed:

- `calEventClear()` zeroes `uid[0]` (NUL-terminated empty), so a parsed event
  that omits `UID` carries `""`, never garbage; the parser captures UID only
  *inside* a `VEVENT` (VTIMEZONE/VCALENDAR UIDs are skipped) via the bounded,
  always-NUL-terminated `icsDecodeText(value, e.uid, CAL_UID_MAX)` — over-long
  UIDs truncate, never overrun (pinned by `test_parser_uid_truncated_safely`).
- The property match is exact `strcmp(name,"UID")`, so `X-UID`/`UIDFOO` cannot
  false-positive; a later `UID` line wins (Google emits exactly one).
- Recurrence expansion copies `uid` into each concrete occurrence for free
  (`CalendarEvent o = e;` struct copy) — no per-occurrence code needed.
- The **calendar cache does NOT persist uid** (`serializeCalendarCache` writes
  `s,e,t,c,a,f,i,n,u,b` only) and the calendar app never *reads* `uid`, so the
  round-trip is unchanged for the calendar. The Todo app uses its own
  `/todo.json` (`serializeTodoCache` persists `u`), so nothing cross-contaminates.
- Struct growth (+96 B/event) lands only on **heap-resident** arrays — the
  `CalendarApp` member (`_events[64]`, `new`-allocated) and the sync sessions'
  `std::vector` buffers — never on a task stack. Firmware RAM is unchanged at
  15.2%. The full 184-test calendar/weather/qr baseline stays green (215/215).

---

## Findings ledger (CSPMO)

| ID | Dim | Severity | Finding | Disposition |
|----|-----|----------|---------|-------------|
| T1 | C/S | Suggestion (fixed) | `deserializeTodoCache` materialised **phantom tasks** from non-object / field-less `tasks[]` elements. A corrupt / hand-edited / adversarial `/todo.json` (e.g. `"tasks":[1,null,"x",{},{"d":5}]`) decoded each junk entry to an empty title **and** empty uid, which would surface on screen as phantom `(untitled)` rows with a degenerate `d<day>#` done-key. This was an internal inconsistency: the done-loop in the *same* function already skips empty / non-string keys, and `CalendarStore::deserialize` drops events lacking the mandatory start — but the tasks loop had no such guard. (W1-analogue: defense-in-depth on the read path; the cache is only ever written by our own sanitising code, so not a live bug.) | **Fixed**: the tasks loop now skips any element that yields neither a title nor a UID (`if (t.title[0]=='\0' && t.uid[0]=='\0') continue;`), mirroring CalendarStore + the done-loop. Pinned by regression test `test_cache_skips_phantom_task_elements` — **proven to FAIL without the fix** ("Expected 1 Was 6"), PASS with it. |
| T2 | C | None | **Shared-seam additivity** (CalendarEvent.uid + IcsParser UID capture) — see the dedicated verdict above: bounded/NUL-terminated capture, exact property match, VEVENT-only, free recurrence copy, calendar cache deliberately uid-free, heap-only struct growth. | No action — additive; 184-test baseline green (215/215). |
| T3 | C | None | **All-day-only extraction**: `todoExtractTasks` skips `!allDay` (timed events are calendar events, not tasks — negative-pinned); recurring all-day masters count as one task; title/uid copies bounded + always NUL-terminated; NULL / non-positive args → 0. | No action — correct; pinned by 5 extraction tests. |
| T4 | C | None | **UID identity + fallback**: `todoMakeKey` prefers the ICS UID (stable across a phone-side rename — the design point, pinned end-to-end by `test_pipeline_resync_keeps_done_across_rename`); the `d<dayUtc>#<title>` fallback is unambiguous (the `d` prefix + purely-numeric dayUtc + *first* `#` delimiter mean a `#` inside the title cannot collide); snprintf bounds over-long fallback keys; deterministic. | No action — correct; pinned by 3 key tests + pipeline. |
| T5 | C | None | **Done toggle/prune off-by-ones**: removal gap-fills by moving the last key into the gap — correct even when removing the last element (no copy, just `--count`); `todoDonePrune` re-tests slot `i` after a gap-fill (no skipped key); full-set **add** fails loudly (returns false, set untouched) while removal on a full set still works and frees a slot; NULL / empty keys rejected everywhere. | No action — correct; pinned by `test_toggle_*` + `test_prune_*` (incl. the full-set negative guard). |
| T6 | C | None | **Cache (de)serialization round-trip**: lossless for bounded inputs; tolerant of empty / garbage / truncated / non-object documents (→ empty, never a crash); counts bounded to `TODO_DONE_MAX` / caller max; done-only load (`tasks==nullptr`) recovers keys + sync; `sync` timestamp kept even when arrays are missing. | No action — correct; pinned by 9 serialization tests (+ T1). |
| T7 | C | None | **Auto-paging index math**: `rowsPerPage` floored at 1; `_page` follows `_sel` (scrolling window) and is clamped `>= 0`; the page indicator uses the same `_page/rowsPerPage+1` house formula as CalendarApp; `rebuildRows` bounds `_rowCount` to `TODO_MAX_TASKS+1` (the array size) — worst case 32 tasks + 1 separator = 33 rows fits exactly; `_sel` is always kept on a task row (clamped, separator skipped). | No action — correct (firmware-only; reviewed by inspection). |
| T8 | C | None | **Gesture state machine**: Main Tap=move (wraps, skips the group separator) / MediumHold=toggle / LongHold=menu; Menu Tap=move / LongHold=select (matches Calendar/Weather); `toggleSelected` guards the separator (`_rowTask < 0`) and out-of-range indices; after a toggle the highlight follows the task into its new group; "Sync now" returns to Main. | No action — correct (firmware-only; reviewed by inspection). |
| T9 | S | None | **No secret leakage**: `TODO_ICS_URL` is never logged, drawn, or committed — only the hint "No Tasks calendar (set TODO_ICS_URL)" is shown; all test fixtures are synthetic ICS + synthetic UIDs; `src/secrets.h` is git-ignored (verified `git check-ignore`); `setInsecure()` inherits the tracked `TODO(TLS)` hardening tag. | No action — sound. |
| T10 | S | None | **Parse/store over-read + adversarial JSON**: `icsDecodeText` bounded; `TodoStore::load` rejects size 0 / `> TODO_CACHE_MAX_BYTES` and short reads; the deserializers never crash on hostile input (garbage / truncated / over-full / non-string — 12+ negative tests); HTTPS body pre-alloc check rejects a declared `> TODO_BODY_MAX` *before* `getString()`, with chunked/unknown sizes caught post-fetch. | No action — sound (+ T1 hardens the tasks path). |
| T11 | S | None | **HTTPS lifecycle**: portal guard refuses to start while the portal is live (checked in both `shouldAutoSyncOnEnter` and `runInternal`); the RAII `WifiOffGuard` powers the radio down on **every** exit path; the session body is a verbatim port of the live-verified calendar/weather session on the shared 24 KB `wifiSessionRunOnTask` trampoline. | No action — sound (firmware-only; inherited). |
| T12 | P | None | **Heap-free seams + stack envelope**: `TodoModel.h` uses only fixed buffers (no new/malloc) and compiles native; the sync buffers (`evBuf`/`tasks`/`doneBuf`) are heap `std::vector`s so they never touch the 24 KB task stack; `parseIcsFeed`'s 8 KB unfold buffer + mbedTLS fit the same envelope as the calendar; `flush(true)` once per screen (no needless full-refresh loops). Struct growth quantified under T2 — heap-only, RAM unchanged (15.2%). | No action — within envelope. |
| T13 | M | None | **House style**: `config.h` `// --- Todo app ---` block documents every `TODO_*` constant with rationale; header comment blocks on every file; the store/sync/app mirror the WeatherStore / CalendarStore / CalendarSync patterns line-for-line where it matters; AppRegistry delta is one include + one `addApp`. | No action — consistent. |
| T14 | O | None | **Degraded mode + loud negatives**: with `secrets.h` absent the firmware builds (RAM 14.5% / Flash 27.6%) and native stays 215/215; the `TODO_ICS_URL`-absent path fails loudly in BOTH the empty-state screen and the sync result ("No Tasks calendar (set TODO_ICS_URL)"); no Wi-Fi secrets → "No Wi-Fi secrets (src/secrets.h)"; portal running → "Wi-Fi portal active - stop it first". Never bricks. | No action — re-verified this round (walkthrough below). |
| T15 | M/O | Deferred | On-panel layout coordinates (selection box, row metrics, `drawBookText` baselines) untested on hardware (needs a device in download mode). `DisplayManager::drawText` ignoring `fontSize` is a pre-existing limitation (prose TODO in `DisplayManager.cpp`, out of scope). | **Deferred** to on-panel validation — ledgered (no code marker; process gate, same convention as WTH·R2 S1 / QR·R2 D1). |
| T16 | O | Deferred / BLOCKED | Live on-device sync verification. Two independent gates: (a) `TODO_ICS_URL` is ABSENT from `secrets.h` — live sync is a 🟡 soft blocker, already stubbed to fail loudly; (b) upload needs a physical BOOT hold on COM7. | **Deferred** 🔴-gated by the parallel-build phase — **DO NOT flash COM7**. Stays BLOCKED; never faked. |

**Critical findings: 0.** T1 was the single highest-signal issue — a
Suggestion (defense-in-depth on the read path, the W1-analogue), not a live
bug, since the cache is only ever written by the already-sanitising sync/store
code. Fixed anyway, with a regression test proven to fail without it, to match
the house bar. Everything else reviewed correct and pinned by the TODO·R1 suite.

---

## Degraded-mode / empty-state walkthrough (re-verified TODO·R2)

| Case | Path | Renders / reports | Verdict |
|------|------|-------------------|---------|
| (a) no `secrets.h` at all | `WIFI_STA_*` + `TODO_ICS_URL` undefined → `shouldAutoSyncOnEnter` false; `renderMain` `#if !defined(TODO_ICS_URL)` | Empty state "No Tasks calendar (set TODO_ICS_URL)" + how-to | Sensible, loud |
| (b) Wi-Fi secrets only, no `TODO_ICS_URL` | auto-sync guarded off (`#if … && defined(TODO_ICS_URL)`); manual "Sync now" → `runInternal` `#if !defined(TODO_ICS_URL)` | Screen empty-state; sync result "No Tasks calendar (set TODO_ICS_URL)" | Sensible, loud |
| (c) secrets present, never synced | `_lastSyncUtc == 0` → auto-sync on open (if clock valid + no portal + battery ≥ floor) | "Syncing…" splash → cache → checklist | Sensible |
| (d) corrupt `/todo.json` | `deserializeTodoCache` rejects (garbage/truncated/oversized) → 0 tasks + empty done | Empty state; next "Sync now" rewrites it | Sensible, loud |
| (e) adversarial `/todo.json` w/ junk tasks[] | T1 guard skips field-less / non-object elements | Only real tasks shown; no phantom `(untitled)` rows | Sensible (regression-pinned) |
| (f) portal running | guard in `shouldAutoSyncOnEnter` + `runInternal` | "Wi-Fi portal active - stop it first" | Sensible, loud |

No Critical in any path; the app never bricks.

---

## Verification evidence (TODO·R2)

| Check | Command | Result |
|-------|---------|--------|
| Native suite | `python -m platformio test -e native` | **215/215 passed** (baseline 214 + 1 TODO-R2 regression) |
| Firmware build | `python -m platformio run -e lilygo_t5_47_s3` | **SUCCESS** — RAM **15.2%** (49832/327680 B), Flash **31.9%** (1338045/4194304 B) |
| Degraded native | rename `secrets.h`→`.bak`; `pio test -e native` | **215/215 passed** |
| Degraded firmware | `pio run -e lilygo_t5_47_s3` (no secrets) | **SUCCESS** — RAM 14.5% (47596 B), Flash 27.6% (1156621 B); then `secrets.h` restored, no commits while renamed |
| Negative proof (T1) | `git stash push -- src/core/TodoModel.h` (revert guard) → `pio test -f test_todo` | `test_cache_skips_phantom_task_elements` **FAILED** ("Expected 1 Was 6"); `git stash pop` restores the fix → PASS |
| On-device sync | (flash COM7) | **BLOCKED** — parallel-build phase + `TODO_ICS_URL` absent; not flashed, not faked |

---

## Deferred-work ledger (markers)

| Marker | Count | Where | Reason |
|--------|-------|-------|--------|
| `TODO(TODO-R2)` deferral markers | 0 | — | No code-level deferrals: the one finding (T1) was fixable now and fixed. The on-device gates (T15/T16) are process gates, ledgered above rather than marked in code (same convention as WTH·R2 / QR·R2). |
| `TODO(R2)` | 8 | `src/core/CalendarEvent.h` ×2, `src/core/IcsParser.h` ×5, `test/test_calendar/test_calendar.cpp` ×1 | Inherited calendar round-2 leftovers — unchanged (must stay 8). |
| `TODO(TLS)` | 3 | `src/apps/calendar/CalendarSync.cpp`, `src/apps/weather/WeatherSync.cpp`, `src/apps/todo/TodoSync.cpp` | `setInsecure()` → proper CA validation, all three sync apps together in a dedicated hardening round (unchanged; TodoSync inherited the tag in R1). |

**0 Critical findings open.** The Todo milestone (M11) is closed pending only
the BLOCKED on-device / live-sync verification (T16), which is gated by the
parallel-build phase and the absent `TODO_ICS_URL` soft blocker — not by any
code defect.
