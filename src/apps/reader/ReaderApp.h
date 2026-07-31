#pragma once
// ===========================================================================
//  ReaderApp  —  E-Reader application (flagship app)
// ===========================================================================
//  Wraps the existing TextReader + library + bookmark + menu logic into the
//  App interface. Internal state machine: Library → Reading → Menu.
//
//  Round 1: skeleton only. Round 2 migrates all logic from main.cpp into here.
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"

class ReaderApp : public App {
public:
    explicit ReaderApp(SystemContext &ctx);
    ~ReaderApp() override;

    // --- Identity ---
    const char* name() const override { return "E-Reader"; }
    const char* icon() const override { return ""; }  // TODO(R2): optional book glyph

    // --- Lifecycle ---
    void onEnter() override;      // TODO(R2): show library / resume last book
    void onExit() override;       // TODO(R2): persist state (already handled by BookmarkManager)

    // --- Input ---
    void onButton(ButtonEvent ev) override;  // TODO(R2): route to Library/Reading/Menu

    // --- Optional hooks ---
    void onLoop(uint32_t nowMs) override {}  // reader needs no periodic work
    bool wantsSleep() override { return true; }

private:
    // TODO(R2): internal state machine
    // enum class Screen { Library, Reading, Menu };
    // Screen _screen = Screen::Library;

    // TODO(R2): migrate from main.cpp —
    //   showLibraryScreen(), librarySelect(), moveLibrarySelection()
    //   showMenuScreen(), menuSelect(), moveMenuSelection(), openMenu(), closeMenu()
    //   dropBookmark(), toggleWebPortal(), toggleReaderFontSize()

    SystemContext &_ctx;
};
