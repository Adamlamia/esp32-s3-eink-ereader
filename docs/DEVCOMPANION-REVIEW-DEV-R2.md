# Dev Companion Feature — CSPMO Review & Fix (DEV·R2)

Review-and-fix round over the DEV·R1 Dev Companion (reference viewer + GitHub
dashboard) implementation on `main`, mirroring the AGD·R2 / TODO·R2 / QR·R2 /
WTH·R2 bar: **zero Critical open** at the end, every finding either fixed
in-code or ledgered with a disposition. Adversarial but fair — hunted for real
logic / memory / e-ink / network / observability defects in the two pure seams
and the app/sync/store wiring, not style. This round reviews and hardens the
Dev Companion milestone.

**Scope reviewed:** `src/core/RefsIndex.h` (refs_index.txt parser seam),
`src/core/GithubModel.h` (GitHub API parse + cache serialize/deserialize seam),
`src/apps/devcompanion/DevCompanionApp.{h,cpp}` (two-section app),
`src/apps/devcompanion/GithubSync.{h,cpp}` (WiFiSession HTTPS sync),
`src/apps/devcompanion/GithubStore.{h,cpp}` (fs::FS cache wrapper),
`src/display/DisplayManager.{h,cpp}` (new `blitRaw()` + `fillRectShade()`),
the `REFS_*` / `GITHUB_*` block of `src/config.h`,
`test/test_devcompanion/test_devcompanion.cpp`, and `tools/make_refs.py`.
`CalendarStore`, `TodoModel`, `IcsParser`, `CalendarEvent`, `WeatherStore` and
`AgendaMerge` treated READ-ONLY per the round contract (not touched).

**Baseline (DEV·R1, PM-verified):** 253/253 native tests, firmware SUCCESS.
This round: **257/257** native (253 + 4 DEV-R2 regressions), firmware SUCCESS
(RAM 19.7% / Flash 32.0%), degraded-mode (no `src/secrets.h`) green both native
(257/257) and firmware, marker census **19** (3 `TODO(TLS)` added by DEV·R1's
GithubSync over the AGD·R2 16; no new marker classes introduced this round).

---

## Verdict

The two pure seams (`parseRefsIndex`, `parseGithubCount`, `parseGithubCi`,
`serializeGithubCache` / `deserializeGithubCache`) are **correct and
well-pinned** — CRLF, blank lines, oversized-field truncation, missing `|`
fallback, null/zero tolerance, dual-key CI parsing, `null`-conclusion Pending,
cache round-trip and the corrupt-file contract were all hunted adversarially and
found sound. Two genuine **Critical** defects were caught and fixed this round,
both in the firmware-only layer the native seams can't reach:

- **D1 (Correctness, fixed):** the two `search/issues` URLs omitted
  `per_page=1`, so GitHub's default `per_page=30` returned up to 30 full issue
  objects (tens of KB) that exceed `GITHUB_BODY_MAX` (8192) and were rejected —
  PR/issue counts read 0 / the repo was marked failed for any repo with several
  open items. Added `&per_page=1` (only `total_count` is consumed), matching the
  config comment's own assumption.
- **D2 (Correctness, fixed):** `renderRefImage` clamped a short `.raw`
  (`sz > REFS_RAW_SIZE ? REFS_RAW_SIZE : sz`) and blitted it as valid
  (`drawn=true`) over the **never-cleared** framebuffer — a truncated/corrupt
  SD file produced a garbage/partial panel instead of the readable placeholder.
  Added the pure, host-tested `core::refsRawSizeValid` seam (exact full frame
  only) and reject non-exact sizes before `blitRaw`.

Two cheap **Observability** gaps were closed (D3: which ref image is shown was
never logged on the happy path; D4: the GitHub on-open auto-sync decline was
silent). No Safety findings: the 259200-byte blit is PSRAM→PSRAM heap (never
stack), `strncpy`+NUL at every copy site, `GITHUB_BODY_MAX` enforced
pre-alloc + post-fetch.

---

## Findings ledger (CSPMO)

