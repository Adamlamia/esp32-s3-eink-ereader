export default function MultiAppFrameworkProgress() {
  const rounds = [
    { id: 1, pattern: "Scaffolding", scope: "App.h, SystemContext.h, AppManager stubs, AppRegistry, ReaderApp stubs, config", status: "done", note: "7 files created, build SUCCESS, 3 commits" },
    { id: 2, pattern: "Implementation", scope: "AppManager logic, ReaderApp migration, main.cpp rewrite", status: "done", note: "Full framework live, flashed to device, 3 commits" },
    { id: 3, pattern: "Test Generation", scope: "ButtonClassify seam extraction, native unit tests", status: "done", note: "24 new tests, 56 total pass, seam extracted, 2 commits" },
    { id: 4, pattern: "Review & Fix", scope: "CSPMO review, deferred-work ledger, final verification", status: "done", note: "0 Critical, 2 Suggestion fixed, build+tests green, 1 commit" },
  ];

  const milestones = [
    { name: "Framework skeleton", progress: 100, rounds: "R1", doneWhen: "App.h + AppManager + AppRegistry compile, existing build unbroken" },
    { name: "Full implementation", progress: 100, rounds: "R2", doneWhen: "Launcher shows, e-reader works identically, main.cpp < 100 lines" },
    { name: "Unit tests", progress: 100, rounds: "R3", doneWhen: "pio test -e native passes with new framework tests" },
    { name: "Review & hardening", progress: 100, rounds: "R4", doneWhen: "Zero Critical findings, deferred-work ledger compiled" },
  ];

  const verificationTimeline = [
    { round: 1, what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — 0 errors, RAM 14.3%, Flash 26.4%" },
    { round: 1, what: "pio test -e native", result: "BLOCKED — no host GCC on machine (pre-existing)" },
    { round: 2, what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — 0 errors, RAM 14.3%, Flash 26.5%" },
    { round: 2, what: "pio run -t upload", result: "SUCCESS — flashed to device, 1110736 bytes" },
    { round: 3, what: "pio test -e native", result: "SUCCESS — 56 tests passed (32 existing + 24 new)" },
    { round: 3, what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — seam extraction compiles clean" },
    { round: 4, what: "pio run -e lilygo_t5_47_s3", result: "SUCCESS — 0 errors after review fixes" },
    { round: 4, what: "pio test -e native", result: "SUCCESS — 56 tests passed" },
  ];

  const commits = [
    { hash: "ea1fce3", msg: "feat(app): add App interface and SystemContext shared-services struct" },
    { hash: "232e798", msg: "feat(app): add AppManager skeleton with launcher/lifecycle/button-dispatch stubs" },
    { hash: "775b63b", msg: "feat(app): add ReaderApp skeleton, AppRegistry, and config constants" },
    { hash: "b6b8c2d", msg: "feat(app): implement AppManager lifecycle, button dispatch, and system tasks" },
    { hash: "1cfe7c9", msg: "feat(reader): migrate e-reader logic from main.cpp into ReaderApp" },
    { hash: "8df45d7", msg: "refactor(main): slim bootstrap — delegate all logic to AppManager" },
    { hash: "99fafcc", msg: "refactor(core): extract ButtonClassify.h pure-logic seam from AppManager" },
    { hash: "1be37cf", msg: "test(app-framework): add 24 unit tests for ButtonClassify seam" },
    { hash: "63eeb85", msg: "fix(review): remove unused include, add defensive null check in ReaderApp" },
  ];

  const techStack = [
    { layer: "MCU", choice: "ESP32-S3 (Arduino framework)" },
    { layer: "Build", choice: "PlatformIO (env: lilygo_t5_47_s3)" },
    { layer: "Display", choice: "LilyGo T5 4.7\" e-ink 960x540 (LilyGo-EPD47)" },
    { layer: "Input", choice: "Single button GPIO21, 3 hold-band gestures" },
    { layer: "Storage", choice: "microSD (SPI) / LittleFS fallback" },
    { layer: "Network", choice: "WiFi AP + ESPAsyncWebServer" },
    { layer: "Tests", choice: "Unity native (pio test -e native)" },
    { layer: "App Framework", choice: "App base class + AppManager + SystemContext + AppRegistry" },
  ];

  const decisions = [
    { decision: "Boot to launcher (not resume last app)", status: "Resolved", notes: "User chose 'always show home screen first'" },
    { decision: "Back-to-home via app-level menu", status: "Resolved", notes: "LongHold opens app menu, 'Back to Home' item calls requestHome()" },
    { decision: "Compile-time app registration", status: "Resolved", notes: "AppRegistry.h — one line per branch, additive merges" },
    { decision: "requestHome() mechanism", status: "Resolved", notes: "SystemContext.manager pointer → AppManager::requestHome() flag" },
    { decision: "Host GCC for native tests", status: "Resolved", notes: "Installed MSYS2 + mingw-w64-x86_64-gcc 16.1.0 via winget" },
  ];

  const risks = [
    { risk: "No host GCC toolchain", impact: "RESOLVED", mitigation: "Installed MSYS2 mingw-w64-gcc 16.1.0 — tests run green" },
    { risk: "E-ink ghosting on launcher", impact: "Visual quality", mitigation: "drawLauncher uses flush(true) full refresh" },
    { risk: "WebPortal BookmarkManager duplication", impact: "Two BookmarkManager instances (boot + ReaderApp)", mitigation: "Acceptable — boot instance is static, ReaderApp creates its own on onEnter" },
  ];

  return (
    <div style={{ fontFamily: "system-ui, sans-serif", padding: 24, maxWidth: 900, margin: "0 auto", color: "#1a1a2e" }}>
      <h1 style={{ fontSize: 22, borderBottom: "2px solid #4361ee", paddingBottom: 8 }}>
        Multi-App Framework — Progress Canvas
      </h1>
      <p style={{ color: "#555", fontSize: 13 }}>
        Status: <strong>All 4 rounds complete — framework ready for feature branches</strong> | Last updated: 2026-08-01 | Round 4/4 |
        Next action: <strong style={{ color: "#2a9d8f" }}>Create feature branches (feature/weather, feature/todo, etc.)</strong>
      </p>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Milestones</h2>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13 }}>
        <thead><tr style={{ background: "#f0f0f5" }}>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Milestone</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Progress</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Rounds</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Done when</th>
        </tr></thead>
        <tbody>
          {milestones.map((m, i) => (
            <tr key={i} style={{ borderBottom: "1px solid #e0e0e0" }}>
              <td style={{ padding: "6px 8px" }}>{m.name}</td>
              <td style={{ padding: "6px 8px" }}>
                <div style={{ background: "#e0e0e0", borderRadius: 4, height: 14, width: 100, position: "relative" }}>
                  <div style={{ background: m.progress === 100 ? "#2a9d8f" : "#4361ee", borderRadius: 4, height: 14, width: `${m.progress}%` }} />
                  <span style={{ position: "absolute", top: 0, left: 40, fontSize: 10, lineHeight: "14px" }}>{m.progress}%</span>
                </div>
              </td>
              <td style={{ padding: "6px 8px" }}>{m.rounds}</td>
              <td style={{ padding: "6px 8px", fontSize: 12, color: "#555" }}>{m.doneWhen}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Chain Steps</h2>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13 }}>
        <thead><tr style={{ background: "#f0f0f5" }}>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Round</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Pattern</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Scope</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Status</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Outcome</th>
        </tr></thead>
        <tbody>
          {rounds.map((r) => (
            <tr key={r.id} style={{ borderBottom: "1px solid #e0e0e0" }}>
              <td style={{ padding: "6px 8px" }}>R{r.id}</td>
              <td style={{ padding: "6px 8px" }}>{r.pattern}</td>
              <td style={{ padding: "6px 8px", fontSize: 12 }}>{r.scope}</td>
              <td style={{ padding: "6px 8px" }}>
                <span style={{ background: r.status === "done" ? "#d4edda" : "#fff3cd", padding: "2px 8px", borderRadius: 4, fontSize: 11 }}>
                  {r.status === "done" ? "DONE" : "PENDING"}
                </span>
              </td>
              <td style={{ padding: "6px 8px", fontSize: 12, color: "#555" }}>{r.note}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Verification Timeline</h2>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13 }}>
        <thead><tr style={{ background: "#f0f0f5" }}>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Round</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Check</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Result</th>
        </tr></thead>
        <tbody>
          {verificationTimeline.map((v, i) => (
            <tr key={i} style={{ borderBottom: "1px solid #e0e0e0" }}>
              <td style={{ padding: "6px 8px" }}>R{v.round}</td>
              <td style={{ padding: "6px 8px", fontFamily: "monospace", fontSize: 12 }}>{v.what}</td>
              <td style={{ padding: "6px 8px", fontSize: 12, color: v.result.includes("BLOCKED") ? "#e63946" : "#2a9d8f" }}>{v.result}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Commits</h2>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 12 }}>
        <tbody>
          {commits.map((c) => (
            <tr key={c.hash} style={{ borderBottom: "1px solid #e0e0e0" }}>
              <td style={{ padding: "4px 8px", fontFamily: "monospace", color: "#4361ee" }}>{c.hash}</td>
              <td style={{ padding: "4px 8px" }}>{c.msg}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Tech Stack (locked)</h2>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13 }}>
        <tbody>
          {techStack.map((t, i) => (
            <tr key={i} style={{ borderBottom: "1px solid #e0e0e0" }}>
              <td style={{ padding: "4px 8px", fontWeight: 600, width: 120 }}>{t.layer}</td>
              <td style={{ padding: "4px 8px" }}>{t.choice}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Open Decisions</h2>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13 }}>
        <thead><tr style={{ background: "#f0f0f5" }}>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Decision</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Status</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Notes</th>
        </tr></thead>
        <tbody>
          {decisions.map((d, i) => (
            <tr key={i} style={{ borderBottom: "1px solid #e0e0e0" }}>
              <td style={{ padding: "6px 8px" }}>{d.decision}</td>
              <td style={{ padding: "6px 8px" }}>
                <span style={{ background: d.status === "Resolved" ? "#d4edda" : "#fff3cd", padding: "2px 8px", borderRadius: 4, fontSize: 11 }}>
                  {d.status}
                </span>
              </td>
              <td style={{ padding: "6px 8px", fontSize: 12, color: "#555" }}>{d.notes}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Risks & Mitigations</h2>
      <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 13 }}>
        <thead><tr style={{ background: "#f0f0f5" }}>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Risk</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Impact</th>
          <th style={{ padding: "6px 8px", textAlign: "left" }}>Mitigation</th>
        </tr></thead>
        <tbody>
          {risks.map((r, i) => (
            <tr key={i} style={{ borderBottom: "1px solid #e0e0e0" }}>
              <td style={{ padding: "6px 8px" }}>{r.risk}</td>
              <td style={{ padding: "6px 8px", fontSize: 12 }}>{r.impact}</td>
              <td style={{ padding: "6px 8px", fontSize: 12 }}>{r.mitigation}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2 style={{ fontSize: 16, marginTop: 24 }}>Human Gates</h2>
      <div style={{ fontSize: 13, lineHeight: 1.8 }}>
        <p>✅ <strong>Architecture design approved</strong> — confirmed in planning session</p>
        <p>✅ <strong>Firmware flashed to device</strong> — R2 upload SUCCESS (1110736 bytes)</p>
        <p>✅ <strong>Host GCC toolchain</strong> — MSYS2 mingw-w64-gcc 16.1.0 installed, 56 tests green</p>
      </div>
    </div>
  );
}
