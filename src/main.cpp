// ===========================================================================
//  main.cpp  —  ESP32-S3 E-Ink E-Reader entry point
// ===========================================================================
//  Boot flow:
//    1. Bring up display + storage (SD or LittleFS).
//    2. Start the local upload website (Wi-Fi AP + http://ereader.local).
//    3. Resume the last book at the last read position, or show the library.
//    4. Handle buttons for page turns / bookmarks; light-sleep when idle.
// ===========================================================================
#include <Arduino.h>
#include "config.h"
#include "display/DisplayManager.h"
#include "storage/BookStorage.h"
#include "bookmark/BookmarkManager.h"
#include "reader/TextReader.h"
#include "web/WebPortal.h"
#include "usb/UsbMassStorage.h"

DisplayManager   display;
BookStorage      storage;
BookmarkManager *bookmarks = nullptr;   // needs the active fs, built in setup()
TextReader      *reader    = nullptr;
WebPortal       *portal    = nullptr;

static uint32_t g_lastActivityMs = 0;
static uint32_t g_bootMs         = 0;
static bool     g_webUserManaged = false;  // true once the user toggles Wi-Fi by hand
static bool     g_inLibrary      = false;  // true while the library/catalog is on screen
static int      g_librarySel     = 0;      // highlighted book in the library list
static bool     g_inMenu         = false;  // true while the reading menu overlay is up
static int      g_menuSel        = 0;      // highlighted menu item

// --- Battery ---------------------------------------------------------------
// NOTE: on the ESP32-S3 the battery pin (GPIO14) is an ADC2 channel, and ADC2
// cannot be read while Wi-Fi is active (it returns 0). Callers therefore only
// sample while the Wi-Fi portal is off and keep the last good reading.
static int readBatteryPercent() {
    uint32_t mv = analogReadMilliVolts(BATTERY_ADC_PIN) * BATTERY_DIVIDER;
    // Li-ion: ~3.3V empty, ~4.2V full.
    int pct = (int)((mv - 3300) * 100 / (4200 - 3300));
    return constrain(pct, 0, 100);
}

// --- Human-readable byte sizes (e.g. "1.2 MB") -----------------------------
static String humanSize(uint64_t bytes) {
    if (bytes < 1024ULL) return String((uint32_t)bytes) + " B";
    if (bytes < 1024ULL * 1024) return String(bytes / 1024.0, 1) + " KB";
    return String(bytes / 1024.0 / 1024.0, 1) + " MB";
}

// --- Small centred helper (compact reading font) ---------------------------
static void drawSmallCentered(int y, const String &s) {
    int w = display.textWidth(s, true);
    display.drawBookText((DISPLAY_WIDTH - w) / 2, y, s);
}

// Forward decl: the library's Wi-Fi row selects this, which is defined below.
static void toggleWebPortal();
static int  libraryItemCount();   // books + 1 (virtual Wi-Fi row), defined below

