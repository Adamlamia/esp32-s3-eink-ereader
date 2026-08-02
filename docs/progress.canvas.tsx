import React from "react";

/**
 * ESP32-S3 E-Ink E-Reader — Build Dashboard (blueprint-style, comprehensive)
 * Source of truth: docs/PROJECT_BRIEF.md (locked 6-feature roadmap) + prior chain prompts
 * Synced by the prompt builder on: (a) prompt handover, (b) outcome-report arrival.
 * Verification timeline lists ONLY real, reported runs — never aspirational.
 *
 * TEMPLATE RULES (progress-canvas skill):
 * - Syncs edit ONLY the data blocks below. Never touch the UI section during a sync.
 * - Dark theme + inline styles only (Canvas preview has no Tailwind).
 * - Three tracks: chain = Batch-1 features (Weather active) · chainSecondary = CalendarApp (complete) · chainTertiary = Multi-App Framework (complete).
 */

type StepStatus = "done" | "active" | "handed-over" | "pending" | "blocked";

const C = {
  bg: "#0f1117", card: "#181b24", border: "#2a2f3d", text: "#e6e8ee",
  dim: "#8b93a7", accent: "#7dd3fc", done: "#4ade80", active: "#60a5fa",
  pending: "#6b7280", blocked: "#f87171", yellow: "#facc15",
};

// ── Status header ────────────────────────────────────────────────────────────
const meta = {
  status: "BATCH 2 IN PROGRESS · Dev Companion DONE & flashed · main 4a737d1 · 257 tests · Voice Journal next",
  updated: "2026-08-02",
  round: "Feature 6 (Voice Journal) scoping — backend decision deferred, hardware gate (INMP441)",
  nextHumanAction: "Decide Voice Journal backend (self-hosted vs cloud) + confirm INMP441 mic availability",
  branch: "main (4a737d1) · feature/todo + feature/qr kept",
};

// ── Locked tech stack (so decisions aren't re-litigated) ─────────────────────
const stack = [
  { layer: "MCU", choice: "ESP32-S3 (Arduino framework)" },
  { layer: "Build", choice: "PlatformIO (env: lilygo_t5_47_s3)" },
  { layer: "Display", choice: "LilyGo T5 4.7\" e-ink 960x540, 16 grayscale (LilyGo-EPD47)" },
  { layer: "Input", choice: "Single button GPIO21, 3 hold-band gestures" },
  { layer: "Storage", choice: "microSD (SPI) / LittleFS fallback" },
  { layer: "Network", choice: "WiFi AP + STA · ESPAsyncWebServer · HTTPClient (calendar sync)" },
  { layer: "Testing", choice: "Unity native (pio test -e native), header-only src/core seams" },
  { layer: "App Framework", choice: "App base + AppManager + SystemContext + AppRegistry" },
  { layer: "Calendar source", choice: "Google Calendar ICS secret URLs (one per calendar = category)" },
  { layer: "Calendar time", choice: "NTP · Asia/Kuala_Lumpur (UTC+8, no DST)" },
  { layer: "Weather source", choice: "Open-Meteo free API (no key) · KL defaults compiled in · /weather.json cache" },
];

// ── Milestones with done-criteria (from the source-of-truth doc) ─────────────
const milestones: { id: string; name: string; progress: number; status: string; rounds: string; done: string }[] = [
  { id: "M1", name: "Framework skeleton", progress: 100, status: "done", rounds: "FW·R1", done: "App.h + AppManager + AppRegistry compile, existing build unbroken" },
  { id: "M2", name: "Framework full implementation", progress: 100, status: "done", rounds: "FW·R2", done: "Launcher shows, e-reader works identically, main.cpp < 100 lines" },
  { id: "M3", name: "Framework unit tests", progress: 100, status: "done", rounds: "FW·R3", done: "pio test -e native passes with framework tests" },
  { id: "M4", name: "Framework review & hardening", progress: 100, status: "done", rounds: "FW·R4", done: "Zero Critical findings, deferred-work ledger compiled" },
  { id: "M5", name: "Calendar pure core + tests", progress: 100, status: "done", rounds: "CAL·R1", done: "ICS parser + date math + recurrence pass native tests, firmware still builds" },
  { id: "M6", name: "Calendar sync + UI", progress: 100, status: "done", rounds: "CAL·R2", done: "Events fetched from Google ICS, cached to SD, 3 views render on device" },
  { id: "M7", name: "Calendar scheduled sync", progress: 100, status: "done", rounds: "CAL·R3", done: "6 AM auto-sync + manual sync work, battery-sane" },
  { id: "M8", name: "Calendar review & hardening", progress: 100, status: "done", rounds: "CAL·R4", done: "Zero Critical findings, feature ready to merge to main" },
  { id: "M9", name: "Weather feature (batch 1 of 4)", progress: 95, status: "done", rounds: "WTH·R1–R2", done: "Open-Meteo sync + cache + UI on device, 0 Critical findings, merged to main, live-verified" },
  { id: "M10", name: "QR Toolkit (batch 2 of 4)", progress: 100, status: "done", rounds: "QR·R1–R2", done: "WiFi/DuitNow/URL QRs, tap cycles, EMVCo seam tested" },
  { id: "M11", name: "Todo (batch 3 of 4) — DEFERRED", progress: 95, status: "blocked", rounds: "TODO·R1–R2", done: "Built+tested+merged, but DISABLED in launcher (TODO(TODO-BACKEND)): Google Tasks has no ICS feed; revisit backend" },
  { id: "M12", name: "Agenda (batch 4 of 4 · FULL PAUSE after)", progress: 100, status: "done", rounds: "AGD·R1–R2", done: "Split-view launcher: left=apps, right=today's timeline (calendar-only, clean Todo slot), next-item highlighted; 0 Critical; flashed to device" },
  { id: "M13", name: "Dev Companion (batch 2 · Feature 5)", progress: 100, status: "done", rounds: "DEV·R1–R2", done: "Reference viewer (4-bpp blit from SD) + GitHub dashboard (PRs/issues/CI); 2 Critical fixed; 257 tests; flashed" },
  { id: "M14", name: "Voice Journal (batch 2 · Feature 6)", progress: 0, status: "pending", rounds: "VJ·Rx", done: "I2S WAV recording + nightly batch sync to swappable backend" },
];

