// ===========================================================================
//  ReaderApp.cpp  —  E-Reader application implementation
// ===========================================================================
//  Migrated from the monolithic main.cpp. Contains the library browser,
//  reading view, menu overlay, bookmark/Wi-Fi/font-toggle actions — all
//  driven by ButtonEvent values delivered from AppManager.
// ===========================================================================
#include "ReaderApp.h"
#include "core/Format.h"

// --- Construction / destruction --------------------------------------------
ReaderApp::ReaderApp(SystemContext &ctx)
    : _ctx(ctx) {}

ReaderApp::~ReaderApp() {
    delete _reader;
    delete _bookmarks;
}

// --- Lifecycle -------------------------------------------------------------
void ReaderApp::onEnter() {
    // Initialise bookmark + reader subsystems on first entry.
    if (!_bookmarks) {
        _bookmarks = new BookmarkManager(_ctx.storage.fs());
        _bookmarks->begin();
    }
    if (!_reader) {
        _reader = new TextReader(_ctx.display, _ctx.storage, *_bookmarks);
    }

    // Personalised greeting; resume the last opened book, else show library.
    String last     = _bookmarks->getLastOpenedBook();
    bool   resuming = last.length() && _ctx.storage.exists(last);
    _ctx.display.showMessage("Welcome back, " OWNER_NAME,
                             resuming ? "Continuing last book..."
                                      : "Opening your library...");
    if (resuming && _reader->open(last, -1)) {
        _screen = Screen::Reading;
        _reader->render();
    } else {
        _screen = Screen::Library;
        showLibraryScreen();
    }
}

void ReaderApp::onExit() {
    // Last book + offset are already persisted by BookmarkManager on every
    // page turn. Just reset to Library for a clean re-entry.
    _screen = Screen::Library;
}

// --- Input -----------------------------------------------------------------
void ReaderApp::onButton(ButtonEvent ev) {
    switch (_screen) {
        case Screen::Library:
            if (ev == ButtonEvent::Tap)           moveLibrarySelection(1);
            else if (ev == ButtonEvent::LongHold) librarySelect();
            break;

        case Screen::Reading:
            if (ev == ButtonEvent::Tap)            _reader->nextPage();
            else if (ev == ButtonEvent::MediumHold) _reader->prevPage();
            else if (ev == ButtonEvent::LongHold)   openMenu();
            break;

        case Screen::Menu:
            if (ev == ButtonEvent::Tap)           moveMenuSelection(1);
            else if (ev == ButtonEvent::LongHold) menuSelect();
            break;
    }
}

// ===========================================================================
//  Library screen
// ===========================================================================
static String humanSize(uint64_t bytes) {
    return String(core::humanSize(bytes).c_str());
}

void ReaderApp::drawSmallCentered(int y, const String &s) {
    int w = _ctx.display.textWidth(s, true);
    _ctx.display.drawBookText((DISPLAY_WIDTH - w) / 2, y, s);
}

int ReaderApp::libraryItemCount() {
    // books + 2 virtual rows (Wi-Fi toggle + font-size toggle)
    return (int)_ctx.storage.listBooks().size() + 2;
}

void ReaderApp::showLibraryScreen() {
    auto books = _ctx.storage.listBooks();
    bool wifi  = _ctx.portal && _ctx.portal->isRunning();
    DisplayManager &d = _ctx.display;
    d.setWifiState(wifi);
    _screen = Screen::Library;
    if (_librarySel < 0 || _librarySel >= libraryItemCount()) _librarySel = 0;

    d.clearBuffer();
    d.drawTextCentered(56, "Your Library", 2);

    // Wi-Fi indicator + how to reach the portal.
    String wifiLine = wifi ? "Wi-Fi ON - join '" AP_SSID "' then open http://" WEB_HOSTNAME ".local"
                           : "Wi-Fi OFF - hold the button here to enable uploads";
    drawSmallCentered(96, wifiLine);

    // Book count + remaining storage.
    String stat = String(books.size()) + (books.size() == 1 ? " book" : " books")
                + "     " + humanSize(_ctx.storage.freeBytes()) + " free ("
                + (_ctx.storage.usingSD() ? "microSD" : "flash") + ")";
    drawSmallCentered(122, stat);

    int lh = d.readerLineHeight() + 6;
    int y  = 168;

    // Row 0: virtual Wi-Fi toggle.
    {
        String wifiRow = String("Wi-Fi upload: ") + (wifi ? "ON" : "OFF");
        if (_librarySel == 0)
            d.drawSelectionBox(MARGIN_X - 12,
                y - d.readerAscender() - 4,
                d.usableWidth() + 24,
                d.readerLineHeight() + 10);
        d.drawBookText(MARGIN_X, y, wifiRow);
        y += lh;
    }

    if (books.empty()) {
        drawSmallCentered(y + 20, "No books yet.");
        drawSmallCentered(y + 54,
            "Join Wi-Fi '" AP_SSID "', open http://" WEB_HOSTNAME ".local, drop .txt files.");
        y += 88;
    } else {
        for (size_t i = 0; i < books.size() && i < 10; i++) {
            String name = books[i].name;
            if (name.length() > 44) name = name.substring(0, 41) + "...";
            String line = String(i + 1) + ".  " + name
                        + "   (" + humanSize(books[i].size) + ")";
            if ((int)i + 1 == _librarySel)
                d.drawSelectionBox(MARGIN_X - 12,
                    y - d.readerAscender() - 4,
                    d.usableWidth() + 24,
                    d.readerLineHeight() + 10);
            d.drawBookText(MARGIN_X, y, line);
            y += lh;
        }
    }

    // Last row: virtual font-size toggle.
    {
        String fontRow = String("Font size: ")
                       + (d.readerFontSmall() ? "Small" : "Normal");
        if (_librarySel == libraryItemCount() - 1)
            d.drawSelectionBox(MARGIN_X - 12,
                y - d.readerAscender() - 4,
                d.usableWidth() + 24,
                d.readerLineHeight() + 10);
        d.drawBookText(MARGIN_X, y, fontRow);
        y += lh;
    }

    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 40,
        "Tap = move    Hold = open / toggle");
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16,
        "Add books: join '" AP_SSID "' -> http://" WEB_HOSTNAME ".local -> drop .txt");
    d.flush(true);
}

