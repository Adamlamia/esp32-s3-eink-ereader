#pragma once
// ===========================================================================
//  DevCompanionApp  —  reference viewer + GitHub dashboard (DEV·R1)
// ===========================================================================
//  Feature 5 (docs/PROJECT_BRIEF.md §2.3): two sections in one app.
//
//    References  — full-screen pinouts/schematics. The selected .raw file
//                  (a 259200-byte 4-bpp framebuffer dump made by the host-side
//                  tools/make_refs.py) is blitted straight into the display
//                  framebuffer (DisplayManager::blitRaw), fit-to-screen with no
//                  zoom, and the label is overlaid at the bottom on a small
//                  white plate for legibility.
//    GitHub      — read-only dashboard: one line per configured repo showing
//                  open PRs + open issues + last CI state, from the /github.json
//                  cache (refreshed by GithubSync).
//
//  Index loading (References): onEnter scans /refs/ for refs_index.txt and
//  parses it via core::parseRefsIndex; if the manifest is absent or empty it
//  falls back to enumerating the .raw files alphabetically. No /refs/ dir or
//  empty -> "No reference images" + "Copy .raw files to /refs/ on SD".
//
//  Cache loading (GitHub): entering the GitHub section reads /github.json; if
//  it is fresh (< GITHUB_STALE_SEC) it is displayed directly, otherwise an
//  auto-sync runs (secrets + portal permitting). No GITHUB_PAT compiled in ->
//  "Set GITHUB_PAT in secrets.h" empty state.
//
//  Gestures (decoded by AppManager, delivered as ButtonEvent):
//    References: Tap        = next reference image (wrap)
//                MediumHold = switch to the GitHub section
//                LongHold   = open menu
//    GitHub:     Tap        = next repo row highlight (wrap)
//                MediumHold = Sync now (manual refresh)
//                LongHold   = open menu
//    Menu:       Tap = move highlight    LongHold = select item
//                (matches CalendarApp / WeatherApp's menu convention exactly)
//
//  Menu items: "References" (switch section), "GitHub" (switch section),
//  "Back to Home" (AppManager::requestHome()). NOTE on section switching:
//  MediumHold is context-dependent per the spec — in References it switches to
//  GitHub, while in GitHub it is the manual "Sync now". The GitHub -> References
//  direction is therefore offered through the menu ("References" item), which
//  keeps both sections reachable at all times.
//
//  No scheduled work: sleepWakeupSec() keeps the base-class default (-1, button-
//  only wakeup) and there is no onLoop() override — the GitHub on-open stale
//  check replaces any background schedule, exactly like WeatherApp. Waking the
//  radio unsolicited would only burn battery.
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"
#include "core/RefsIndex.h"
#include "core/GithubModel.h"

class DevCompanionApp : public App {
public:
    explicit DevCompanionApp(SystemContext &ctx);

    // --- Identity ---
    const char* name() const override { return "Dev Companion"; }
    const char* icon() const override { return "[<>]"; }   // code-ish glyph

    // --- Lifecycle ---
    void onEnter() override;
    void onExit()  override;

    // --- Input ---
    void onButton(ButtonEvent ev) override;

    // --- Optional hooks ---
    // No onLoop() override: no scheduled sync (see header).
    bool wantsSleep() override { return !_syncing; }   // stay awake mid-sync
    // No sleepWakeupSec() override: button-only wakeup is correct here (the
    // GitHub on-open stale check replaces any scheduled wakeup). Base returns -1.

private:
    // --- Section / screen state machines ---
    enum class Section : uint8_t { Refs = 0, GitHub = 1 };
    enum class Screen  : uint8_t { Main = 0, Menu = 1 };

    // --- References ---
    void scanRefs();              // /refs/refs_index.txt, else .raw enumeration
    void renderRefs();            // dispatch: image or empty state
    void renderRefEmpty();
    void renderRefImage();

    // --- GitHub ---
    void loadGithubCache();       // GithubStore -> _repos / _ghLastSync
    bool shouldAutoSyncGithub() const;
    void renderGithub();

    // --- Shared rendering / navigation ---
    void renderCurrent();         // dispatch on _section
    void renderMenu();
    void openMenu();
    void menuSelect();
    void switchSection(Section s);
    void runSync();

    // --- Helpers ---
    String ciGlyph(core::CiState ci) const;   // ASCII CI marker (font is ASCII-only)
    String ghLastSyncLine() const;

    // --- State ---
    SystemContext &_ctx;
    Section _section = Section::Refs;
    Screen  _screen  = Screen::Main;

    // References index (manifest or enumerated .raw files)
    core::RefsEntry _refs[REFS_MAX_ENTRIES];
    int _refCount = 0;
    int _refIndex = 0;

    // GitHub dashboard cache
    core::GithubRepoStatus _repos[GITHUB_MAX_REPOS];
    int     _repoCount   = 0;
    int64_t _ghLastSync  = 0;
    int     _ghHighlight = 0;     // highlighted repo row in the GitHub list

    int  _menuSel  = 0;
    bool _syncing  = false;       // true only while GithubSync::run() blocks

    static const int MENU_COUNT = 3;  // References, GitHub, Back to Home
};
