// ===========================================================================
//  ReaderApp.cpp  —  E-Reader application implementation
// ===========================================================================
//  Round 1: skeleton stubs only. Round 2 migrates the full library, reading,
//  menu, bookmark, Wi-Fi toggle and font-size logic from main.cpp into here.
// ===========================================================================
#include "ReaderApp.h"

// --- Construction / destruction --------------------------------------------
ReaderApp::ReaderApp(SystemContext &ctx)
    : _ctx(ctx) {}

ReaderApp::~ReaderApp() {
    // TODO(R2): delete owned TextReader / BookmarkManager if heap-allocated
}

// --- Lifecycle -------------------------------------------------------------
void ReaderApp::onEnter() {
    // TODO(R2): migrate from main.cpp —
    //   Attempt to resume the last opened book (bookmarks->getLastOpenedBook()).
    //   If resuming succeeds: enter Reading screen, render the page.
    //   Otherwise: enter Library screen, call showLibraryScreen().
    //   Show the personalised boot greeting ("Welcome back, Adam").
}

void ReaderApp::onExit() {
    // TODO(R2): migrate from main.cpp —
    //   Last book + offset are already persisted by BookmarkManager on every
    //   page turn, so onExit() may only need to null out the reader or set
    //   _screen back to Library for a clean re-entry.
}

// --- Input -----------------------------------------------------------------
void ReaderApp::onButton(ButtonEvent ev) {
    // TODO(R2): migrate from main.cpp's handleButtons() reading section —
    //
    // switch (_screen) {
    //     case Screen::Library:
    //         Tap      → moveLibrarySelection(+1)
    //         LongHold → librarySelect()
    //         MediumHold → (unused in library, ignore)
    //
    //     case Screen::Reading:
    //         Tap        → reader->nextPage()
    //         MediumHold → reader->prevPage()
    //         LongHold   → openMenu()
    //
    //     case Screen::Menu:
    //         Tap      → moveMenuSelection(+1)
    //         LongHold → menuSelect()
    //         MediumHold → (unused in menu, ignore)
    // }
    //
    // Menu gains a 5th item: "Back to Home" → _ctx.manager->requestHome()
}
