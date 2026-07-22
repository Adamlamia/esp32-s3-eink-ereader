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

DisplayManager   display;
BookStorage      storage;
BookmarkManager *bookmarks = nullptr;   // needs the active fs, built in setup()
TextReader      *reader    = nullptr;
WebPortal       *portal    = nullptr;

static uint32_t g_lastActivityMs = 0;
static uint32_t g_bootMs         = 0;

// --- Battery ---------------------------------------------------------------
static int readBatteryPercent() {
    uint32_t mv = analogReadMilliVolts(BATTERY_ADC_PIN) * BATTERY_DIVIDER;
    // Li-ion: ~3.3V empty, ~4.2V full.
    int pct = (int)((mv - 3300) * 100 / (4200 - 3300));
    return constrain(pct, 0, 100);
}

// --- Show the on-device library when there is nothing to resume -----------
static void showLibraryScreen() {
    auto books = storage.listBooks();
    display.clearBuffer();
    display.drawTextCentered(60, "Your Library", 2);
    int y = 140;
    if (books.empty()) {
        display.drawTextCentered(y, "No books yet.", 1);
        display.drawTextCentered(y + 50,
            "Connect to Wi-Fi '" AP_SSID "' and open http://" WEB_HOSTNAME ".local", 0);
    } else {
        for (size_t i = 0; i < books.size() && i < 8; i++) {
            display.drawText(MARGIN_X, y, String(i + 1) + ".  " + books[i].name, 1);
            y += display.lineHeightFor(1);
        }
    }
    display.flush(true);
}

// --- Button handling -------------------------------------------------------
static void handleButtons() {
    static uint32_t bootPressStart = 0;

    // BOOT: short press = next page, long press (>800ms) = drop a bookmark.
    if (digitalRead(BTN_BOOT) == LOW) {
        if (bootPressStart == 0) bootPressStart = millis();
    } else if (bootPressStart) {
        uint32_t held = millis() - bootPressStart;
        bootPressStart = 0;
        g_lastActivityMs = millis();
        if (reader) {
            if (held > 800) {
                bookmarks->addBookmark(reader->bookName(),
                                       reader->currentOffset(), "Quick mark");
                bookmarks->save();
                display.showMessage("Bookmarked", "Position saved");
                delay(600);
                reader->render();
            } else {
                reader->nextPage();
            }
        }
    }

    if (digitalRead(BTN_PREV) == LOW) { g_lastActivityMs = millis(); if (reader) reader->prevPage(); delay(180); }
    if (digitalRead(BTN_NEXT) == LOW) { g_lastActivityMs = millis(); if (reader) reader->nextPage(); delay(180); }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n" FW_NAME "  v" FW_VERSION);

    pinMode(BTN_BOOT, INPUT_PULLUP);
    pinMode(BTN_PREV, INPUT_PULLUP);
    pinMode(BTN_NEXT, INPUT_PULLUP);

    if (!display.begin()) Serial.println("[main] display init failed");
    display.showMessage(FW_NAME, "Starting up...");

    if (!storage.begin()) {
        display.showMessage("Storage error", "No SD / LittleFS available");
        return;
    }

    bookmarks = new BookmarkManager(storage.fs());
    bookmarks->begin();

    reader = new TextReader(display, storage, *bookmarks);
    portal = new WebPortal(storage, *bookmarks);
    portal->begin();

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

    // Keep the web portal alive for a while after boot, then shut Wi-Fi down
    // to save battery — the reader keeps working offline.
    if (portal && portal->isRunning() &&
        millis() - g_bootMs > (uint32_t)WEB_ACTIVE_MINUTES * 60000UL) {
        portal->stop();
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