| ID | Dim | Severity | Finding | Disposition |
|----|-----|----------|---------|-------------|
| D1 | C | **Critical (fixed)** | Both `search/issues` URLs in `GithubSync::runInternal` omitted `per_page=1`. GitHub's search default is `per_page=30`, so any repo with more than a handful of open PRs/issues returns a `items[]` array of tens of KB — above `GITHUB_BODY_MAX` (8192). `githubGet` rejects oversized bodies, `body` stays empty, `parseGithubCount("")` → -1 → `countsOk=false`: the repo is counted **failed** and its PR/issue counts read 0 (or, if all repos fail, the cache is never written and the panel stays empty). The `config.h` comment even says "search responses with per_page=1 are well under 1 KB" — the code simply never sent `per_page=1`. The `actions/runs` URL already had `per_page=1`; only the two search URLs were missing it. | **Fixed**: appended `&per_page=1` to both search URLs (only `total_count` is consumed, so a 1-item body is sufficient and keeps it well under the body cap). Firmware-only (network path), reviewed by inspection + URL-length check (still ≤ `GH_URL_MAX=192`). Commit `49632e7`. |
| D2 | C | **Critical (fixed)** | `renderRefImage` treated any non-empty `.raw` as valid: `sz` was clamped to `REFS_RAW_SIZE` and a **short** file (`rd < REFS_RAW_SIZE`) was still `blitRaw()`'d with `drawn=true`. The reference path deliberately does **not** clear the framebuffer first (the image is meant to cover the whole panel), so a truncated/corrupt dump (a short SD write, or a wrong converter) left the untouched tail showing **stale/garbage pixels** with no error indication — exactly the "bricking on corrupt SD" case the review asked about. | **Fixed**: added the pure, host-tested seam `core::refsRawSizeValid(sz)` (valid ⇔ `sz == REFS_RAW_SIZE == 259200`, the exact `tools/make_refs.py` output) and `renderRefImage` now rejects any non-exact size (logs `bad size N (want 259200) - corrupt?`) and falls through to the readable placeholder. `blitRaw` itself keeps its memory-safe clamp (it is a low-level blit); the exact-frame contract lives in the viewer + seam. Regression `test_refs_raw_size_valid` **proven to FAIL** ("Expected FALSE Was TRUE") when the seam is loosened to `sz <= REFS_RAW_SIZE`. Commits `49632e7` (seam+caller) + `2be3da8` (test). |
| D3 | O | Suggestion (fixed) | `renderRefImage` logged only **errors** (open failed / short read / alloc failed) — never which image was actually going on the panel, so a blank or wrong picture was not diagnosable from serial. The review explicitly asked "Does the refs viewer log which image it's displaying?" (it did not). | **Fixed**: one happy-path line `[DevCompanion] refs: show n/total 'Label' (/refs/file.raw)`. Firmware-only, by inspection. Commit `8c4b1e8`. |
| D4 | O | Suggestion (fixed) | `switchSection`'s on-open **auto-sync decline** was silent (only the "will sync" branch logged). A developer staring at an empty/stale GitHub panel could not tell from serial whether the cache was fresh, the clock unfixed, the portal up, or no secrets compiled in. | **Fixed**: one decline line `[DevCompanion] GitHub on-open: auto-sync skipped (sync=… repos=…; fresh/unfixed-clock/portal/no-secrets)`. Combined with `[GhStore] load`, `[GhSync]` per-repo and `runSync` result logs, an empty panel is now fully diagnosable from serial. Commit `8c4b1e8`. |
| D5 | C | Suggestion (pinned) | `parseGithubCount`'s handling of **HTTP error bodies** (`{"message":"API rate limit exceeded"}` etc.) is a valid-object-without-`total_count` → correctly returns -1, but the case was **unpinned**. (In practice `githubGet` only parses on HTTP 200, so a 403 rate-limit body never reaches the parser; the seam must still treat these as errors on its own.) | **Pinned**: new regression `test_count_http_error_body` (rate-limit / bad-credentials / Validation-Failed bodies all → -1). Commit `2be3da8`. |
| D6 | C | Suggestion (pinned) | `parseRefsIndex` handling of a **missing final newline** and of **multiple `\|` on one line** was reviewed-correct (last line without `\n` still parses; first `\|` wins, extra pipes stay in the label) but **unpinned**. | **Pinned**: `test_refs_missing_final_newline` + `test_refs_multiple_pipes_first_wins`. Commit `2be3da8`. |
| D7 | S | None | **259200-byte blit is heap, not stack**: the read buffer is `ps_malloc(REFS_RAW_SIZE)` and the destination `_framebuffer` is `ps_calloc`'d PSRAM — `memcpy` runs PSRAM→PSRAM. `GithubSync` executes on the dedicated 24 KB task (`wifiSessionRunOnTask`), so its `results[]`/`url[]` locals never touch the 8 KB loop stack. The app object is `new DevCompanionApp(ctx)` (heap, `AppRegistry`), so `_refs[16]` (16×72 = **1.1 KB**) + `_repos[4]` are heap, not stack. | No action — verified within envelope. |
| D8 | S | None | **`GITHUB_BODY_MAX` enforcement** is two-layer: pre-alloc reject when Content-Length is known (`declared > GITHUB_BODY_MAX`) + post-fetch length check (`b.length() <= GITHUB_BODY_MAX`); the chunked/unknown-length edge is acknowledged in-code (GitHub sends Content-Length for these small responses). **`strncpy`+NUL** at every copy site (`GithubSync` name, `GithubModel` name, `scanRefs` file). `CiState` cast is range-guarded (`ci>=0 && ci<=4`, else `Unknown`). | No action — correct; CiState guard pinned by `test_cache_sanitises_fields`. |
| D9 | C | None | **Section switching + item wrap at boundaries**: Tap is guarded by `_refCount > 0` / `_repoCount > 0` (0-item case is a deliberate no-op); 1-item wraps to itself (`(0+1)%1==0`); max-item wraps via modulo; `_ghHighlight`/`_refIndex` re-clamped in `renderGithub`/`renderRefs`/`loadGithubCache`. MediumHold is context-dependent (Refs→switch, GitHub→sync) and the reverse direction is always reachable via the menu — documented in the header. `per_page=1` on `actions/runs` is correct (returns the most recent run). | No action — correct; boundary behaviour confirmed by inspection. |
| D10 | P | Note | **259200-byte SD read** per image flip is ~100–200 ms on SPI SD — acceptable and dwarfed by the e-ink flush. **12 HTTPS GETs** (3/repo × 4) at 12 s timeout each is a ≤~2 min worst case for a manual/on-open sync — acceptable for a dashboard, and Wi-Fi is RAII-off on every exit. `parseRefsIndex` is single-pass O(n). | No action — within envelope (informational). |
| D11 | M | None | **House style** compliant (`// ===` headers, `namespace core`, `#pragma once`, config coupling documented). GitHub REST endpoints documented in both `GithubSync.h` and `GithubModel.h` for future maintenance. `make_refs.py` is self-documenting (`--help` epilog describes the 4-bpp layout + examples, module docstring documents the pipeline). MediumHold context-dependence documented in `DevCompanionApp.h`. | No action — consistent. |
| D12 | S | None | **`blitRaw` clamp vs validate**: `blitRaw` clamps `len` to the framebuffer (memory-safe, documented "a short buffer leaves the tail untouched") and null-guards both pointers. That is correct for a low-level blit; the *exact-frame* contract belongs to the refs viewer (D2) which now enforces it via `refsRawSizeValid`. No overflow possible either way. | No action — correct by design; contract moved to the tested seam (D2). |
| D13 | M/O | Deferred | On-panel render validation of the reference viewer (full-screen blit, label plate) and a live GitHub sync against the real API need a device in download mode + secrets over the air. | **Deferred** 🔴-gated — **DO NOT flash COM7**. The seams are fully native-tested; only the on-panel render / live network path is device-gated (same convention as AGD·R2 A15). |

