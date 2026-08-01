# Weather Feature — CSPMO Review & Fix (WTH·R2)

Review-and-fix round over the WTH·R1 weather implementation on
`feature/weather`, mirroring the calendar's CAL·R4 bar: **zero Critical open**
at the end, every Suggestion either fixed or ledgered with a marker.

**Scope reviewed:** `src/apps/weather/` (WeatherApp, WeatherStore, WeatherSync),
`src/core/OpenMeteo.h`, the weather block of `src/config.h`, and the
`src/app/AppRegistry.h` launcher delta.

**Baseline:** 146/146 native tests, firmware SUCCESS (RAM 15.0%, Flash 31.4%),
degraded-mode (no `src/secrets.h`) green, `TODO​(WTH-R2)` ×3, inherited
`TODO​(R2)` ×8.

---

## Findings ledger (CSPMO)

| ID | Dim | Severity | Finding | Disposition |
|----|-----|----------|---------|-------------|
| W1 | C | Suggestion (fixed) | `deserializeWeatherCache` trusted cached numbers the live parser sanitises — a corrupt / hand-edited / future-schema `/weather.json` could surface an out-of-physical-range wind, an inverted day min/max, or an out-of-range code on screen. | **Fixed** (`2e9e313`): deserializer now mirrors `parseOpenMeteo` — wind clamp 0..500 + non-finite→0, day min/max swap, code-range tolerance. +5 regression tests (146→151). |
| W2 | M | Suggestion (fixed) | `CalendarSync` and `WeatherSync` carried near-verbatim Wi-Fi/NTP/24 KB-trampoline code (the two `TODO​(WTH-R2)` markers). | **Fixed** (`1b044c8`): extracted `src/app/WifiSession.{h,cpp}` used by both. Behaviour-preserving for the live-verified calendar (verbatim move; only the serial log tag parameterised). |
| W3 | S | Deferred | `client.setInsecure()` skips TLS CA validation in both sync sessions (inherited from the live-verified calendar; brief does not mandate it; Open-Meteo carries no credential). | **Deferred** to a shared hardening round that fixes BOTH apps together — `TODO​(TLS)` ×2 (`CalendarSync.cpp`, `WeatherSync.cpp`). Replaces the third `TODO​(WTH-R2)` TLS ref. |
| W4 | C | None | Parser edge cases reviewed: temp non-finite rejected, humidity 0..100 clamp, wind 0..500 clamp, code 0..99, day min/max swap, label truncation, `WEATHER_BODY_MAX` pre-alloc check (inherited CAL·R4 fix). | No action — already correct; pinned by the 24 WTH·R1 parser tests. |
| W5 | C | None | Time-zone / weekday math: forecast day labels anchor on `fetchedUtc` (not `now`), so a stale cache rendered under an invalid boot clock still shows correct weekdays; `time()` is true UTC, no offset subtraction. | No action — correct by construction. |
| W6 | C | None | Stale-check + battery guard + menu state machine + e-ink full-refresh discipline in `WeatherApp`: auto-fetch only when cache > 3 h AND clock valid AND not portal-guarded; manual tap always allowed; full refresh on every paint. | No action — correct. |
| W7 | S | None | Secret handling: all use `#if defined`-guarded; no secrets in logs; URL/label built into fixed buffers with bounds checks; no JSON injection surface (parser is tolerant, output is numeric/enum). | No action — sound. |
| W8 | P | None | 24 KB task stack: `WeatherSnapshot` ~100 B + ArduinoJson doc bounded by `WEATHER_BODY_MAX`; no unbounded strings below the UI edge; no new heap in seams. | No action — within envelope. |
| W9 | O | None | Degraded mode (no secrets): app renders empty state / cached data; manual refresh reports "No Wi-Fi secrets (src/secrets.h)"; portal-running reports "Wi-Fi portal active - stop it first". Never bricks. | No action — verified by walkthrough below. |
| S1 | M | Deferred | On-panel layout coordinates untested on hardware; `DisplayManager::drawText` ignores its `fontSize` arg (pre-existing limitation, already a `TODO` in `DisplayManager.cpp`, explicitly out of scope). | **Deferred** to on-panel validation (needs a device in download mode). |
| S2 | O | Deferred | Live on-device fetch unverified (upload needs physical BOOT hold). | **Deferred** 🟡 — non-blocking; code path is a verbatim port of the live-verified calendar session. |

**Critical findings: 0.** W1 was the single highest-signal issue and was a
Suggestion (defense-in-depth on the read path), not a live bug — the cache is
only ever written by the already-sanitising live parser. Fixed anyway, with
regression tests, to match the CAL·R4 bar.

---

## Empty-state / first-fetch walkthrough

| Case | Path | Renders | Verdict |
|------|------|---------|---------|
| (a) no cache file | `WeatherStore::load` → not found → cleared snapshot | Empty state ("No weather yet"); auto-fetch on open if clock valid + secrets + no portal | Sensible |
| (b) corrupt cache | `deserializeJson` fails / `v` mismatch → cleared snapshot | Empty state (same as a) | Sensible |
| (c) valid cache + clock invalid (`time() < CAL_CLOCK_MIN_EPOCH`) | cache rendered; auto-fetch declined (can't judge staleness); forecast labels anchor on `fetchedUtc` | Last-known data shown; weekdays correct | Sensible |
| (d) valid cache + stale + no secrets | cache rendered; manual tap → "No Wi-Fi secrets (src/secrets.h)" | Last-known data + readable reason | Sensible |
| (e) valid cache + stale + portal running | cache rendered; auto-fetch + manual tap both portal-guarded → "Wi-Fi portal active - stop it first" | Last-known data + readable reason | Sensible |

No Critical in any path; the app never bricks. Cases (a)/(b)/(c) are pinned by
the WTH·R1 store/parser tests plus the W1 regression tests
(`test_cache_dataless_doc_is_empty_state`, `test_cache_day_temps_sanitised`, …).

---

## Deferred-work ledger (markers)

| Marker | Count | Where | Reason |
|--------|-------|-------|--------|
| `TODO​(TLS)` | 2 | `src/apps/calendar/CalendarSync.cpp:117`, `src/apps/weather/WeatherSync.cpp:90` | Replace `setInsecure()` with proper CA validation — both apps together in a dedicated hardening round. |
| `TODO​(R2)` | 8 | `src/core/CalendarEvent.h` ×2, `src/core/IcsParser.h` ×5, `test/test_calendar/test_calendar.cpp` ×1 | Inherited calendar round-2 leftovers — unchanged by this round (must stay 8). |
| fontSize note | 1 | `src/display/DisplayManager.cpp:78` (prose "is a TODO", not a `TODO(...)` token) | Pre-existing `drawText` ignores `fontSize` — out of scope (S1). |

`TODO​(WTH-R2)` census is now **0** (all three resolved: two by the WifiSession
extraction, one reclassified as `TODO​(TLS)`).
