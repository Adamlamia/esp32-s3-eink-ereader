#pragma once
// ===========================================================================
//  ReaderApp  —  E-Reader application (flagship app)
// ===========================================================================
//  Wraps the existing TextReader + library + bookmark + menu logic into the
//  App interface. Internal state machine: Library → Reading → Menu.
//
//  Gestures (decoded by AppManager, delivered as ButtonEvent):
//    Library:  Tap = move highlight     LongHold = open / toggle
//    Reading:  Tap = next page          MediumHold = prev page   LongHold = menu
//    Menu:     Tap = move highlight     LongHold = select item
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"
#include "app/AppManager.h"
#include "reader/TextReader.h"
#include "bookmark/BookmarkManager.h"

class ReaderApp : public App {
public:
    explicit ReaderApp(SystemContext &ctx);
    ~ReaderApp() override;

    // --- Identity ---
    const char* name() const override { return "E-Reader"; }

    // --- Lifecycle ---
    void onEnter() override;
    void onExit() override;

    // --- Input ---
    void onButton(ButtonEvent ev) override;

    // --- Optional hooks ---
    void onLoop(uint32_t nowMs) override {}   // reader needs no periodic work
    bool wantsSleep() override { return true; }

private:
    // --- Internal screen state machine ---
    enum class Screen { Library, Reading, Menu };

    // --- Library helpers ---
    void showLibraryScreen();
    int  libraryItemCount();
    void moveLibrarySelection(int delta);
    void librarySelect();

    // --- Reading menu helpers ---
    void showMenuScreen();
    void openMenu();
    void closeMenu();
    void moveMenuSelection(int delta);
    void menuSelect();
    String menuLabel(int i);

    // --- Actions ---
    void dropBookmark();
    void toggleWebPortal();
    void toggleReaderFontSize();

    // --- Small centred helper (compact reading font) ---
    void drawSmallCentered(int y, const String &s);

    // --- State ---
    SystemContext   &_ctx;
    Screen           _screen = Screen::Library;
    BookmarkManager *_bookmarks = nullptr;
    TextReader      *_reader    = nullptr;
    int              _librarySel = 0;
    int              _menuSel    = 0;

    static const int MENU_COUNT = 5;   // Resume, Library, Bookmark, Wi-Fi, Home
};