**Critical findings: 2 (D1, D2) — both fixed. Safety findings: 0.** D3/D4
(observability) fixed cheaply; D5/D6 pinned with regressions. Everything else
reviewed correct.

---

## Degraded-mode / empty-state walkthrough (re-verified DEV-R2)

| Case | Path | Renders / reports | Verdict |
|------|------|-------------------|---------|
| (a) no `secrets.h` at all | `#else` branches: `shouldAutoSyncGithub` → false; `renderGithub` (no PAT) empty state | "Set GITHUB_PAT in secrets.h" on screen; `runSync` reports "No Wi-Fi secrets" / "No GITHUB_PAT". Firmware + native build green (257/257). | Sensible, loud |
| (b) no `/refs/` dir or empty | `scanRefs` finds nothing | "No reference images. Copy .raw files to /refs/ on SD." + make_refs hint | Sensible, loud |
| (c) corrupt `.raw` (short/truncated) | `refsRawSizeValid(sz)` false → not blitted | Readable "Could not read /refs/…" placeholder + serial `bad size N (want 259200) - corrupt?` (D2) | Sensible, loud (regression-pinned) |
| (d) missing `.raw` (index lists a deleted file) | `open()` fails | Placeholder + serial `open(...) failed` | Sensible, loud |
| (e) corrupt `/github.json` | `deserializeGithubCache` rejects | 0 repos + "No GitHub data yet. MediumHold to sync now." | Sensible, loud |
| (f) repo with many open items | `per_page=1` keeps body tiny | Correct `total_count` parsed (D1 fix) | Sensible |