void ReaderApp::moveLibrarySelection(int delta) {
    int n = libraryItemCount();
    _librarySel += delta;
    if (_librarySel < 0)  _librarySel = n - 1;
    if (_librarySel >= n) _librarySel = 0;
    showLibraryScreen();
}

void ReaderApp::librarySelect() {
    if (_librarySel == 0) { toggleWebPortal(); return; }
    if (_librarySel == libraryItemCount() - 1) { toggleReaderFontSize(); return; }

    auto books = _ctx.storage.listBooks();
    int  bookIdx = _librarySel - 1;
    if (books.empty() || bookIdx < 0 || bookIdx >= (int)books.size() || !_reader) {
        showLibraryScreen();
        return;
    }
    _ctx.display.showMessage("Opening", books[bookIdx].name);
    delay(500);
    if (_reader->open(books[bookIdx].path, -1)) {
        _screen = Screen::Reading;
        _reader->render();
    } else {
        _ctx.display.showMessage("Could not open", books[bookIdx].name);
        delay(900);
        showLibraryScreen();
    }
}

// ===========================================================================
//  Reading menu overlay
// ===========================================================================
String ReaderApp::menuLabel(int i) {
    switch (i) {
        case 0:  return "Resume reading";
        case 1:  return "Library";
        case 2:  return "Bookmark here";
        case 3:  return String("Wi-Fi upload: ") + ((_ctx.portal && _ctx.portal->isRunning()) ? "ON" : "OFF");
        default: return "Back to Home";
    }
}

void ReaderApp::showMenuScreen() {
    DisplayManager &d = _ctx.display;
    d.setWifiState(_ctx.portal && _ctx.portal->isRunning());
    d.clearBuffer();
    d.drawTextCentered(74, "Menu", 2);
    int lh = d.lineHeightFor(1) + 16;
    int x  = DISPLAY_WIDTH / 2 - 220;
    int y  = 190;
    for (int i = 0; i < MENU_COUNT; i++) {
        String label = menuLabel(i);
        if (i == _menuSel) {
            int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label, 1);
        y += lh;
    }
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16,
        "Tap = move    Hold = select");
    d.flush(true);
}

void ReaderApp::openMenu() {
    if (!_reader || !_reader->bookName().length()) return;
    _menuSel = 0;
    _screen  = Screen::Menu;
    showMenuScreen();
}

void ReaderApp::closeMenu() {
    _screen = Screen::Reading;
    if (_reader && _reader->bookName().length()) _reader->render();
    else showLibraryScreen();
}

void ReaderApp::moveMenuSelection(int delta) {
    _menuSel += delta;
    if (_menuSel < 0)           _menuSel = MENU_COUNT - 1;
    if (_menuSel >= MENU_COUNT) _menuSel = 0;
    showMenuScreen();
}

void ReaderApp::menuSelect() {
    switch (_menuSel) {
        case 0:  closeMenu(); break;                              // resume reading
        case 1:  _screen = Screen::Library; showLibraryScreen(); break;  // open library
        case 2:  dropBookmark(); break;                           // bookmark + resume
        case 3:  toggleWebPortal(); break;                        // Wi-Fi on/off
        default: _ctx.manager->requestHome(); break;              // back to Home
    }
}

// ===========================================================================
//  Actions
// ===========================================================================
void ReaderApp::dropBookmark() {
    if (!_reader || !_bookmarks) return;
    _bookmarks->addBookmark(_reader->bookName(), _reader->currentOffset(), "Quick mark");
    _bookmarks->save();
    _ctx.display.showMessage("Bookmarked", "Position saved");
    delay(600);
    _screen = Screen::Reading;
    _reader->render();
}

void ReaderApp::toggleWebPortal() {
    if (!_ctx.portal) return;
    _ctx.manager->setWebUserManaged(true);
    if (_ctx.portal->isRunning()) {
        _ctx.portal->stop();
        _ctx.display.setWifiState(false);
        _ctx.display.showMessage("Wi-Fi Off", "Upload portal disabled");
    } else {
        _ctx.portal->begin();
        _ctx.display.setWifiState(true);
        _ctx.display.showMessage("Wi-Fi On",
            "Join '" AP_SSID "' then open http://" WEB_HOSTNAME ".local");
    }
    delay(900);
    // Return to whichever screen we were on.
    if (_screen == Screen::Menu)         { _screen = Screen::Reading; _reader->render(); }
    else if (_screen == Screen::Library) showLibraryScreen();
    else if (_reader && _reader->bookName().length()) { _screen = Screen::Reading; _reader->render(); }
    else                                 showLibraryScreen();
}

void ReaderApp::toggleReaderFontSize() {
    bool small = !_ctx.display.readerFontSmall();
    _ctx.display.setReaderFontSmall(small);
    _ctx.prefs.putBool("fontSmall", small);
    showLibraryScreen();   // list redraws in the new size = instant preview
}