// ── Prompt chain — ACTIVE track: Batch-1 features (Weather → QR → Todo → Agenda) ─
const chain: { r: string; pattern: string; scope: string; status: StepStatus; note: string }[] = [
  { r: "WTH·R1", pattern: "Implementation", scope: "Open-Meteo core seam + /weather.json store + sync (calendar lifecycle reuse) + WeatherApp UI + AppRegistry", status: "done", note: "3 commits (391535f/10d212b/47da9de) · 146 tests (+24) · RAM 15.0% Flash 31.4% · degraded-mode green · 3 TODO(WTH-R2)" },
  { r: "WTH·R2", pattern: "Review & Fix", scope: "CSPMO review of feature/weather, resolve TODO(WTH-R2) (shared WiFi/NTP helper, TLS CA), final verification", status: "done", note: "3 commits (2e9e313/1b044c8/b162914) · 151 tests (+5 regr) · 0 Critical · WifiSession extracted · TODO(TLS)×2 deferred" },
  { r: "QR·R1", pattern: "Implementation", scope: "core/Emvco.h + core/QrPayload.h seams + Unity tests + QR lib render + QrApp (tap cycles WiFi/DuitNow/URL) + AppRegistry", status: "done", note: "4 commits (717d13f/bdf35dc/f1864f0/4a7f3c4) · 182 tests (+31) · RAM 15.2% Flash 31.6% · degraded green · 12 negative tests · 0 TODO(QR-R2) · PM-verified" },
  { r: "QR·R2", pattern: "Review & Fix", scope: "CSPMO review of feature/qr, resolve any findings, final verification", status: "done", note: "3 commits (2103b42/b447f34/d0dc5fa) · 184 tests (+2 regr) · 0 Critical · Q1 Safety fixed (tag-63 reject) · M1 static_assert · PM-verified" },
  { r: "TODO·R1", pattern: "Implementation", scope: "core/TodoModel.h (ICS task extraction + done-state merge) + /todo.json store + TodoSync (reuse WifiSession+parseIcsFeed) + TodoApp checklist UI + AppRegistry", status: "done", note: "5 commits (b89b9f2/0fff32b/f23c385/6b975b7/6cd6249) · 214 tests (+30) · RAM 15.2% Flash 31.9% · degraded green · UID done-identity · shared parser extended (baseline safe) · PM-verified" },
  { r: "TODO·R2", pattern: "Review & Fix", scope: "CSPMO review of feature/todo, resolve findings, final verification", status: "done", note: "3 commits (7dc6d48/06f32c0/67bc292) · 215 tests (+1 regr) · 0 Critical · T1 phantom-task fix · shared-seam UID change verified additive · PM-verified" },
  { r: "AGD·R1", pattern: "Implementation", scope: "core/AgendaMerge.h seam + AppManager split-view launcher (calendar-only, clean Todo slot) + native tests + config.h AGENDA_* + README", status: "done", note: "4 commits (eed51b4/38db6bd/e91d4c9/ecf3d30) · 228 tests (+13) · RAM 19.7% Flash 31.7% · PM-verified" },
  { r: "AGD·R2", pattern: "Review & Fix", scope: "CSPMO review of Agenda feature, observability fix, capacity regression pin", status: "done", note: "3 commits (1170a33/5686f75/1efa36b) · 229 tests (+1 regr) · 0 Critical 0 Safety · A1 serial log + A5 capacity pin · PM-verified" },
];

// ── Secondary track: CalendarApp (COMPLETE, merged to main c75e50d) ──────────
const chainSecondary: { r: string; pattern: string; scope: string; status: StepStatus; note: string }[] = [
  { r: "CAL·R1", pattern: "Implementation", scope: "Pure core: CalendarEvent, CalendarDate, IcsParser, bounded recurrence + Unity tests", status: "done", note: "4 commits · 30 new tests (85 total) · build SUCCESS · 7 TODO(R2) refs" },
  { r: "CAL·R2", pattern: "Implementation", scope: "SD cache + WiFi/NTP/HTTP sync + CalendarApp UI + AppRegistry", status: "done", note: "6 commits · 96 tests (11 new store) · build+flash SUCCESS · live sync verified" },
  { r: "CAL·R3", pattern: "Implementation", scope: "Scheduled 6 AM sync + power management + manual sync", status: "done", note: "6 commits · 118 tests (22 new schedule) · build+flash · UTC bug fixed" },
  { r: "CAL·R4", pattern: "Review & Fix", scope: "CSPMO review, deferred-work ledger, final verification", status: "done", note: "7 commits · 122 tests (+4 regression) · 4C fixed · 5S fixed · merged main c75e50d" },
];

// ── Tertiary track: Multi-App Framework (COMPLETE, on main) ──────────────────
const chainTertiary: { r: string; pattern: string; scope: string; status: StepStatus; note: string }[] = [
  { r: "FW·R1", pattern: "Scaffolding", scope: "App.h, SystemContext.h, AppManager stubs, AppRegistry, ReaderApp stubs, config", status: "done", note: "7 files created, build SUCCESS, 3 commits" },
  { r: "FW·R2", pattern: "Implementation", scope: "AppManager logic, ReaderApp migration, main.cpp rewrite", status: "done", note: "Full framework live, flashed to device, 3 commits" },
  { r: "FW·R3", pattern: "Test Generation", scope: "ButtonClassify seam extraction, native unit tests", status: "done", note: "24 new tests, 56 total pass, seam extracted, 2 commits" },
  { r: "FW·R4", pattern: "Review & Fix", scope: "CSPMO review, deferred-work ledger, final verification", status: "done", note: "0 Critical, 2 Suggestion fixed, build+tests green, 1 commit" },
];