No Critical in any path; the app never bricks on hostile SD or missing secrets.

---

## Verification evidence (DEV-R2)

| Check | Command | Result |
|-------|---------|--------|
| Native suite | `python -m platformio test -e native` | **257/257 passed** (baseline 253 + 4 DEV-R2 regressions) |
| Firmware build | `python -m platformio run -e lilygo_t5_47_s3` | **SUCCESS** — RAM **19.7%** (64712/327680 B), Flash **32.0%** (1342869/4194304 B) |
| Degraded native | rename `secrets.h`→`.bak`; `pio test -e native` | **257/257 passed** |
| Degraded firmware | `pio run -e lilygo_t5_47_s3` (no secrets) | **SUCCESS**; then `secrets.h` restored, no commits while renamed |
| Negative proof (D2) | loosen `refsRawSizeValid` to `sz <= REFS_RAW_SIZE`; `pio test -f test_devcompanion` | `test_refs_raw_size_valid` **FAILED** ("Expected FALSE Was TRUE", line 197); restore the seam → 29/29 PASS |
| On-device render / live sync | (flash COM7 + live GitHub API) | **BLOCKED** — device-gated; not flashed, not faked |

---

## Deferred-work ledger (markers)

| Marker | Count | Where | Reason |
|--------|-------|-------|--------|
| `TODO(DEV-R2)` deferral markers | 0 | — | No code-level deferrals: all four actionable findings (D1–D4) were fixed now. The on-device gate (D13) is a process gate, ledgered above rather than marked in code (same convention as AGD·R2 / QR·R2). |
| `TODO(TLS)` | 6 | `src/apps/calendar/CalendarSync.cpp` ×1, `src/apps/weather/WeatherSync.cpp` ×1, `src/apps/todo/TodoSync.cpp` ×1, `src/apps/devcompanion/GithubSync.{h,cpp}` ×3 | `setInsecure()` → proper CA validation, all sync apps together in a dedicated hardening round. **3 → 6 this round**: DEV·R1's `GithubSync` inherited the same tag (bearer PAT is the most sensitive payload, so it matters most here). Unchanged by DEV·R2 (TLS hardening is out of this round's scope). |
| `TODO(TODO-BACKEND)` | 5 | `src/app/AppManager.cpp` ×2, `src/app/AppManager.h` ×1, `src/app/AppRegistry.h` ×1, `src/core/AgendaMerge.h` ×1 | The disabled Todo app + the agenda's clean `nullptr/0` Todo slot, pending the backend decision (inherited; unchanged). |
| `TODO(R2)` | 8 | `src/core/IcsParser.h` ×5, `src/core/CalendarEvent.h` ×2, `test/test_calendar/test_calendar.cpp` ×1 | Inherited calendar round-2 leftovers — unchanged (must stay 8). |

**Total: 19 markers, 3 classes.** Before/after DEV·R2 is unchanged (19 → 19);
the +3 vs the AGD·R2 census of 16 is DEV·R1's `GithubSync` `TODO(TLS)` tags.
**No new marker classes introduced this round.** Secrets never committed
(`git check-ignore src/secrets.h` → ignored; `git log --all -- src/secrets.h` empty).

**0 Critical findings open.** The Dev Companion milestone is closed pending only
the BLOCKED on-device render / live-sync verification (D13), gated by the
device + live-API requirement — not by any code defect.
