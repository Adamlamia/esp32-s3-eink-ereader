# QR Toolkit — CSPMO Review & Fix (QR·R2)

Review-and-fix round over the QR·R1 QR Toolkit implementation on
`feature/qr`, mirroring the WTH·R2 bar: **zero Critical open** at the end,
every finding either fixed in-code (pinned by a regression test) or ledgered
with a disposition. Adversarial but fair — hunted for real logic / security /
memory / e-ink defects, not style.

**Scope reviewed:** `git diff 6666873...HEAD` — `src/core/Emvco.h`,
`src/core/QrPayload.h`, `src/apps/qr/QrApp.{h,cpp}`, `test/test_qr/test_qr.cpp`,
the QR block of `src/config.h`, the `platformio.ini` lib_dep, the
`src/app/AppRegistry.h` launcher delta, the `DisplayManager::fillRect()` seam,
and the README section.

**Baseline (QR·R1, PM-verified):** 182/182 native tests, firmware SUCCESS
(RAM 15.2%, Flash 31.6%), degraded-mode (no `src/secrets.h`) green,
0 QR-R2 deferral markers, inherited `TODO(R2)` ×8 + `TODO(TLS)` ×2.

---

## Findings ledger (CSPMO)

| ID | Dim | Severity | Finding | Disposition |
|----|-----|----------|---------|-------------|
| Q1 | C | Safety (fixed) | `emvcoBuild` accepted a caller-supplied tag-`63` field. The CRC trailer is auto-generated, so a payload already carrying a `63` built a string with a **duplicate** `63` trailer — which `emvcoParse` correctly rejects (`sawCrc` + `pos+8 != len`). Net effect: `parse(build(p)) != p`, silently breaking the documented lossless round-trip for any future caller that adds `63` by hand. Not reachable from QrApp today (it never adds `63`), but a real footgun in a payment seam whose whole contract is "never mangle, always round-trip". | **Fixed**: `emvcoBuild` now refuses any payload containing a tag-`63` field (returns false, `out` cleared — fails loudly, never partial). Pinned by regression test `test_build_rejects_caller_supplied_crc_tag` (proven to FAIL without the fix: "Expected FALSE Was TRUE"). |
| M1 | C/M | Suggestion (fixed) | `qrListAddWifi`'s comment claimed *"A WiFi payload always fits QR_PAYLOAD_MAX (192 < 320)"* — sloppy reasoning that conflated two different buffers. The truncation-safety it promised is REAL, but for a different reason: `wifiQrBuild` writes at most `QR_WIFI_QR_MAX-1` (191) chars into the local `payload[QR_WIFI_QR_MAX]` (it fails loudly on a too-small buffer), and `QR_WIFI_QR_MAX <= QR_PAYLOAD_MAX`, so `qrListAdd` can never truncate it. The invariant was asserted in prose only. | **Fixed**: corrected the comment to state the actual argument and pinned the invariant with a compile-time `static_assert(QR_WIFI_QR_MAX <= QR_PAYLOAD_MAX, …)`. Pinned at runtime by `test_list_add_wifi_payload_never_truncated`, which drives the assembled payload to exactly 191 chars (the max `wifiQrBuild` can emit) and asserts the stored entry is byte-complete. The `static_assert` was proven to fail loudly (compile error with a readable message) when the invariant is inverted. |
| C1 | C | None | **CRC byte-exactness**: `emvcoCrc16` is the textbook CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no final XOR). Independently re-verified this round with a separate Python impl: canonical check value `0x29B1` for "123456789"; `FIXTURE_BUILD_EXPECTED` trailer `5C13` and `FIXTURE_DUITNOW` trailer `432B` both reproduce exactly. CRC is computed over the payload **including** the `6304` header, per the EMVCo spec, in both build and parse. | No action — byte-exact; now triple-pinned (Unity check-value + 2 Python-cross-checked fixtures). |
| C2 | C | None | **TLV parse bounds**: every header read is guarded (`pos+4 > len`), declared lengths are checked against the remaining bytes (`pos+4+vlen > len` → reject), value copies are bounded by the 2-digit length ceiling (≤ 99 < `QR_EMVCO_VALUE_MAX`), the tag-63 trailer must be last/length-04/valid-hex/CRC-matching, and any field after `63` is rejected. On any failure `out` is cleared (never half-decoded). 12 negative-guard tests cover corrupt CRC, truncation, over-long declared length, missing CRC, non-hex CRC, garbage/short input, and field-after-CRC. | No action — no over-read path found; well guarded. |
| C3 | C | None | **WiFi escaping completeness**: exactly the five zxing specials `\ ; , : "` are backslash-prefixed, in both SSID and password; `wifiQrEscape` refuses (out="") rather than truncate a credential; `T:nopass` for open networks (null or "" password). Pinned by 7 WiFi tests incl. all-five-specials in SSID and in password. | No action — complete and correct. |
| C4 | C | None | **Carousel wrap/bounds**: tap cycles `(_index+1) % count` (wraps); `renderMain` clamps a stale `_index` to 0; `qrListAdd` rejects at `QR_MAX_ENTRIES` (count unchanged); `qrCopyTrunc` always NUL-terminates. Menu selection wraps `% MENU_COUNT`. | No action — correct. |
| C5 | C | None | **QR version/capacity guard**: `QR_BYTE_CAPACITY_LOW[14]` matches ISO/IEC 18004 byte-mode ECC-LOW capacities v1–v13 (independently checked: 17,32,53,78,106,134,154,192,230,271,321,367,425). `renderQr` picks the smallest fitting version in `QR_MIN_VERSION..QR_MAX_VERSION`, and `QR_PAYLOAD_MAX-1 (319) ≤ capacity[13] (425)`, so any storable payload encodes; the unreachable `renderPayloadTooLong` path is a loud safety net for the library's missing data-too-big error. Module-buffer sizing mirrors the lib's `(4v+17)²` formula (v13 → 596 B). | No action — correct; guards the lib's silent-corruption @TODO. |
| C6 | C | None | **Kind auto-detection edge cases**: `addSecretEntry` checks `http://`/`https://` FIRST, then `emvcoIsValid`. So a URL that also happens to validate as EMVCo is captioned "URL" (the user's obvious intent), and a genuine QRPS (starts `0002…`) is captioned "DuitNow payment". Precedence resolves the ambiguity correctly. (Firmware-only logic — not native-unit-testable without pulling the HAL in; reviewed by inspection.) | No action — correct by construction (documented in QrApp.h). |
| S1 | S | None | **No secret leakage**: the raw payload is NEVER drawn — `renderQr` renders only the bitmap + the kind caption; `renderMain` draws only the label + footer; nothing is logged. Test fixtures are synthetic (the DuitNow merchant ID `60123456789` and name "KEDAI BUKU ALAM" are dummy data). `secrets.h` is git-ignored and never referenced by tests. | No action — sound. |
| S2 | S | None | **Parse over-read / adversarial input**: see C2 — all reads bounds-checked; malformed/adversarial EMVCo is rejected loudly and leaves the output struct empty. | No action — sound. |
| P1 | P | None | **Heap/HAL-free seams**: `Emvco.h` + `QrPayload.h` use only fixed buffers (no new/malloc) and compile standalone (`#ifndef` fallbacks) under `pio test -e native`; `ricmoo/QRCode` is a firmware-only `lib_deps` entry the native env never sees. Encoder state (`s_qr` + 596 B) is `static`, keeping ~600 B off the 8 KB loop-task stack. `flush(true)` once per screen — no needless full-refresh loops. | No action — within envelope. |
| O1 | O | None | **Degraded mode**: with `secrets.h` absent the firmware builds (RAM 14.5% / Flash 27.3%) and the app renders its empty state; every negative guard fails loudly (12 QR negative tests + the 2 QR-R2 regressions). Re-verified this round (see walkthrough). | No action — verified. |
| D1 | M/O | Deferred | On-panel layout coordinates + real-world QR scanability untested on hardware (needs a device in download mode). `DisplayManager::drawText` ignoring `fontSize` is a pre-existing limitation (already a prose TODO in `DisplayManager.cpp`, out of scope). | **Deferred** to on-panel validation — ledgered below (no code marker; process gate). |
| D2 | O | Deferred / BLOCKED | Live on-device QR scan verification (upload needs physical BOOT hold on COM7). | **Deferred** 🔴-gated by the parallel-build phase — **DO NOT flash COM7**. Stays BLOCKED; never faked. |

**Critical findings: 0.** Q1 was the single highest-signal issue (a Safety /
round-trip-contract defect in the payment seam) and was fixed + regression-
pinned. M1 was a maintainability hardening (prose invariant → `static_assert`).
Everything else reviewed correct and pinned by the existing QR·R1 suite.

---

## Independent CRC cross-check (this round)

A separate Python CRC-16/CCITT-FALSE implementation reproduced every fixture
trailer, confirming the C++ seam is byte-exact against the published standard:

| Input (CRC computed over) | Python CRC | Fixture trailer | Match |
|---------------------------|-----------|-----------------|-------|
| `"123456789"` (canonical check value) | `0x29B1` | — (defining constant) | ✅ |
| `FIXTURE_BUILD_EXPECTED` body + `6304` | `0x5C13` | `…63045C13` | ✅ |
| `FIXTURE_DUITNOW` body + `6304` | `0x432B` | `…6304432B` | ✅ |

---

## Degraded-mode / empty-state walkthrough (re-verified QR·R2)

| Case | Path | Renders | Verdict |
|------|------|---------|---------|
| (a) no `secrets.h` at all | `WIFI_STA_SSID` / `QR_PAYLOAD_n` all undefined → `buildEntries` adds nothing | Empty state ("No QR entries configured yet" + how-to) | Sensible |
| (b) WiFi creds only | `qrListAddWifi` from `WIFI_STA_SSID/PASS` | One "Wi-Fi" QR (caption "Wi-Fi network"); password NEVER drawn | Sensible |
| (c) QR_PAYLOAD_n only | `addSecretEntry` per `#ifdef` pair; missing label → "QR n+1" | Each entry, kind auto-captioned | Sensible |
| (d) payload too long to encode | capacity guard finds no fitting version (unreachable while `QR_PAYLOAD_MAX ≤ cap[13]`) | Readable "payload too long" screen, never a corrupt QR | Sensible (loud safety net) |
| (e) corrupt EMVCo pasted as payload | `emvcoIsValid` false → captioned "Text", still encodes | Text QR (garbage-in is the user's data, not a crash) | Sensible |

No Critical in any path; the app never bricks.

---

## Verification evidence (QR·R2)

| Check | Command | Result |
|-------|---------|--------|
| Native suite | `python -m platformio test -e native` | **184/184 passed** (baseline 182 + 2 QR-R2 regressions) |
| Firmware build | `python -m platformio run -e lilygo_t5_47_s3` | **SUCCESS** — RAM **15.2%** (49832/327680 B), Flash **31.6%** (1326429/4194304 B) |
| Degraded native | rename `secrets.h`→`.bak`; `pio test -e native` | **184/184 passed** |
| Degraded firmware | `pio run -e lilygo_t5_47_s3` (no secrets) | **SUCCESS** — RAM 14.5%, Flash 27.3% (then `secrets.h` restored, no commits while renamed) |
| Negative proof (Q1) | revert tag-63 guard → `pio test -f test_qr` | `test_build_rejects_caller_supplied_crc_tag` **FAILED** ("Expected FALSE Was TRUE"); passes with the fix restored |
| Negative proof (M1) | invert `static_assert` condition → compile | **loud compile error**: "static assertion failed: qrListAddWifi: a max-length WiFi payload must fit the entry buffer…"; compiles clean when restored |
| On-device scan | (flash COM7) | **BLOCKED** — parallel-build phase; not flashed, not faked |

---

## Deferred-work ledger (markers)

| Marker | Count | Where | Reason |
|--------|-------|-------|--------|
| QR-R2 deferral markers | 0 | — | No code-level deferrals: both findings were fixable now and fixed. The on-device gate (D2) is a process gate, ledgered above rather than marked in code (same convention as WTH·R2's S2). |
| `TODO(R2)` | 8 | `src/core/CalendarEvent.h` ×2, `src/core/IcsParser.h` ×5, `test/test_calendar/test_calendar.cpp` ×1 | Inherited calendar round-2 leftovers — unchanged (must stay 8). |
| `TODO(TLS)` | 2 | `src/apps/calendar/CalendarSync.cpp`, `src/apps/weather/WeatherSync.cpp` | `setInsecure()` → proper CA validation, both apps together in a dedicated hardening round (unchanged). |

**0 Critical findings open.** The QR Toolkit milestone (M10) is closed pending
only the BLOCKED on-device scan verification (D2), which is gated by the
parallel-build phase, not by any code defect.