// ── Verification timeline — proven runs ONLY (from outcome reports) ──────────
const verificationTimeline: { when: string; round: string; what: string; result: string }[] = [
  { when: "2026-08-01", round: "FW·R1", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — 0 errors, RAM 14.3%, Flash 26.4%" },
  { when: "2026-08-01", round: "FW·R2", what: "pio run -t upload", result: "SUCCESS — flashed to device, 1110736 bytes" },
  { when: "2026-08-01", round: "FW·R3", what: "pio test -e native", result: "SUCCESS — 56 tests passed (32 existing + 24 new)" },
  { when: "2026-08-01", round: "FW·R4", what: "pio test -e native", result: "SUCCESS — 56 tests passed" },
  { when: "2026-08-01", round: "CAL·R1", what: "pio test -e native", result: "SUCCESS — 85 tests (30 new calendar + 55 existing)" },
  { when: "2026-08-01", round: "CAL·R1", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 14.3%, Flash 26.5%" },
  { when: "2026-08-01", round: "CAL·R2", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 14.3%, Flash 26.8% (degraded build, secrets absent)" },
  { when: "2026-08-01", round: "CAL·R2", what: "pio test -e native", result: "SUCCESS — 96/96 (85 R1 + 11 new store seam)" },
  { when: "2026-08-01", round: "CAL·R2", what: "pio run -t upload (COM7)", result: "SUCCESS — 1126304 bytes, boot serial clean" },
  { when: "2026-08-01", round: "CAL·R2", what: "live on-device sync", result: "BLOCKED — src/secrets.h absent (no WiFi creds / ICS URLs)" },
  { when: "2026-08-01", round: "CAL·R3", what: "pio test -e native", result: "SUCCESS — 118/118 (96 R2 + 22 new SyncSchedule)" },
  { when: "2026-08-01", round: "CAL·R3", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 14.3%, Flash 26.9%" },
  { when: "2026-08-01", round: "CAL·R3", what: "pio run -t upload (COM7)", result: "SUCCESS — flashed, no crash on monitor" },
  { when: "2026-08-01", round: "CAL·R3", what: "live on-device sync", result: "BLOCKED — src/secrets.h still absent" },
  { when: "2026-08-01", round: "CAL·R4", what: "pio test -e native", result: "SUCCESS — 122/122 (118 R3 + 4 regression tests)" },
  { when: "2026-08-01", round: "CAL·R4", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 14.3%, Flash 26.9% (+20 B)" },
  { when: "2026-08-01", round: "CAL·R4", what: "live on-device sync (after portal-off fix)", result: "SUCCESS — user confirmed events fetched from Google Calendar" },
  { when: "2026-08-01", round: "WTH·R1", what: "pio test -e native (baseline)", result: "SUCCESS — 122/122 (pre-weather)" },
  { when: "2026-08-01", round: "WTH·R1", what: "pio test -e native (final, PM re-run)", result: "SUCCESS — 146/146 (122 + 24 new weather) · verified independently by PM" },
  { when: "2026-08-01", round: "WTH·R1", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 15.0%, Flash 31.4% (1318037 B)" },
  { when: "2026-08-01", round: "WTH·R1", what: "degraded build (secrets.h renamed away)", result: "SUCCESS — native 146/146 + firmware RAM 14.3% Flash 27.1%; secrets restored, no commits while renamed" },
  { when: "2026-08-01", round: "WTH·R1", what: "negative tests (truncated body / corrupt cache / URL overflow)", result: "SUCCESS — 3/3 guards fail loudly as designed" },
  { when: "2026-08-01", round: "WTH·R1", what: "pio run -t upload (COM7, optional)", result: "SKIPPED — device not in download mode (needs physical BOOT hold); non-blocking" },
  { when: "2026-08-01", round: "WTH·R2", what: "pio test -e native (PM re-run)", result: "SUCCESS — 151/151 (146 R1 + 5 regression) · verified independently by PM" },
  { when: "2026-08-01", round: "WTH·R2", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 15.0%, Flash 31.4%" },
  { when: "2026-08-01", round: "WTH·R2", what: "degraded build (no secrets.h)", result: "SUCCESS — native 151/151 + firmware RAM 14.3% Flash 27.1%" },
  { when: "2026-08-01", round: "WTH·R2", what: "negative test (W1 fix reverted)", result: "SUCCESS — 3 tests FAIL without fix, green with it" },
  { when: "2026-08-02", round: "QR·R1", what: "pio test -e native (PM re-run)", result: "SUCCESS — 182/182 (151 + 31 new QR) · verified independently by PM" },
  { when: "2026-08-02", round: "QR·R1", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 15.2%, Flash 31.6% (1326429 B)" },
  { when: "2026-08-02", round: "QR·R1", what: "degraded build (secrets.h renamed away)", result: "SUCCESS — native 182/182 + firmware build; secrets restored, no commits while renamed" },
  { when: "2026-08-02", round: "QR·R1", what: "negative tests (bad CRC / oversize / truncated / non-hex)", result: "SUCCESS — 12 guards fail loudly as designed; caught 5 real seam bugs, fixed" },
  { when: "2026-08-02", round: "QR·R1", what: "on-device render (COM7)", result: "BLOCKED — parallel build phase; deferred to serial integration" },
  { when: "2026-08-02", round: "QR·R2", what: "pio test -e native (PM re-run)", result: "SUCCESS — 184/184 (182 + 2 regression) · verified independently by PM" },
  { when: "2026-08-02", round: "QR·R2", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 15.2%, Flash 31.6%" },
  { when: "2026-08-02", round: "QR·R2", what: "degraded build (secrets.h renamed away)", result: "SUCCESS — native 184/184 + firmware RAM 14.5% Flash 27.3%; secrets restored, no commits while renamed" },
  { when: "2026-08-02", round: "QR·R2", what: "negative proof (Q1 tag-63 guard reverted)", result: "SUCCESS — test_build_rejects_caller_supplied_crc_tag FAILS without fix, green with it" },
  { when: "2026-08-02", round: "QR·R2", what: "CSPMO review", result: "SUCCESS — 0 Critical, 1 Safety fixed (Q1), 1 Suggestion fixed (M1), 10 dims correct" },
  { when: "2026-08-02", round: "TODO·R1", what: "pio test -e native (PM re-run)", result: "SUCCESS — 214/214 (184 + 30 new todo) · verified independently by PM; shared-parser UID change kept baseline green" },
  { when: "2026-08-02", round: "TODO·R1", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 15.2%, Flash 31.9% (1338033 B)" },
  { when: "2026-08-02", round: "TODO·R1", what: "degraded build (secrets.h renamed away)", result: "SUCCESS — native 214/214 + firmware RAM 14.5% Flash 27.6%; secrets restored, no commits while renamed" },
  { when: "2026-08-02", round: "TODO·R1", what: "negative tests (timed-event exclude / corrupt store / full done-set / truncation)", result: "SUCCESS — guards fail loudly as designed" },
  { when: "2026-08-02", round: "TODO·R1", what: "live sync + on-device (COM7)", result: "BLOCKED — parallel phase + TODO_ICS_URL absent (soft); deferred" },
  { when: "2026-08-02", round: "TODO·R2", what: "pio test -e native (PM re-run)", result: "SUCCESS — 215/215 (214 + 1 regression) · verified independently by PM" },
  { when: "2026-08-02", round: "TODO·R2", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 15.2%, Flash 31.9%" },
  { when: "2026-08-02", round: "TODO·R2", what: "degraded build (secrets.h renamed away)", result: "SUCCESS — native 215/215 + firmware RAM 14.5% Flash 27.6%; secrets restored, no commits while renamed" },
  { when: "2026-08-02", round: "TODO·R2", what: "negative proof (T1 fix stashed)", result: "SUCCESS — test_cache_skips_phantom_task_elements FAILS (Expected 1 Was 6) without fix, green with it" },
  { when: "2026-08-02", round: "TODO·R2", what: "CSPMO review + shared-seam audit", result: "SUCCESS — 0 Critical, 1 Suggestion fixed (T1); CalendarEvent.uid + IcsParser UID capture verified additive (baseline green)" },
  { when: "2026-08-02", round: "MERGE", what: "git merge --no-ff feature/todo → main 82fd48a", result: "SUCCESS — main re-run 215/215 tests + firmware build RAM 15.2% Flash 31.9%; branch kept" },
  { when: "2026-08-02", round: "FLASH", what: "pio run -e lilygo_t5_47_s3 -t upload (COM7)", result: "SUCCESS — hash verified, hard reset; serial port live. On-screen QR/Todo smoke-test = user to confirm" },
  { when: "2026-08-02", round: "AGD·R1", what: "pio test -e native (PM re-run)", result: "SUCCESS — 228/228 (215 + 13 new agenda) · verified independently by PM" },
  { when: "2026-08-02", round: "AGD·R1", what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — RAM 19.7%, Flash 31.7%" },
  { when: "2026-08-02", round: "AGD·R2", what: "pio test -e native (PM re-run)", result: "SUCCESS — 229/229 (228 + 1 regression) · verified independently by PM" },
  { when: "2026-08-02", round: "AGD·R2", what: "CSPMO review", result: "SUCCESS — 0 Critical, 0 Safety; A1 Observability fixed (serial log), A5 capacity pin added" },
  { when: "2026-08-02", round: "AGD·FLASH", what: "pio run -e lilygo_t5_47_s3 -t upload (COM7)", result: "SUCCESS — hash verified, hard reset; Agenda split-view launcher on device" },
  { when: "2026-08-01", round: "WTH·R2", what: "marker census", result: "TODO(WTH-R2)=0 · TODO(TLS)=2 · TODO(R2)=8 unchanged" },
];

// ── Marker ledger summary (open TODO(marker) counts, from latest report) ─────
const markerLedger: { marker: string; open: number | null; note: string }[] = [
  { marker: "TODO(R2)", open: 8, note: "7 src + 1 test — inherited calendar MONTHLY/YEARLY recurrence; PM-verified unchanged on feature/weather" },
  { marker: "TODO(R3)", open: 0, note: "All 3 resolved in CAL·R3" },
  { marker: "TODO(R4)", open: 0, note: "All calendar R4 findings closed" },
  { marker: "TODO(WTH-R2)", open: 0, note: "All 3 resolved in WTH·R2: 2 → WifiSession extraction, 1 → reclassified TODO(TLS)" },
  { marker: "TODO(TLS)", open: 3, note: "CalendarSync + WeatherSync + TodoSync — replace setInsecure() with CA validation; deferred to shared hardening round (TodoSync extended the tag in TODO·R1)" },
  { marker: "TODO(TODO-BACKEND)", open: 1, note: "AppRegistry.h — Todo disabled in launcher pending a backend that serves undated tasks (Google Tasks has no ICS feed). Re-enable by uncommenting the two TodoApp lines" },
  { marker: "TODO(QR-R2)", open: 0, note: "QR·R2 review-closed: Q1 Safety + M1 Suggestion fixed & pinned; 0 Critical open; on-device scan BLOCKED (process gate, no code marker)" },
  { marker: "TODO(TODO-R2)", open: 0, note: "TODO·R2 review-closed: T1 phantom-task fix pinned; 0 Critical open; shared-seam UID change audited additive; live sync BLOCKED (process + soft gate)" },
];

// ── Open decisions (deliberate, not accidental) ──────────────────────────────
const decisions: { decision: string; status: string; note: string }[] = [
  { decision: "Boot to launcher (not resume last app)", status: "Resolved", note: "User chose 'always show home screen first' (planning 2026-08)" },
  { decision: "Back-to-home via app-level menu", status: "Resolved", note: "LongHold opens app menu, 'Back to Home' calls requestHome()" },
  { decision: "Compile-time app registration", status: "Resolved", note: "AppRegistry.h — one line per branch, additive merges" },
  { decision: "Host GCC for native tests", status: "Resolved", note: "MSYS2 + mingw-w64-x86_64-gcc 16.1.0 via winget (2026-08-01)" },
  { decision: "Calendar: ICS secret URLs (not OAuth2)", status: "Resolved", note: "Identical glanceable result on grayscale e-ink; no token expiry/fragility (2026-08-01)" },
  { decision: "Calendar: 3–4 calendars = categories", status: "Resolved", note: "One secret URL per calendar; event tagged by feed index (2026-08-01)" },
  { decision: "Calendar: tap cycles Today→3-day→Week", status: "Resolved", note: "Hold = menu (Sync now / Back to Home) (2026-08-01)" },
  { decision: "Calendar: Monday-start week", status: "Resolved", note: "Sunday belongs to previous week (2026-08-01)" },
  { decision: "Calendar: 6 AM sync + boot + manual", status: "Resolved", note: "NTP via Asia/Kuala_Lumpur; timer-wakeup while asleep (2026-08-01)" },
  { decision: "Calendar: fixed UTC+8, no DST/TZID engine", status: "Resolved", note: "Malaysia has no DST; floating times treated at fixed offset (2026-08-01)" },
  { decision: "Calendar seams may include config.h", status: "Resolved", note: "R1 deviation: CalendarEvent.h is app-specific (CAL_TITLE_MAX); config.h is a pure macro header, host-testability preserved (2026-08-01)" },
  { decision: "Sync runs on a dedicated 24KB FreeRTOS task", status: "Resolved", note: "R2: 8KB loopTask stack can't hold parseIcsFeed 8KB buffer + mbedTLS; blocking to UI, self-deleting task (2026-08-01)" },
  { decision: "Sync is portal-guarded, STA-only, RAII WiFi-off", status: "Resolved", note: "R2: aborts if WebPortal active (avoids WiFi.mode conflict); always restores WIFI_OFF (2026-08-01)" },
  { decision: "Cache = /calendar.json + pure serialize seam", status: "Resolved", note: "R2: ArduinoJson on storage.fs(); host-testable core::serialize/deserializeCalendarCache seam (11 tests) (2026-08-01)" },
  { decision: "UTC fix: time() is already UTC, no offset subtraction", status: "Resolved", note: "R3: configTime gmtOffset only steers localtime(); removed erroneous -CAL_TZ_OFFSET_SEC from staNtp() (2026-08-01)" },
  { decision: "Generic App::sleepWakeupSec() hook (min-over-apps)", status: "Resolved", note: "R3: AppManager arms timer to min of all apps' wakeup; default -1 preserves button-only for other apps (2026-08-01)" },
  { decision: "Debounce latch prevents sync re-trigger within same hour", status: "Resolved", note: "R3: lastSyncUtc updated immediately on sync start; shouldAutoSync won't re-fire (2026-08-01)" },
  { decision: "VALUE=DATE exact match (not DATE-TIME)", status: "Resolved", note: "R4 C1: strstr matched VALUE=DATE-TIME as all-day; now checks terminator char (2026-08-01)" },
  { decision: "civilFromDays year restoration for Jan/Feb", status: "Resolved", note: "R4 C4: Hinnant inverse missing y+=(m<=2); Jan/Feb dates had year-1 (2026-08-01)" },
  { decision: "Weekly BYDAY anchored at DTSTART time-of-day", status: "Resolved", note: "R4 C3: was emitting at midnight; now preserves original event time (2026-08-01)" },
  { decision: "ICS body size pre-check before allocation", status: "Resolved", note: "R4 S6: http.getSize() vs SYNC_BODY_MAX rejects oversized before getString() (2026-08-01)" },
  { decision: "Weather: Open-Meteo (no API key) + KL defaults compiled in", status: "Resolved", note: "WTH·R1: #ifndef-guarded WEATHER_LAT/LON/LABEL = 3.1390/101.6869/'Kuala Lumpur'; firmware fully functional with zero weather secrets (2026-08-01)" },
  { decision: "Weather: daily temps as int16_t tenths-of-°C", status: "Resolved", note: "WTH·R1: compact integer cache JSON (251 == 25.1 °C); current keeps API floats (2026-08-01)" },
  { decision: "Weather: WEATHER_URL_MAX = 320 (not 192)", status: "Resolved", note: "WTH·R1 deviation: full Open-Meteo URL ≈ 270 chars; 192 would never fit. Documented in config.h (2026-08-01)" },
  { decision: "Weather: no scheduled sync, on-open stale check only", status: "Resolved", note: "WTH·R1: 3 h stale threshold + manual tap-refresh; no sleepWakeupSec — radio not worth waking for weather (2026-08-01)" },
  { decision: "Weather reuses CAL_CLOCK_MIN_EPOCH + core::CalendarDate", status: "Resolved", note: "WTH·R1: no constant duplication; WEATHER_TZ UTC+8 == CAL_TZ_OFFSET_SEC makes weekday reuse safe (2026-08-01)" },
  { decision: "Shared WiFi/NTP/trampoline helper deferred to WTH·R2", status: "Resolved", note: "WTH·R2: extracted src/app/WifiSession.{h,cpp}; calendar behaviour preserved (verbatim move, log tag parameterised); portal-guard inline (2026-08-01)" },
  { decision: "TLS CA validation deferred to shared hardening round", status: "Deferred", note: "WTH·R2 W3: setInsecure() inherited from live-verified calendar; brief doesn't mandate; Open-Meteo carries no credential; TODO(TLS)×2 (2026-08-01)" },
  { decision: "Weather cache deserializer mirrors live-parser sanitisation", status: "Resolved", note: "WTH·R2 W1: defence-in-depth on read path; pinned by 5 regression tests (2026-08-01)" },
  { decision: "Parallel git worktrees per feature branch", status: "Resolved", note: "Discovered WTH·R1: E:/Project/ereader-{qr,todo,weather} worktrees off main; PM uses them for native-test verification (2026-08-01)" },
  { decision: "Weather UI/UX enhancement deferred", status: "Deferred", note: "User confirmed functional but wants a dedicated UI/UX pass later; feature/weather branch kept for this (2026-08-01)" },
  { decision: "QR lib = ricmoo/QRCode (Nayuki qrcodegen, MIT)", status: "Resolved", note: "QR·R1: tiny, firmware-env-only (native stays HAL-free); ECC LOW + ISO 18004 byte-mode capacity guard because lib silently corrupts over-capacity data (2026-08-02)" },
  { decision: "EMVCo CRC16-CCITT (0x1021, init 0xFFFF) over tag-63 header", status: "Resolved", note: "QR·R1: pinned by canonical check value 0x29B1 + DuitNow fixture cross-computed with independent Python impl; public-spec, reversible code — not a payment-compliance gate (2026-08-02)" },
  { decision: "QR payload never drawn on screen (label + kind caption only)", status: "Resolved", note: "QR·R1: WiFi password stays secrets.h → RAM → bitmap; only the QR image + label render, never the plaintext payload (2026-08-02)" },
  { decision: "QR render box = 420 px (not ~440)", status: "Resolved", note: "QR·R1 deviation: 540 px panel can't fit 440 + label + caption + footer without overlap; 420 is the max that keeps all elements on-screen (2026-08-02)" },
  { decision: "emvcoBuild rejects caller-supplied tag-63", status: "Resolved", note: "QR·R2 Q1 (Safety): a caller tag-63 made a duplicate CRC trailer that parse rejects, breaking lossless round-trip; build now refuses it loudly, pinned by regression test (2026-08-02)" },
  { decision: "WiFi payload no-truncation pinned by static_assert", status: "Resolved", note: "QR·R2 M1: QR_WIFI_QR_MAX(192) <= QR_PAYLOAD_MAX(320) compile-time assert; honest fix after a dead-guard hypothesis was disproven by negative-proof discipline (2026-08-02)" },
  { decision: "Todo done-identity = ICS UID (fallback d<day>#<title>)", status: "Resolved", note: "TODO·R1: extended shared CalendarEvent + parseIcsFeed to capture UID (bounded CAL_UID_MAX=96); rename on phone keeps done-state, deletion prunes; 184-test baseline stayed green (2026-08-02)" },
  { decision: "Todo gestures: MediumHold=toggle done, LongHold=menu", status: "Resolved", note: "TODO·R1: LongHold-as-menu is the universal house convention; MediumHold is the secondary-action band; Tap moves highlight (2026-08-02)" },
  { decision: "/todo.json stores tasks + done-set + sync stamp", status: "Resolved", note: "TODO·R1: UI must render across reboots and Agenda will read this cache; done-only seam still delivered + unit-tested independently (2026-08-02)" },
  { decision: "deserializeTodoCache skips phantom task elements", status: "Resolved", note: "TODO·R2 T1: corrupt/adversarial tasks[] entries (non-object / field-less) no longer materialise (untitled) rows; defence-in-depth pinned by regression test (2026-08-02)" },
  { decision: "Todo DEFERRED & disabled in launcher", status: "Deferred", note: "2026-08-02 user decision: Google Tasks (product) has NO ICS/CalDAV feed (OAuth2 API only); the all-day-calendar source only suits date-bounded tasks, owner's are mostly undated. Code+31 tests kept merged; commented out of AppRegistry (TODO(TODO-BACKEND)); reflash removed it from launcher. Revisit Option C (ICS bridge) vs Option B (OAuth2) (2026-08-02)" },
];

// ── Risks & mitigations ──────────────────────────────────────────────────────
const risks: { risk: string; impact: string; mitigation: string }[] = [
  { risk: "ICS recurrence complexity", impact: "Missed events if unsupported RRULE", mitigation: "CAL·R1 supports DAILY/WEEKLY+BYDAY; others skipped with TODO(R2) marker" },
  { risk: "ESP32 has no battery-backed RTC", impact: "Time lost on full power-off", mitigation: "NTP re-sync on boot/WiFi; light sleep keeps timer alive" },
  { risk: "ICS secret URL is a credential", impact: "Leak = calendar readable", mitigation: "Lives in git-ignored src/secrets.h only; never committed" },
  { risk: "E-ink ghosting on launcher", impact: "Visual quality", mitigation: "drawLauncher uses flush(true) full refresh (mitigated FW·R2)" },
  { risk: "WiFi mode conflict (WebPortal AP_STA vs calendar STA-only sync)", impact: "Sync fails/clashes if portal active", mitigation: "MITIGATED R2: sync aborts while portal running + RAII WIFI_OFF; on-screen message" },
  { risk: "Launcher background sync gap (S5)", impact: "Timer wake from launcher doesn't sync (no active app onLoop)", mitigation: "OPEN: requires background-tick hook in AppManager; seed for future round" },
  { risk: "Weather HTTPS uses setInsecure() (no CA validation)", impact: "MITM possible on open Wi-Fi", mitigation: "OPEN TODO(WTH-R2): inherit calendar's pattern, fix both apps together in Review & Fix" },
  { risk: "Weather UI layout untested on real panel", impact: "Coordinates may need tuning", mitigation: "RESOLVED: user confirmed layout works on device; UI/UX enhancement deferred to a future round" },
  { risk: "Live weather fetch unverified on device", impact: "Network path proven only by calendar parity", mitigation: "RESOLVED: user live-verified 2026-08-01 (3-day forecast + refresh confirmed)" },
];

// ── Definition of done (release-level checklist from the source doc) ─────────
const doneChecklist: { item: string; done: boolean }[] = [
  { item: "Multi-app framework on main (launcher + AppManager)", done: true },
  { item: "E-reader works identically after migration", done: true },
  { item: "Native unit tests green (56 passing)", done: true },
  { item: "Firmware flashed & verified on device", done: true },
  { item: "Calendar pure core + tests green", done: true },
  { item: "Calendar syncs from Google Calendar (ICS)", done: true },
  { item: "Calendar 3 views render on device", done: true },
  { item: "Calendar 6 AM auto-sync + manual sync (code-complete)", done: true },
  { item: "Calendar review: 0 Critical findings open", done: true },
  { item: "Calendar feature merged to main via PR", done: true },
  { item: "Weather pure core seam + tests green (146 passing)", done: true },
  { item: "Weather sync + cache + UI code-complete on feature/weather", done: true },
  { item: "Weather degraded-mode build (no secrets) green", done: true },
  { item: "Weather review: 0 Critical findings open (WTH·R2)", done: true },
  { item: "Weather shared WiFi/NTP helper extracted (WifiSession)", done: true },
  { item: "Weather live fetch verified on device", done: true },
  { item: "Weather feature merged to main", done: true },
];

// ── Human gates (Act · How · If-skipped) + agent-cleared checks ──────────────
const clearedChecks: { name: string; verified: string }[] = [
  { name: "Architecture design approved", verified: "2026-08-01 by user; confirmed in planning session" },
  { name: "Firmware flashed to device", verified: "2026-08-01; FW·R2 upload SUCCESS (1110736 bytes)" },
  { name: "Host GCC toolchain", verified: "2026-08-01; MSYS2 mingw-w64-gcc 16.1.0 installed, 56 tests green" },
  { name: "Calendar R1 commits verified", verified: "2026-08-01 by PM; git log confirms 3ac5a66/17107ba/bf5e3e4/6b71f09 on feature/calendar" },
  { name: "Calendar R1 marker census verified", verified: "2026-08-01 by PM; git grep TODO(R2) = 7 (2 actionable + 5 refs), matches report" },
  { name: "Calendar R2 commits verified", verified: "2026-08-01 by PM; git log confirms 9839cac/b984a58/5af09e0/8d2cbd5/71dfecf/8834e9e on feature/calendar" },
  { name: "Calendar R2 marker census verified", verified: "2026-08-01 by PM; git grep TODO(R2)=8 (7 src+1 test), TODO(R3)=3, matches report" },
  { name: "Calendar R3 commits verified", verified: "2026-08-01 by PM; git log confirms bccf830/847c288/f040362/109a86e/a6ded7c/70f6f2d on feature/calendar" },
  { name: "Calendar R3 marker census verified", verified: "2026-08-01 by PM; git grep TODO(R3)=0 (all resolved), TODO(R2)=8 (inherited), matches report" },
  { name: "Calendar R4 commits verified", verified: "2026-08-01 by PM; git log confirms cc148af/d721d77/4b2d1c5/9462782/21a0e02/fe45bb8/7376822 on feature/calendar" },
  { name: "Calendar R4 census verified", verified: "2026-08-01 by PM; TODO(R4)=0, TODO(R2)=8 (deferred ledger), 23 commits ahead of main" },
  { name: "Calendar merged to main", verified: "2026-08-01; c75e50d on main (--no-ff merge of feature/calendar)" },
  { name: "Weather R1 commits verified", verified: "2026-08-01 by PM; git log confirms 391535f/10d212b/47da9de on feature/weather, main untouched at c75e50d" },
  { name: "Weather R1 test count verified", verified: "2026-08-01 by PM; independent pio test -e native re-run = 146/146 PASSED (matches report)" },
  { name: "Weather R1 marker census verified", verified: "2026-08-01 by PM; git grep TODO(WTH-R2)=3 (WeatherSync.h:1 + .cpp:2), TODO(R2)=8 inherited unchanged" },
  { name: "Weather KL defaults = no human gate", verified: "2026-08-01 by PM; #ifndef guards make firmware functional with zero weather secrets" },
  { name: "Weather R2 commits verified", verified: "2026-08-01 by PM; git log confirms 2e9e313/1b044c8/b162914 on feature/weather (6 total ahead of main)" },
  { name: "Weather R2 test count verified", verified: "2026-08-01 by PM; independent pio test -e native re-run = 151/151 PASSED" },
  { name: "Weather R2 census verified", verified: "2026-08-01 by PM; TODO(WTH-R2)=0, TODO(TLS)=2, TODO(R2)=8 unchanged" },
  { name: "Weather live fetch verified on device", verified: "2026-08-01 by user; 3-day forecast, temp, humidity, wind all render; manual refresh works" },
  { name: "Weather merged to main", verified: "2026-08-01; 6666873 (--no-ff merge, 16 files, +2040/-121)" },
  { name: "QR feature/qr worktree ff'd to main", verified: "2026-08-02 by PM; git merge --ff-only main → 6666873, secrets.h present" },
  { name: "QR R1 commits verified", verified: "2026-08-02 by PM; git log confirms 717d13f/bdf35dc/f1864f0/4a7f3c4 on feature/qr, 4 ahead of main" },
  { name: "QR R1 test count verified", verified: "2026-08-02 by PM; independent pio test -e native re-run = 182/182 PASSED (matches report)" },
  { name: "QR R1 census + secrets hygiene verified", verified: "2026-08-02 by PM; TODO(QR-R2)=0, TODO(R2)=8, TODO(TLS)=2; git log --all -- src/secrets.h empty (never committed)" },
  { name: "QR R2 commits verified", verified: "2026-08-02 by PM; git log confirms 2103b42/b447f34/d0dc5fa on feature/qr, 7 ahead of main" },
  { name: "QR R2 test count verified", verified: "2026-08-02 by PM; independent pio test -e native re-run = 184/184 PASSED (matches report)" },
  { name: "QR R2 census + secrets hygiene verified", verified: "2026-08-02 by PM; TODO(QR-R2)=0 code markers, tree clean, secrets.h never committed" },
  { name: "QR merged to main", verified: "2026-08-02; ce6b413 (--no-ff merge, 12 files, +1674); main re-run 184/184 tests green" },
  { name: "Todo feature/todo worktree ff'd to main", verified: "2026-08-02 by PM; git merge --ff-only main → ce6b413, secrets.h present" },
  { name: "Todo R1 commits verified", verified: "2026-08-02 by PM; git log confirms b89b9f2/0fff32b/f23c385/6b975b7/6cd6249 on feature/todo, 5 ahead of main" },
  { name: "Todo R1 test count verified", verified: "2026-08-02 by PM; independent pio test -e native re-run = 214/214 PASSED (matches report)" },
  { name: "Todo R1 census + secrets hygiene verified", verified: "2026-08-02 by PM; TODO(TODO-R2)=0, TODO(TLS)=3, TODO(R2)=8; git log --all -- src/secrets.h empty; tree clean" },
  { name: "Todo R2 commits verified", verified: "2026-08-02 by PM; git log confirms 7dc6d48/06f32c0/67bc292 on feature/todo, 8 ahead of main" },
  { name: "Todo R2 test count verified", verified: "2026-08-02 by PM; independent pio test -e native re-run = 215/215 PASSED (matches report)" },
  { name: "Todo R2 staging cleanup corrected", verified: "2026-08-02 by PM; report claimed staging deleted but empty .todo-staging dir remained in primary workspace — PM removed it" },
  { name: "Todo merged to main", verified: "2026-08-02; 82fd48a (--no-ff merge, 14 files, +2141); main re-run 215/215 tests + firmware build SUCCESS (RAM 15.2% Flash 31.9%); feature/todo branch kept" },
  { name: "Firmware flashed to device (QR+Todo)", verified: "2026-08-02; pio -t upload SUCCESS on COM7 (hash verified, hard reset); serial port live. On-screen app smoke-test pending user confirmation" },
  { name: "Agenda R1 commits verified", verified: "2026-08-02 by PM; git log confirms eed51b4/38db6bd/e91d4c9/ecf3d30, 228/228 tests, no secrets" },
  { name: "Agenda R2 commits verified", verified: "2026-08-02 by PM; git log confirms 1170a33/5686f75/1efa36b, 229/229 tests, 0 Critical" },
  { name: "Agenda flashed to device (batch-1 final)", verified: "2026-08-02; pio -t upload SUCCESS on COM7 (hash verified); split-view launcher live" },
];

const gates: { icon: string; name: string; act: string; how: string; ifSkipped: string }[] = [
  {
    icon: "🟡",
    name: "Tasks-calendar ICS URL for Todo live sync",
    act: "during TODO·R1 (optional; only needed for LIVE on-device sync, not for code/tests)",
    how: "in src/secrets.h add: #define TODO_ICS_URL \"https://calendar.google.com/calendar/ical/.../basic.ics\" (+ optional #define TODO_ICS_LABEL \"Tasks\") pointing at a dedicated Tasks Google calendar",
    ifSkipped: "executor stubs behind the real interface (empty state / fails loudly); live sync marked BLOCKED until filled — code + native tests unaffected",
  },
  {
    icon: "🟢",
    name: "Custom weather location (optional)",
    act: "no action needed — KL defaults compiled in; only if user wants a different city, any round",
    how: "in src/secrets.h add: #define WEATHER_LAT <float> / WEATHER_LON <float> / WEATHER_LABEL \"<name>\"",
    ifSkipped: "Kuala Lumpur is used — feature is fully functional out of the box",
  },
];

// ── UI ── (generic render — DO NOT EDIT during syncs) ────────────────────────
const statusColor = (s: string) =>
  s === "done" ? C.done : s === "active" || s === "handed-over" ? C.active : s === "blocked" ? C.blocked : C.pending;

const decisionColor = (s: string) =>
  s.startsWith("Resolved") ? C.done : s.startsWith("Open") ? C.yellow : C.dim;

const Section = ({ title, children }: { title: string; children: React.ReactNode }) => (
  <div style={{ background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, padding: 16, marginBottom: 16 }}>
    <div style={{ color: C.accent, fontSize: 13, fontWeight: 700, letterSpacing: 1, textTransform: "uppercase", marginBottom: 12 }}>{title}</div>
    {children}
  </div>
);

const Th = ({ children, w }: { children: React.ReactNode; w?: number }) => (
  <th style={{ padding: "4px 8px", width: w }}>{children}</th>
);
const Td = ({ children, color, bold }: { children: React.ReactNode; color?: string; bold?: boolean }) => (
  <td style={{ padding: "4px 8px", color: color ?? C.text, fontWeight: bold ? 600 : 400 }}>{children}</td>
);

// Shared chain table — reused for every parallel track so render stays generic.
const ChainTable = ({ rows }: { rows: typeof chain }) => (
  <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 12 }}>
    <thead>
      <tr style={{ color: C.dim, textAlign: "left" }}>
        <Th w={34}>#</Th><Th w={110}>Pattern</Th><Th>Scope</Th><Th w={100}>Status</Th><Th>Note</Th>
      </tr>
    </thead>
    <tbody>
      {rows.map((s) => (
        <tr key={s.r} style={{ borderTop: `1px solid ${C.border}` }}>
          <Td color={C.dim}>{s.r}</Td>
          <Td>{s.pattern}</Td>
          <Td>{s.scope}</Td>
          <Td color={statusColor(s.status)} bold>{s.status}</Td>
          <Td color={C.dim}>{s.note}</Td>
        </tr>
      ))}
    </tbody>
  </table>
);

export default function ProgressDashboard() {
  return (
    <div style={{ background: C.bg, color: C.text, minHeight: "100vh", padding: 24, fontFamily: "ui-sans-serif, system-ui, sans-serif" }}>

      {/* Status header — blueprint-style */}
      <div style={{ marginBottom: 20 }}>
        <div style={{ fontSize: 22, fontWeight: 800 }}>📊 {"ESP32-S3 E-Ink E-Reader"} — Build Dashboard</div>
        <div style={{ color: C.dim, fontSize: 12, marginTop: 4 }}>
          Source of truth: {"docs/ROADMAP.md + framework plan + calendar prompts"} · Last updated {meta.updated}
        </div>
        <div style={{ display: "flex", gap: 10, marginTop: 10, flexWrap: "wrap" }}>
          {[
            { k: "Status", v: meta.status },
            { k: "Chain round", v: meta.round },
            { k: "Branch", v: meta.branch },
            { k: "Your next action", v: meta.nextHumanAction },
          ].map((b) => (
            <div key={b.k} style={{ background: C.card, border: `1px solid ${C.border}`, borderRadius: 8, padding: "6px 12px", fontSize: 12 }}>
              <span style={{ color: C.dim }}>{b.k}: </span>
              <span style={{ color: b.k === "Your next action" ? C.yellow : C.text, fontWeight: 600 }}>{b.v}</span>
            </div>
          ))}
        </div>
      </div>

      <Section title="Milestones (build phases → chain rounds)">
        {milestones.map((m) => (
          <div key={m.id} style={{ marginBottom: 12 }}>
            <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
              <div style={{ width: 30, fontWeight: 700, color: statusColor(m.status) }}>{m.id}</div>
              <div style={{ flex: 1, fontSize: 13, fontWeight: 600 }}>{m.name}</div>
              <div style={{ color: C.dim, fontSize: 11, width: 70 }}>{m.rounds}</div>
              <div style={{ width: 160, background: C.border, borderRadius: 6, height: 8 }}>
                <div style={{ width: `${m.progress}%`, background: statusColor(m.status), height: 8, borderRadius: 6 }} />
              </div>
              <div style={{ width: 40, textAlign: "right", color: C.dim, fontSize: 12 }}>{m.progress}%</div>
            </div>
            <div style={{ marginLeft: 42, color: C.dim, fontSize: 11, marginTop: 2 }}>Done when: {m.done}</div>
          </div>
        ))}
      </Section>

      <Section title="Prompt chain">
        <ChainTable rows={chain} />
        {chainSecondary.length > 0 && (
          <>
            <div style={{ color: C.dim, fontSize: 11, margin: "12px 0 6px", textTransform: "uppercase", letterSpacing: 1 }}>Completed track — CalendarApp</div>
            <ChainTable rows={chainSecondary} />
          </>
        )}
        {chainTertiary.length > 0 && (
          <>
            <div style={{ color: C.dim, fontSize: 11, margin: "12px 0 6px", textTransform: "uppercase", letterSpacing: 1 }}>Completed track — Multi-App Framework</div>
            <ChainTable rows={chainTertiary} />
          </>
        )}
      </Section>

      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 16 }}>
        <Section title="Verification timeline (proven runs only)">
          {verificationTimeline.length === 0 ? (
            <div style={{ color: C.dim, fontSize: 13 }}>Nothing proven yet — awaiting the first outcome report.</div>
          ) : (
            verificationTimeline.map((v, i) => (
              <div key={i} style={{ fontSize: 12, marginBottom: 6 }}>
                <span style={{ color: C.dim }}>{v.when} · {v.round}</span> · {v.what} →{" "}
                <span style={{ color: v.result.includes("BLOCKED") || v.result.includes("FLAKY") ? C.yellow : C.done }}>{v.result}</span>
              </div>
            ))
          )}
        </Section>

        <Section title="Marker ledger (open TODOs by phase)">
          {markerLedger.length === 0 ? (
            <div style={{ color: C.dim, fontSize: 13 }}>No markers yet — populated from the first census.</div>
          ) : (
            <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 12 }}>
              <thead>
                <tr style={{ color: C.dim, textAlign: "left" }}>
                  <Th>Marker</Th><Th w={60}>Open</Th><Th>Note</Th>
                </tr>
              </thead>
              <tbody>
                {markerLedger.map((m) => (
                  <tr key={m.marker} style={{ borderTop: `1px solid ${C.border}` }}>
                    <Td>{m.marker}</Td>
                    <Td color={m.open === null ? C.dim : C.yellow} bold>{m.open === null ? "—" : m.open}</Td>
                    <Td color={C.dim}>{m.note}</Td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </Section>
      </div>

      <Section title="Locked tech stack">
        <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 12 }}>
          <tbody>
            {stack.map((s) => (
              <tr key={s.layer} style={{ borderTop: `1px solid ${C.border}` }}>
                <Td color={C.dim}>{s.layer}</Td>
                <Td>{s.choice}</Td>
              </tr>
            ))}
          </tbody>
        </table>
      </Section>

      <Section title="Open decisions (deliberate, not accidental)">
        <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 12 }}>
          <thead>
            <tr style={{ color: C.dim, textAlign: "left" }}>
              <Th>Decision</Th><Th w={150}>Status</Th><Th>Notes</Th>
            </tr>
          </thead>
          <tbody>
            {decisions.map((d) => (
              <tr key={d.decision} style={{ borderTop: `1px solid ${C.border}` }}>
                <Td>{d.decision}</Td>
                <Td color={decisionColor(d.status)} bold>{d.status}</Td>
                <Td color={C.dim}>{d.note}</Td>
              </tr>
            ))}
          </tbody>
        </table>
      </Section>

      <Section title="Risks & mitigations">
        <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 12 }}>
          <thead>
            <tr style={{ color: C.dim, textAlign: "left" }}>
              <Th>Risk</Th><Th>Impact</Th><Th>Mitigation</Th>
            </tr>
          </thead>
          <tbody>
            {risks.map((r) => (
              <tr key={r.risk} style={{ borderTop: `1px solid ${C.border}` }}>
                <Td>{r.risk}</Td>
                <Td color={C.blocked}>{r.impact}</Td>
                <Td color={C.dim}>{r.mitigation}</Td>
              </tr>
            ))}
          </tbody>
        </table>
      </Section>

      <Section title="Definition of done (release-level)">
        {doneChecklist.map((c, i) => (
          <div key={i} style={{ display: "flex", gap: 8, fontSize: 13, marginBottom: 6 }}>
            <span style={{ color: c.done ? C.done : C.pending }}>{c.done ? "☑" : "☐"}</span>
            <span style={{ color: c.done ? C.text : C.dim }}>{c.item}</span>
          </div>
        ))}
      </Section>

      <Section title="Human gates (Act · How · If-skipped)">
        {clearedChecks.map((c, i) => (
          <div key={i} style={{ display: "flex", gap: 10, fontSize: 13, marginBottom: 10 }}>
            <div>✅</div>
            <div>
              <div>{c.name}</div>
              <div style={{ color: C.dim, fontSize: 11 }}>Agent-cleared: {c.verified}</div>
            </div>
          </div>
        ))}
        {gates.map((g, i) => (
          <div key={i} style={{ display: "flex", gap: 10, fontSize: 13, marginBottom: 10 }}>
            <div>{g.icon}</div>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 600 }}>{g.name}</div>
              <div style={{ color: C.dim, fontSize: 11, marginTop: 2 }}>
                <span style={{ color: C.yellow }}>Act:</span> {g.act} · <span style={{ color: C.yellow }}>How:</span> {g.how} · <span style={{ color: C.yellow }}>If skipped:</span> {g.ifSkipped}
              </div>
            </div>
          </div>
        ))}
      </Section>
    </div>
  );
}