// --- Show the on-device library / catalog ---------------------------------
static void showLibraryScreen() {
    auto books = storage.listBooks();
    bool wifi  = portal && portal->isRunning();
    display.setWifiState(wifi);
    g_inLibrary = true;
    g_inMenu    = false;
    if (g_librarySel < 0 || g_librarySel >= libraryItemCount()) g_librarySel = 0;

    display.clearBuffer();
    display.drawTextCentered(56, "Your Library", 2);

    // Wi-Fi indicator + how to reach the portal.
    String wifiLine = wifi ? "Wi-Fi ON - join '" AP_SSID "' then open http://" WEB_HOSTNAME ".local"
                           : "Wi-Fi OFF - hold the button here to enable uploads";
    drawSmallCentered(96, wifiLine);

    // Book count + remaining storage.
    String stat = String(books.size()) + (books.size() == 1 ? " book" : " books")
                + "     " + humanSize(storage.freeBytes()) + " free ("
                + (storage.usingSD() ? "microSD" : "flash") + ")";
    drawSmallCentered(122, stat);

    int lh = display.readerLineHeight() + 6;
    int y  = 168;
    // Row 0 is a virtual, selectable Wi-Fi toggle so uploads stay reachable
    // without a book open (hold = select acts on whatever row is highlighted).
    {
        String wifiRow = String("Wi-Fi upload: ") + (wifi ? "ON" : "OFF");
        if (g_librarySel == 0)
            display.drawSelectionBox(MARGIN_X - 12,
                y - display.readerAscender() - 4,
                display.usableWidth() + 24,
                display.readerLineHeight() + 10);
        display.drawBookText(MARGIN_X, y, wifiRow);
        y += lh;
    }
    if (books.empty()) {
        drawSmallCentered(y + 20, "No books yet.");
        drawSmallCentered(y + 54,
            "Join Wi-Fi '" AP_SSID "', open http://" WEB_HOSTNAME ".local, drop .txt files.");
    } else {
        for (size_t i = 0; i < books.size() && i < 10; i++) {
            String name = books[i].name;
            if (name.length() > 44) name = name.substring(0, 41) + "...";
            String line = String(i + 1) + ".  " + name
                        + "   (" + humanSize(books[i].size) + ")";
            // Bold box marks the item a hold will open (obvious after a flash).
            // +1 because row 0 is the Wi-Fi toggle.
            if ((int)i + 1 == g_librarySel)
                display.drawSelectionBox(MARGIN_X - 12,
                    y - display.readerAscender() - 4,
                    display.usableWidth() + 24,
                    display.readerLineHeight() + 10);
            display.drawBookText(MARGIN_X, y, line);
            y += lh;
        }
    }
    display.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 40,
        "Tap = move    Hold = open / toggle");
    display.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16,
        "Add books: join '" AP_SSID "' -> http://" WEB_HOSTNAME ".local -> drop .txt");
    display.flush(true);
}

// The library list carries one virtual row (index 0 = Wi-Fi toggle) above the
// books, so the Wi-Fi upload portal stays reachable now that hold = select.
static int libraryItemCount() { return (int)storage.listBooks().size() + 1; }

// Move the highlight through the list (wraps around) and redraw.
static void moveLibrarySelection(int delta) {
    int n = libraryItemCount();
    g_librarySel += delta;
    if (g_librarySel < 0)  g_librarySel = n - 1;
    if (g_librarySel >= n) g_librarySel = 0;
    showLibraryScreen();
}

// Act on the highlighted row: row 0 toggles Wi-Fi, the rest open a book.
static void librarySelect() {
    if (g_librarySel == 0) { toggleWebPortal(); return; }   // virtual Wi-Fi row
    auto books = storage.listBooks();
    int  bookIdx = g_librarySel - 1;                        // -1: row 0 is Wi-Fi
    if (books.empty() || bookIdx < 0 || bookIdx >= (int)books.size() || !reader) {
        showLibraryScreen();
        return;
    }
    display.showMessage("Opening", books[bookIdx].name);
    delay(500);
    if (reader->open(books[bookIdx].path, -1)) {
        g_inLibrary = false;
        reader->render();
    } else {
        display.showMessage("Could not open", books[bookIdx].name);
        delay(900);
        showLibraryScreen();
    }
}

// --- Button handling -------------------------------------------------------
//  Everything is driven from the single on-board button (GPIO21) by gestures.
//  Reading:  quick tap = next page   medium hold = previous page   long hold = menu
//  Menu:     1 tap = move box     hold = select the highlighted item
//  Library:  1 tap = move box     hold = open book / toggle the Wi-Fi row
//  Optional external PREV/NEXT buttons (if wired) still work directly.
static void dropBookmark() {
    if (!reader) return;
    bookmarks->addBookmark(reader->bookName(), reader->currentOffset(), "Quick mark");
    bookmarks->save();
    display.showMessage("Bookmarked", "Position saved");
    delay(600);
    reader->render();
}

