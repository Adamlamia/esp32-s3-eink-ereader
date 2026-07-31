#pragma once
// ===========================================================================
//  SystemContext  —  Shared services bundle passed to every app
// ===========================================================================
//  A plain struct that carries references to all platform services so apps
//  never touch global variables. Constructed once in main.cpp's setup() and
//  handed to each App at registration time.
// ===========================================================================
#include <Arduino.h>
#include <Preferences.h>
#include "display/DisplayManager.h"
#include "storage/BookStorage.h"
#include "web/WebPortal.h"

// Forward-declare to avoid circular includes; apps that need the manager
// (e.g. to call requestHome()) include AppManager.h themselves.
class AppManager;

struct SystemContext {
    DisplayManager  &display;       // e-ink framebuffer + drawing helpers
    BookStorage     &storage;       // SD / LittleFS filesystem access
    WebPortal       *portal;        // Wi-Fi upload portal (may be null)
    Preferences     &prefs;         // NVS-backed persistent settings
    AppManager      *manager;       // set after AppManager construction
    int              batteryPct;    // latest reading (0..100), refreshed by AppManager

    // --- Convenience -------------------------------------------------------
    // Read the battery ADC and return a percentage. Respects the ADC2/Wi-Fi
    // constraint (returns the cached value while Wi-Fi is active).
    // TODO(R2): implement — move logic from main.cpp's readBatteryPercent().
    int readBattery();
};
