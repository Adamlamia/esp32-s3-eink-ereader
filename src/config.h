#pragma once
// ===========================================================================
//  config.h  —  Central configuration for the ESP32-S3 E-Ink E-Reader
// ===========================================================================
//  Tweak Wi-Fi behaviour, pin assignments and reader defaults here.
//  Keep any real Wi-Fi passwords out of git — see secrets.h (git-ignored).
// ===========================================================================

// --- Firmware -------------------------------------------------------------
#define FW_NAME     "ESP32-S3 E-Ink E-Reader"
#define FW_VERSION  "0.1.0"

// --- Wi-Fi ----------------------------------------------------------------
// The reader boots into a self-hosted Access Point so you can reach the
// upload website even with no router around. If STA credentials are provided
// (via secrets.h) it will also try to join your home network.
#define AP_SSID       "EReader-Setup"
#define AP_PASSWORD   "read1234"        // >= 8 chars, change me!
#define WEB_HOSTNAME  "ereader"          // http://ereader.local  (mDNS)
#define WEB_PORT      80

// Optional: create src/secrets.h defining WIFI_STA_SSID / WIFI_STA_PASS
// to also connect to your existing Wi-Fi. It is #included if present.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// --- Storage --------------------------------------------------------------
// Books live on the microSD card when present, otherwise on internal LittleFS.
#define BOOKS_DIR       "/books"        // directory that holds .txt files
#define BOOKMARKS_FILE  "/bookmarks.json"
#define SETTINGS_FILE   "/settings.json"

// --- microSD (SPI) pins for the T5 4.7" S3 --------------------------------
// These match LilyGo-EPD47's utilities.h for the ESP32-S3 board. Guarded so
// that if utilities.h is included first, its definitions win (no redefine
// warnings) and stay authoritative.
#ifndef SD_MISO
  #define SD_MISO   (16)
#endif
#ifndef SD_MOSI
  #define SD_MOSI   (15)
#endif
#ifndef SD_SCLK
  #define SD_SCLK   (11)
#endif
#ifndef SD_CS
  #define SD_CS     (42)
#endif

// --- Buttons (T5 4.7" S3) -------------------------------------------------
// The board reliably exposes ONE programmable button at runtime: BOOT (GPIO0).
// The full reader is therefore driveable from that single button via press
// gestures (see below). Optional external tactile buttons on the free GPIOs
// give dedicated prev/next if you wire them (button -> GND, pull-ups on).
#define BTN_BOOT    0     // built-in gesture button (see gesture map)
#define BTN_PREV    39    // optional external button: previous page
#define BTN_NEXT    40    // optional external button: next page

// --- Single-button gesture map (BOOT) -------------------------------------
//   single tap   -> next page
//   double tap   -> previous page
//   triple tap   -> open the library screen
//   long press   -> drop a bookmark at the current position
#define BTN_DEBOUNCE_MS     30    // ignore contact bounce
#define BTN_LONGPRESS_MS    700   // hold beyond this = long press
#define BTN_MULTITAP_GAP_MS 350   // max gap between taps in a multi-tap

// --- USB Mass Storage ("USB drive mode") ----------------------------------
// When enabled, holding BOOT while plugging into a computer exposes the
// microSD card as a normal USB drive so you can drag books over the cable.
// Off by default: enabling it switches the USB stack to TinyUSB (OTG), which
// changes the serial-monitor behaviour and needs validating on hardware.
// Enable via build_flags in platformio.ini: -DENABLE_USB_MSC=1
#ifndef ENABLE_USB_MSC
  #define ENABLE_USB_MSC 0
#endif

// --- Battery monitoring ---------------------------------------------------
#define BATTERY_ADC_PIN   14    // shared ADC divider on many T5 revisions
#define BATTERY_DIVIDER   2.0f  // on-board resistor divider ratio

// --- Reader defaults ------------------------------------------------------
#define DISPLAY_WIDTH     960
#define DISPLAY_HEIGHT    540
#define MARGIN_X          30
#define MARGIN_Y          30
#define DEFAULT_FONT_SIZE 1     // 0 = small, 1 = medium, 2 = large
#define FULL_REFRESH_EVERY 8    // full-clear every N pages to kill ghosting

// --- Power management -----------------------------------------------------
#define IDLE_SLEEP_SECONDS  120  // enter light sleep after inactivity
#define WEB_ACTIVE_MINUTES  10   // keep Wi-Fi/web alive this long after boot
