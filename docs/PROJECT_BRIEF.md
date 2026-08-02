# Project Brief — ESP32-S3 E-Ink E-Reader

> **Source-of-truth briefing for the `project-manager` orchestration skill** and any
> future session that drives the build. Read this first.
>
> Related docs:
> - `docs/progress.canvas.tsx` — visual build dashboard (sync it at every checkpoint).
> - `docs/ROADMAP.md` — blue-sky wishlist (ideas, **not** the locked plan).
> - `docs/HARDWARE.md` — hardware notes.
>
> This brief captures the **locked** plan only. If this file and a conversation
> disagree, this file wins until explicitly updated.

---

## 0. How to use this document (for the orchestrator)

1. On session start, read this brief + `docs/progress.canvas.tsx` to recover state.
2. Drive work as a **prompt-chain**: for each round, hand the relevant feature spec
   (§2) to the `quality-prompt-builder` skill to produce the coding prompt, then run
   the `coding-executor` subagent, then **verify the artifact yourself** (do not copy
   the agent's claims), then sync the canvas, then decide the next round.
3. Honour the execution conventions in §5 — especially the three coding-agent
   mandates (logical commits, summary report, pre-flight human gates) and the
   environment quirks (PowerShell, `python -m platformio`, native-test PATH).
4. Respect the checkpoint/pause rules in §2.2. **Nothing builds until the user gives
   the go signal** — execution starts in a separate session (§7).

---

## 1. Snapshot (current state)

| Item | State |
|---|---|
| Branch | `main` |
| Firmware version | `0.2.0` (`FW_VERSION` in `src/config.h`) |
| Framework | Multi-app framework done: `App` base, `AppManager` (launcher + button decode + system tasks), `SystemContext` (DI), `AppRegistry.h` (one-line-per-branch registration) |
| Calendar app | Done, merged `--no-ff` (c75e50d) + pushed. Google ICS sync (no OAuth), ArduinoJson cache, NTP+HTTPS, portal-guarded, RAII WiFi-off, scheduled sync |
| Tests | **122 passing** (native Unity) |
| Web portal | OFF by default (`WEB_AUTO_START 0`) so calendar sync gets the radio |
| `src/secrets.h` | **EXISTS** (user-created; WiFi creds + ICS URLs). Live sync verified on hardware. Git-ignored — never commit |
| Canvas | Reflects completed calendar chain |
| Uncommitted | `docs/progress.canvas.tsx` (M); `docs/progress.canvas.status.json` untracked (leave alone) |
| Deferred tech debt | 8 `TODO(R2)` in calendar = MONTHLY/YEARLY recurrence |

---

## 2. Locked roadmap — 6 features (all spec'd, none built yet)

Execution mode: **autopilot**, with a checkpoint after each feature and a **full
pause** at the boundary noted below. The user gives the go signal in a separate
session.

### 2.1 Build order & dependencies

```
Weather → QR Toolkit → Todo → Agenda   ⟵ batch 1 (autopilot, FULL PAUSE after Agenda)
Dev Companion, Voice Journal            ⟵ batch 2 (later, separate)
```

Rationale: Weather and QR are independent (simplest first). Todo reuses the existing
ICS mechanism. **Agenda is last** because it merges the calendar + todo caches, so it
depends on both being present.

### 2.2 Autopilot shape

- One feature = one chain round (may split into sub-rounds if large).
- **Checkpoint after every feature**; **FULL PAUSE after Agenda** before batch 2.
- Per-round loop: build prompt → launch `coding-executor` → receive report →
  **verify-don't-copy** against the real artifact (build, tests, git log, TODO census)
  → sync canvas → decide next.
- Blocker taxonomy: **HARD** (stops the round) vs **SOFT 🟡** (stub & keep going,
  surface in report).

### 2.3 Feature specs

#### Feature 1 — Weather *(build first; simplest)*
- **API:** Open-Meteo (free, **no API key**).
- **Config (secrets.h):** `WEATHER_LAT`, `WEATHER_LON`, `WEATHER_LABEL`.
  Default location Kuala Lumpur: lat `3.1390`, lon `101.6869`.
- **Display:** current temperature, weather icon, feels-like, humidity, wind **+ 3-day
  forecast**.
- **Cache:** `/weather.json` on the active filesystem (SD or LittleFS).
- **Sync:** on-open if stale **+ manual refresh**. Reuse the calendar's WiFi/NTP/HTTPS
  + RAII-WiFi-off + portal-guard patterns.
- **Testable seam:** a header-only pure parser in `src/core/` (e.g. parse Open-Meteo
  JSON → struct), native Unity tests.

#### Feature 2 — QR Toolkit
- **Config (secrets.h):** QR payload definitions.
- **Capabilities:**
  - **WiFi QR** auto-generated from the existing creds (`WIFI_STA_SSID`/`WIFI_STA_PASS`).
  - **DuitNow QR:** decode to the raw EMVCo string **+ regenerate** it.
  - **URL / text QRs.**
- **UI:** tap cycles between QR types.
- **Testable seam:** EMVCo encode/decode + QR payload formatting in `src/core/`.

#### Feature 3 — Todo
- **Backend:** a dedicated **"Tasks" Google Calendar** fetched via the **existing ICS
  mechanism** — **zero new auth** (reuses `CAL_ICS_URL_n`).
- **Model:** tasks = **all-day events**.
- **Done-state:** stored **locally on SD** (not pushed back to Google).
- **Editing:** phone-editable (edit the Google Tasks calendar on the phone; device
  picks it up on next sync).
- **Testable seam:** task extraction from the ICS cache + done-state merge in
  `src/core/`.
- **⚠️ STATUS — DEFERRED / DISABLED (2026-08-02):** built, tested (31 native tests)
  and merged to `main` (82fd48a), but **commented out of the launcher**
  (`AppRegistry.h`, marker `TODO(TODO-BACKEND)`). Reason: **Google Tasks (the
  product) exposes NO ICS/CalDAV feed** — only an OAuth2 JSON API — so the
  "Tasks Google Calendar (all-day events)" source only suits **date-bounded**
  tasks, and the owner's tasks are mostly **undated**. Revisit with a backend
  that serves undated tasks (e.g. a self-hosted/3rd-party ICS bridge — "Option C",
  or the Tasks API via OAuth2 — "Option B", which breaks the zero-auth decision).
  Re-enable by uncommenting the two `TodoApp` lines in `AppRegistry.h`.

#### Feature 4 — Agenda *(closes batch 1; FULL PAUSE after)*
- **Rework the launcher into a split view:**
  - **Left** = app list.
  - **Right** = today's timeline **merging calendar + todo**, **next item highlighted**.
- **Implementation:** a thin **merge layer** over the two existing caches
  (`/calendar.json` + todo store). No new network code.
- **Testable seam:** the calendar+todo → ordered-timeline merge in `src/core/`.

#### Feature 5 — Dev Companion *(batch 2)*
- **Reference viewer:** full-screen pinouts/schematics stored on SD; **host-side Pillow
  conversion script** to prepare images; **fit-to-screen, no zoom**.
- **GitHub dashboard (read-only):** configurable **repo list in secrets.h** + **PAT**;
  show **open PRs + issues + last CI status**.

#### Feature 6 — Voice Journal *(batch 2; deferred)*
- **Hardware:** INMP441 I2S MEMS mic.
- **Flow:** hold-to-record **WAV → SD queue** → **nightly batch sync** to a **swappable
  backend** (self-hosted Whisper + Ollama on the user's PC, **or** cloud) → transcribe +
  reformat → **timestamped entries on SD**.
- **Hard constraint:** ESP32 **cannot** do on-device free-form STT — the device stays
  "dumb"; intelligence lives in the backend.
- **Privacy:** backend must stay swappable. Not always-on; user opens it nightly
  (queue + sync at night). Backend choice (self-hosted vs cloud) **deferred**.

---

## 3. Two-button navigation — **SEPARATE effort** (not part of the feature chain)

Locked design. Runs independently of the §2 feature chain (user decision).

| Control | Pin | Tap | Hold |
|---|---|---|---|
| **Button A** (Forward) | **GPIO 9** | Next / confirm / page-forward | Secondary / menu |
| **Button B** (Back) | **GPIO 46** | Back / up one level / prev page | **Home** (jump to launcher) |
| Onboard button (`SENSOP_VN`) | GPIO 21 | **Hidden backup "forward"** (kept so the device stays navigable if an external switch fails) | — |
| Hardware reset (`RST`) | EN pin | unchanged (not a GPIO) | — |

- **Middle button** (`STR_IO0` = GPIO0) is the e-paper `CFG_STR` line — **not usable**.
- **Wiring:** each switch is a **momentary normally-open tact switch**, one leg → GPIO,
  other → GND. `INPUT_PULLUP`, reuse the existing 30 ms debounce. **No extra parts.**
- **Firmware:** add `BTN_A`/`BTN_B` pins to `config.h`; `AppManager` polls both; add
  `ButtonEvent::Back` routed to the active app; **Home handled globally** via
  `AppManager::returnToLauncher()`. New pure seam
  `classifyBackButton(heldMs, debounceMs, homeHoldMs)`; `BTN_BACK_HOLD_MS ≈ 500`.
  Launcher: A-tap moves selection, A-hold activates, B = no-op (already home).
- **Bring-up:** verify GPIO 9 / 46 are broken out on the board header before permanent
  soldering; confirm with a button-diagnostic test.
- This **replaces** the single-button hold-timing gesture model for navigation.

---

## 4. Hardware shopping list

| Part | Spec | For |
|---|---|---|
| Battery | 3.7 V Li-Po **1S**, **JST-PH 2.0 mm 2-pin**, ~1500–2000 mAh, **with protection circuit** | Power |
| microSD | **16 GB, Class 10, FAT32** | Storage |
| Mic | **INMP441** I2S MEMS module | Voice Journal |
| Tact switches | **2× momentary normally-open** (6×6 mm or 12×12 mm) + caps | Two-button nav |
| Buzzer *(optional)* | **bare passive** buzzer (PWM-driven) — **NOT** the KY-012 module (that's active) | Audio cues |

---

## 5. Execution conventions & constraints

### 5.1 Environment (Windows / PowerShell)
- PlatformIO is invoked as **`python -m platformio …`** (bare `pio` is **not** on PATH).
- PowerShell uses **`;`** as a statement separator, **never `&&`**.
- Native tests need a host toolchain on PATH:
  `$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH` before `python -m platformio test -e native`.
- Build env: `lilygo_t5_47_s3`. Device on **COM7 @ 115200**.
- Board = ESP32-S3, 16 MB flash + 8 MB **octal** PSRAM (`qio_opi`). GPIO 26–37 are
  consumed internally by flash/PSRAM (unavailable).

### 5.2 Architecture
- **Header-only pure-logic seams** in `src/core/` (`namespace core`, **no HAL**) →
  native Unity-testable. Use **delegation-based seams** for testability.
- Apps live in `src/apps/<name>/`; register via one line in `AppRegistry.h`.
- Reuse existing patterns: WiFi/NTP/HTTPS + RAII-WiFi-off + portal-guard (from
  calendar), ArduinoJson caches on `BookStorage::fs()`.

### 5.3 Secrets
- `src/secrets.h` is **git-ignored**, `#include`d via `__has_include`. **Never commit
  it.** Holds WiFi creds, ICS URLs, and (new) weather coords, QR payloads, GitHub
  PAT/repos. Firmware must still build with no `secrets.h` (graceful empty state).

### 5.4 Coding-agent mandates (every generated prompt MUST encode these)
1. **Logical commits** — commit before finishing; split into a few logical commits
   (never one blob); meaningful conventional messages; **never commit secrets**.
2. **Summary report** — return what was built, key mechanics, verification runs
   performed, and any deviations from this brief.
3. **Human-gate flags** — placed **pre-flight** (before the prompt), each answering
   **Act / How / If-skipped** inline.

### 5.5 Autopilot protocol
- **Verify-don't-copy:** independently confirm the artifact (build, tests, `git log`,
  TODO census) rather than trusting the agent's report.
- Sync `docs/progress.canvas.tsx` at every checkpoint (dark theme, data/UI separation
  per the `progress-canvas` skill).
- Surface HARD vs SOFT 🟡 blockers; pause at the defined boundaries.

---

## 6. Open & deferred items

- **Execution timing:** starts in a separate session on the user's go signal.
- **Batch 2** (Dev Companion, Voice Journal) begins only after the post-Agenda pause.
- **Voice Journal backend** (self-hosted Whisper+Ollama vs cloud) — deferred.
- **Calendar MONTHLY/YEARLY recurrence** — 8 `TODO(R2)` deferred.
- **GPIO 9 / 46 physical availability** on the board header — confirm at bring-up.
- **Todo backend** — `TODO(TODO-BACKEND)`: Google Tasks has no ICS feed; the
  date-bounded "Tasks calendar" source doesn't fit undated tasks. App disabled in
  launcher (code + tests kept). Decide a backend (Option C bridge vs Option B
  OAuth2) before re-enabling.

---

## 7. Decisions log

| Date | Decision |
|---|---|
| — | 6-feature roadmap locked; build order Weather → QR → Todo → Agenda; autopilot; full pause after Agenda |
| — | Two-button nav locked: Button A = GPIO 9, Button B = GPIO 46, GPIO 21 = hidden backup forward; runs **separately** from the feature chain |
| — | Execution to be **started in a different session** by the user |
| 2026-08-02 | Todo **deferred & disabled in launcher** (`TODO(TODO-BACKEND)`): Google Tasks has no ICS/CalDAV feed (OAuth2 API only); the all-day-calendar source only suits date-bounded tasks. Code + 31 tests kept merged; revisit with an undated-task backend (Option C bridge / Option B OAuth2) |
