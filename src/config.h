#pragma once
// ===========================================================================
//  config.h  —  Central configuration for the ESP32-S3 E-Ink E-Reader
// ===========================================================================
//  Tweak Wi-Fi behaviour, pin assignments and reader defaults here.
//  Keep any real Wi-Fi passwords out of git — see secrets.h (git-ignored).
// ===========================================================================

// --- Firmware -------------------------------------------------------------
#define FW_NAME     "ESP32-S3 E-Ink E-Reader"
#define FW_VERSION  "0.2.0"
#define OWNER_NAME  "Adam"               // shown on the boot greeting screen

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
// The T5 4.7" S3 exposes ONE user button, on GPIO21 (LilyGo BUTTON_1), wired
// to GND and read active-low. The whole reader is driven from it via press
// gestures (see below). NOTE: GPIO0 is the e-paper CFG_STR line and GPIO40 is
// the e-paper STH line, so they CANNOT be used as buttons. The optional
// external prev/next pins are disabled (-1); set them to a free GPIO only if
// you physically wire extra buttons (button -> GND).
#define BTN_BOOT    21    // built-in user button (see gesture map)
#define BTN_PREV    -1    // optional external button: previous page (disabled)
#define BTN_NEXT    -1    // optional external button: next page (disabled)

// --- Single-button gesture map (GPIO21) -----------------------------------
//  While reading a book (three hold bands on the one button):
//   quick tap    -> next page (fires instantly on release)
//   medium hold  -> previous page (fires on release, before the menu threshold)
//   long hold    -> open the menu (Resume / Library / Bookmark / Wi-Fi)
//  In the menu or the library list:
//   single tap   -> move the highlight box (wraps around, acts instantly)
//   long hold    -> select / open the highlighted item
//                   (menu item 0 "Resume" closes; library row 0 toggles Wi-Fi)
#define BTN_DEBOUNCE_MS     30    // ignore contact bounce
#define BTN_PREVHOLD_MS     350   // reading: hold this long (< long-press) = prev page
#define BTN_LONGPRESS_MS    750   // hold beyond this = long press (open menu / select)

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
#define MARGIN_X          26    // side margins (px)
#define MARGIN_Y          20    // top margin (px)
#define STATUS_H          30    // bottom status-bar reserve (px)
#define DEFAULT_FONT_SIZE 1     // FiraSans drives UI text; book body uses ReaderFont
#define FULL_REFRESH_EVERY 8    // full-clear every N pages to kill ghosting

// --- App framework --------------------------------------------------------
#define APP_LAUNCHER_TITLE  "Home"   // title shown on the launcher / home screen
#define APP_MAX_COUNT       8        // max registered apps (static array in AppManager)

// --- Calendar app ---------------------------------------------------------
// Pure-logic calendar core (Round 1). Sizing constants for the header-only
// seams in src/core/Calendar*.h; later rounds (SD cache, Wi-Fi/NTP/HTTP sync,
// e-ink UI) build on these. No heap is used anywhere in the seams, so every
// capacity below bounds a fixed static buffer.
#define CAL_TZ_OFFSET_SEC    (8 * 3600)  // fixed UTC offset: Malaysia (MYT, UTC+8, no DST)
#define CAL_MAX_EVENTS       64          // max concrete events held in memory / per sync
#define CAL_TITLE_MAX        64          // event title buffer (chars, incl. NUL terminator)
#define CAL_SYNC_HOUR        6           // hour-of-day (local) the daily sync is due
#define CAL_SYNC_WINDOW_DAYS 14          // rolling window of days to materialise events for
#define CAL_MAX_CALENDARS    4           // max ICS feeds (Google calendars) tracked

// Round 2: the synced cache (lastSyncUtc + materialised events) is persisted
// as JSON at CAL_CACHE_FILE on the active filesystem (SD or LittleFS) by
// src/apps/calendar/CalendarStore.
#define CAL_CACHE_FILE       "/calendar.json"

// Round 3: scheduled-sync + power management (see core/SyncSchedule.h and
// CalendarApp::onLoop). All three tune the automatic daily sync:
//   CAL_MIN_BATTERY_FOR_SYNC  skip AUTO/boot syncs below this battery % so a
//                             near-empty device never lights up the radio for a
//                             non-essential fetch (manual "Sync now" still works).
//   CAL_SYNC_STALE_SEC        backstop: force a resync once the cache is at
//                             least this old, even if the daily hour was missed.
//   CAL_WAKE_CAP_SEC          max light-sleep timer the Calendar app requests
//                             (AppManager caps sleepWakeupSec() at this) so the
//                             device wakes periodically to re-check the schedule
//                             and battery instead of sleeping straight to 06:00.
#define CAL_MIN_BATTERY_FOR_SYNC 15            // % battery floor for auto/boot syncs
#define CAL_SYNC_STALE_SEC       (20 * 3600)   // resync backstop: 20 h since last sync
#define CAL_WAKE_CAP_SEC         (6 * 3600)    // max scheduled wakeup interval: 6 h

// Wall-clock validity floor (UTC epoch seconds, 2023-11-14): any time(nullptr)
// reading below this is "clock not fixed yet" (the ESP32-S3 has no battery-
// backed RTC, so it reads garbage near 0 after a power cycle until NTP runs).
// Shared by CalendarSync's NTP wait and CalendarApp's clock sources so every
// site agrees on what "valid" means.
#define CAL_CLOCK_MIN_EPOCH      1700000000LL

// ICS feeds + Wi-Fi credentials are SECRETS and live in src/secrets.h
// (git-ignored, #included above via __has_include). Create that file with,
// for n = 0..CAL_MAX_CALENDARS-1 (every line optional and individually
// guarded; the secret ICS URL itself — with its embedded API key — is the
// credential, so it must never be committed):
//
//   #define WIFI_STA_SSID     "your-router-ssid"
//   #define WIFI_STA_PASS     "your-router-password"
//   #define CAL_ICS_URL_0     "https://calendar.google.com/calendar/ical/xxx/basic.ics"
//   #define CAL_ICS_LABEL_0   "Work"
//   #define CAL_ICS_URL_1     "https://calendar.google.com/calendar/ical/yyy/basic.ics"
//   #define CAL_ICS_LABEL_1   "Family"
//   // ... CAL_ICS_URL_2 / CAL_ICS_LABEL_2, CAL_ICS_URL_3 / CAL_ICS_LABEL_3
//
// Without secrets.h the firmware still builds: the calendar shows its empty
// cache and "Sync now" reports "No Wi-Fi secrets (src/secrets.h)" on screen.

// --- Power management -----------------------------------------------------
#define IDLE_SLEEP_SECONDS  120  // enter light sleep after inactivity
#define WEB_AUTO_START    0    // 0 = portal OFF at boot (calendar sync works); 1 = auto-start AP
#define WEB_ACTIVE_MINUTES  10   // keep Wi-Fi/web alive this long after boot (if auto-started)
