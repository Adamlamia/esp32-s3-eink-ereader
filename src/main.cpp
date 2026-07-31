// ===========================================================================
//  main.cpp  —  ESP32-S3 E-Ink multi-app platform entry point
// ===========================================================================
//  Boot flow:
//    1. Bring up display + storage (SD or LittleFS).
//    2. Optionally enter USB drive mode (hold BOOT while powering on).
//    3. Construct shared services (SystemContext) and the WebPortal.
//    4. Register all apps via AppRegistry and hand control to AppManager.
//    5. AppManager draws the launcher; the user picks an app to enter.
// ===========================================================================
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "display/DisplayManager.h"
#include "storage/BookStorage.h"
#include "web/WebPortal.h"
#include "usb/UsbMassStorage.h"
#include "app/AppManager.h"
#include "app/AppRegistry.h"

// --- Shared services (constructed once, referenced everywhere) -------------
static DisplayManager   g_display;
static BookStorage      g_storage;
static Preferences      g_prefs;
static WebPortal       *g_portal  = nullptr;
static AppManager      *g_manager = nullptr;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n" FW_NAME "  v" FW_VERSION);

    // --- Button pins ---
    pinMode(BTN_BOOT, INPUT_PULLUP);
    if (BTN_PREV >= 0) pinMode(BTN_PREV, INPUT_PULLUP);
    if (BTN_NEXT >= 0) pinMode(BTN_NEXT, INPUT_PULLUP);

    // --- Display ---
    if (!g_display.begin()) Serial.println("[main] display init failed");

    // --- User settings (NVS) ---
    g_prefs.begin("reader");
    g_display.setReaderFontSmall(g_prefs.getBool("fontSmall", false));

    // --- USB Mass Storage mode (hold BOOT while powering on) ---
#if ENABLE_USB_MSC
    delay(60);
    if (digitalRead(BTN_BOOT) == LOW) {
        static UsbMassStorage usb;
        if (usb.begin()) {
            g_display.showMessage("USB Drive Mode",
                                  "SD card mounted on your computer");
            while (true) delay(1000);
        }
        g_display.showMessage("USB Drive Mode", "No SD card detected");
        delay(1500);
    }
#endif

    // --- Storage ---
    if (!g_storage.begin()) {
        g_display.showMessage("Storage error", "No SD / LittleFS available");
        return;
    }

    // --- Web portal (Wi-Fi AP + upload site) ---
    // WebPortal needs a BookmarkManager for its API; we pass a temporary one.
    // The ReaderApp creates its own BookmarkManager internally.
    static BookmarkManager bootBookmarks(g_storage.fs());
    bootBookmarks.begin();
    g_portal = new WebPortal(g_storage, bootBookmarks);
    g_portal->begin();
    g_display.setWifiState(g_portal->isRunning());

    // --- System context (shared services bundle) ---
    static SystemContext ctx{g_display, g_storage, g_portal, g_prefs, nullptr, 100};
    if (!g_portal->isRunning()) ctx.batteryPct = ctx.readBattery();

    // --- App framework ---
    static AppManager mgr(ctx);
    g_manager   = &mgr;
    ctx.manager = &mgr;                 // apps use this to call requestHome()
    registerAllApps(mgr, ctx);          // from AppRegistry.h
    mgr.begin();                        // draws the launcher
}

void loop() {
    if (g_manager) g_manager->loop();
    delay(20);
}