// Toggle the Wi-Fi upload portal on/off (menu item or the library's Wi-Fi row).
// Flipping it by hand disables the automatic power-saving shutoff so it stays as
// the user left it.
static void toggleWebPortal() {
    if (!portal) return;
    g_webUserManaged = true;
    if (portal->isRunning()) {
        portal->stop();
        display.setWifiState(false);
        display.showMessage("Wi-Fi Off", "Upload portal disabled");
    } else {
        portal->begin();
        display.setWifiState(true);
        display.showMessage("Wi-Fi On",
            "Join '" AP_SSID "' then open http://" WEB_HOSTNAME ".local");
    }
    delay(900);
    // Return to whichever list we were on; only fall through to the book when we
    // toggled Wi-Fi from the reading menu (not from the library's Wi-Fi row).
    if (g_inLibrary)                             showLibraryScreen();
    else if (reader && reader->bookName().length()) reader->render();
    else                                         showLibraryScreen();
}

// --- Reading menu overlay (opened by holding the button) ------------------
static const int MENU_COUNT = 4;
static String menuLabel(int i) {
    switch (i) {
        case 0:  return "Resume reading";
        case 1:  return "Library";
        case 2:  return "Bookmark here";
        default: return String("Wi-Fi upload: ") + ((portal && portal->isRunning()) ? "ON" : "OFF");
    }
}

static void showMenuScreen() {
    display.setWifiState(portal && portal->isRunning());
    display.clearBuffer();
    display.drawTextCentered(74, "Menu", 2);
    int lh = display.lineHeightFor(1) + 16;
    int x  = DISPLAY_WIDTH / 2 - 220;
    int y  = 190;
    for (int i = 0; i < MENU_COUNT; i++) {
        String label = menuLabel(i);
        // A bold box around the current item makes a single tap's effect obvious;
        // a tiny "> " prefix was too easy to miss after a full-screen flash.
        if (i == g_menuSel) {
            int w = display.textWidth(label, false);
            display.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        display.drawText(x, y, label, 1);
        y += lh;
    }
    display.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16,
        "Tap = move    Hold = select");
    display.flush(true);
}

static void openMenu() {
    if (!reader || !reader->bookName().length()) return;   // only meaningful while reading
    g_menuSel = 0;
    g_inMenu  = true;
    showMenuScreen();
}

static void moveMenuSelection(int delta) {
    g_menuSel += delta;
    if (g_menuSel < 0)           g_menuSel = MENU_COUNT - 1;
    if (g_menuSel >= MENU_COUNT) g_menuSel = 0;
    showMenuScreen();
}

static void closeMenu() {
    g_inMenu = false;
    if (reader && reader->bookName().length()) reader->render();
    else showLibraryScreen();
}

static void menuSelect() {
    switch (g_menuSel) {
        case 0:  closeMenu(); break;                          // resume reading
        case 1:  g_inMenu = false; showLibraryScreen(); break; // open library
        case 2:  g_inMenu = false; dropBookmark();      break; // bookmark + resume
        default: g_inMenu = false; toggleWebPortal();   break; // Wi-Fi on/off
    }
}

static void handleButtons() {
    static bool     down       = false;
    static uint32_t pressStart = 0;
    static bool     longFired  = false;

    const uint32_t now = millis();
    const bool     nowDown = (digitalRead(BTN_BOOT) == LOW);

    if (nowDown && !down) {                 // ---- press begins
        down = true;
        pressStart = now;
        longFired = false;
    } else if (nowDown && down) {            // ---- being held
        if (!longFired && now - pressStart >= BTN_LONGPRESS_MS) {
            longFired = true;
            g_lastActivityMs = now;
            if (g_inMenu)         menuSelect();      // hold in menu    = select item
            else if (g_inLibrary) librarySelect();   // hold in library = open / toggle
            else                  openMenu();         // hold in reading = open menu
        }
    } else if (!nowDown && down) {           // ---- release
        down = false;
        uint32_t held = now - pressStart;
        if (!longFired && held >= BTN_DEBOUNCE_MS) {
            g_lastActivityMs = now;
            // Menu / library: any release short of the long-press just moves the
            // highlight (acts instantly, no double-tap to wait for).
            if (g_inMenu) {
                moveMenuSelection(1);
            } else if (g_inLibrary) {
                moveLibrarySelection(1);
            } else if (reader) {
                // Reading uses three hold bands on the one button: a quick tap
                // turns forward instantly, a deliberate medium hold (still short
                // of the menu threshold) turns back a page. This keeps forward
                // snappy while making "previous" reliable -- a fast double-tap
                // can't work because the ~0.8s full refresh blocks polling.
                if (held >= BTN_PREVHOLD_MS) reader->prevPage();
                else                         reader->nextPage();
            }
        }
    }

    // Optional dedicated external buttons (wired to GND); disabled when < 0.
    if (BTN_PREV >= 0 && digitalRead(BTN_PREV) == LOW) { g_lastActivityMs = now; if (reader) reader->prevPage(); delay(180); }
    if (BTN_NEXT >= 0 && digitalRead(BTN_NEXT) == LOW) { g_lastActivityMs = now; if (reader) reader->nextPage(); delay(180); }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n" FW_NAME "  v" FW_VERSION);

    pinMode(BTN_BOOT, INPUT_PULLUP);
    if (BTN_PREV >= 0) pinMode(BTN_PREV, INPUT_PULLUP);
    if (BTN_NEXT >= 0) pinMode(BTN_NEXT, INPUT_PULLUP);

    if (!display.begin()) Serial.println("[main] display init failed");
    display.showMessage(FW_NAME, "Starting up...");

#if ENABLE_USB_MSC
    // Hold BOOT while powering on to expose the microSD card as a USB drive.
    delay(60);                               // let the input settle
    if (digitalRead(BTN_BOOT) == LOW) {
        static UsbMassStorage usb;
        if (usb.begin()) {
            display.showMessage("USB Drive Mode",
                                "SD card mounted on your computer");
            while (true) delay(1000);        // host owns the card; stay here
        }
        display.showMessage("USB Drive Mode", "No SD card detected");
        delay(1500);
    }
#endif

    if (!storage.begin()) {
        display.showMessage("Storage error", "No SD / LittleFS available");
        return;
    }

    bookmarks = new BookmarkManager(storage.fs());
    bookmarks->begin();

    reader = new TextReader(display, storage, *bookmarks);
    portal = new WebPortal(storage, *bookmarks);
    portal->begin();
    display.setWifiState(portal->isRunning());
    if (!portal->isRunning()) display.setBattery(readBatteryPercent());

    // Resume the last opened book, else show the library.
    String last = bookmarks->getLastOpenedBook();
    if (last.length() && storage.exists(last) && reader->open(last, -1)) {
        reader->render();
    } else {
        showLibraryScreen();
    }

    g_bootMs = g_lastActivityMs = millis();
}

void loop() {
    handleButtons();

    // Refresh the battery reading periodically; shown on the next page redraw.
    // Skip while Wi-Fi is on (ADC2 is unavailable then) and keep the last value.
    static uint32_t lastBattMs = 0;
    if (millis() - lastBattMs > 15000UL) {
        lastBattMs = millis();
        if (!portal || !portal->isRunning()) display.setBattery(readBatteryPercent());
    }

    // Keep the web portal alive for a while after boot, then shut Wi-Fi down
    // to save battery — the reader keeps working offline. Once the user has
    // toggled Wi-Fi by hand (menu / library), we stop auto-managing it.
    if (!g_webUserManaged && portal && portal->isRunning() &&
        millis() - g_bootMs > (uint32_t)WEB_ACTIVE_MINUTES * 60000UL) {
        portal->stop();
        display.setWifiState(false);
    }

    // Light sleep after inactivity; a button press (BOOT) wakes us up.
    if (millis() - g_lastActivityMs > (uint32_t)IDLE_SLEEP_SECONDS * 1000UL) {
        if (!portal || !portal->isRunning()) {
            Serial.println("[main] idle -> light sleep");
            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_BOOT, 0);
            esp_light_sleep_start();
            g_lastActivityMs = millis();
        }
    }

    delay(20);
}
