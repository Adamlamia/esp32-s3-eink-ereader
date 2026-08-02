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
#define CAL_UID_MAX          96          // event UID buffer (chars, incl. NUL terminator);
                                         // Google UIDs are ~37 chars ("...@google.com"),
                                         // so 96 covers long third-party UIDs with
                                         // headroom. Captured by core::parseIcsFeed
                                         // (TODO·R1) so the Todo app can match done-state
                                         // by a stable identity that survives re-syncs
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
//
// Weather location (Open-Meteo, no API key). All three lines are OPTIONAL:
// Kuala Lumpur defaults are compiled in via the #ifndef guards below, so the
// Weather app is fully functional with NO weather secrets at all.
//
//   #define WEATHER_LAT    3.1390f        // degrees, +N/-S
//   #define WEATHER_LON    101.6869f      // degrees, +E/-W
//   #define WEATHER_LABEL  "Kuala Lumpur" // <= 23 chars, shown on screen
#ifndef WEATHER_LAT
  #define WEATHER_LAT    3.1390f          // default: Kuala Lumpur
#endif
#ifndef WEATHER_LON
  #define WEATHER_LON    101.6869f
#endif
#ifndef WEATHER_LABEL
  #define WEATHER_LABEL  "Kuala Lumpur"
#endif

// --- Weather app ------------------------------------------------------------
// Open-Meteo weather app (WTH·R1). Sizing constants for the header-only seam
// in src/core/OpenMeteo.h (no heap; every capacity bounds a fixed buffer),
// the /weather.json cache (src/apps/weather/WeatherStore) and the sync session
// (src/apps/weather/WeatherSync, which reuses the calendar's Wi-Fi/NTP/HTTPS
// + RAII-WiFi-off + portal-guard lifecycle).
#define WEATHER_CACHE_FILE      "/weather.json"   // cache path on the active FS
#define WEATHER_LABEL_MAX       24          // location label buffer (chars, incl. NUL)
#define WEATHER_FORECAST_DAYS   3           // daily forecast rows fetched + cached
#define WEATHER_URL_MAX         320         // request-URL buffer: the full Open-Meteo
                                            // URL with all current+daily fields is
                                            // ~270 chars, so 192 would never fit
#define WEATHER_BODY_MAX        8192        // reject bodies above this (real payload < 1 KB)
#define WEATHER_STALE_SEC       (3 * 3600)  // on-open resync threshold: 3 h since fetch
#define WEATHER_MIN_BATTERY_FOR_SYNC 15     // % floor for AUTO (on-open) resync only;
                                            // manual refresh is always allowed
#define WEATHER_TZ              "Asia/Kuala_Lumpur"  // Open-Meteo timezone: UTC+8, no
                                            // DST — the same fixed offset as
                                            // CAL_TZ_OFFSET_SEC, which the UI reuses
                                            // for its weekday/date math

// --- QR app ---------------------------------------------------------------
// QR Toolkit (QR-R1). Sizing constants for the header-only seams in
// src/core/Emvco.h + src/core/QrPayload.h (no heap; every capacity bounds a
// fixed static buffer). The QR ENCODING lib (ricmoo/QRCode) is a firmware-
// only dependency in platformio.ini; the seams + native tests stay HAL-free.
//
// QR entries are SECRETS-adjacent config and live in src/secrets.h
// (git-ignored, #included above via __has_include). For n = 0..QR_MAX_ENTRIES-
// 1, every line is optional and individually #ifdef-guarded by QrApp, so the
// firmware builds with ZERO QR secrets (the app then shows its empty state):
//
//   #define QR_PAYLOAD_0   "https://example.com/menu"   // URL / free text /
//   #define QR_LABEL_0     "Cafe menu"                  //   raw EMVCo QRPS...
//   #define QR_PAYLOAD_1   "00020101021126590014MY.GOV.BNM.RPP...6304ABCD"
//   #define QR_LABEL_1     "DuitNow"
//   // ... QR_PAYLOAD_2 / QR_LABEL_2 up to QR_PAYLOAD_7 / QR_LABEL_7
//
// A WiFi QR is ALSO generated automatically from WIFI_STA_SSID/WIFI_STA_PASS
// when those exist, ahead of any QR_PAYLOAD_n entries.
#define QR_MAX_ENTRIES      8        // max QR carousel entries (WiFi + secrets)
#define QR_LABEL_MAX        32       // entry label buffer (chars, incl. NUL)
#define QR_PAYLOAD_MAX      320      // entry payload buffer (chars, incl. NUL);
                                     // fits a QR v13 byte-mode code (425 cap)
#define QR_WIFI_QR_MAX      192      // WiFi QR string buffer (escaped worst case)
#define QR_EMVCO_MAX_FIELDS 16       // max top-level EMVCo TLV fields decoded
#define QR_EMVCO_VALUE_MAX  100      // EMVCo field value buffer: 99 chars + NUL
                                     // (the 2-digit length ceiling)
#define QR_EMVCO_PAYLOAD_MAX 384     // recommended EMVCo build-output buffer
#define QR_MIN_VERSION      3        // smallest QR version QrApp will emit
#define QR_MAX_VERSION      13       // largest: 69 modules, 425 byte-mode chars
                                     // (ECC LOW) and a 596-byte module buffer

// --- Todo app ---------------------------------------------------------------
// Todo (TODO·R1). Tasks live in a dedicated "Tasks" Google Calendar fetched
// via the EXISTING ICS mechanism (core/IcsParser.h + the calendar's portal-
// guarded / RAII-WiFi-off / NTP / HTTPS lifecycle) — zero new auth. Each task
// is an ALL-DAY event in that calendar (title = task text); the user edits
// tasks in the Google Calendar app on the phone and the device picks them up
// on the next sync. Done-state is DEVICE-LOCAL only: persisted at
// TODO_CACHE_FILE, never pushed back to Google.
//
// Sizing constants for the header-only seam in src/core/TodoModel.h (no heap;
// every capacity bounds a fixed static buffer), the /todo.json cache
// (src/apps/todo/TodoStore) and the sync session (src/apps/todo/TodoSync).
#define TODO_CACHE_FILE      "/todo.json"    // cache path on the active FS: tasks
                                             // + done-set + lastSyncUtc (the Agenda
                                             // feature reads this cache later)
#define TODO_MAX_TASKS       32          // max tasks held in memory / per sync
#define TODO_MAX_EVENTS      48          // parsed ICS events per sync (the all-day
                                             // ones become tasks; timed events skip)
#define TODO_TITLE_MAX       CAL_TITLE_MAX   // task title buffer == event title buffer
#define TODO_UID_MAX         CAL_UID_MAX     // done-set key buffer == event UID buffer
                                             // (done-state is keyed by the ICS UID)
#define TODO_DONE_MAX        48          // max done-keys persisted (stale keys are
                                             // pruned on every sync, so this only
                                             // bounds outstanding + recently done)
#define TODO_BODY_MAX        8192        // ICS body cap: the parser's unfold buffer
                                             // (ICS_BUF_MAX) holds 8191 chars, so a
                                             // larger body could never fully parse
#define TODO_STALE_SEC       (6 * 3600)  // on-open resync threshold: 6 h since sync
#define TODO_MIN_BATTERY_FOR_SYNC 15     // % floor for AUTO (on-open) resync only;
                                             // manual "Sync now" is always allowed
#define TODO_LABEL_MAX       24          // calendar label buffer (chars, incl. NUL)

// The Tasks calendar ICS URL is a SECRET (it embeds a private API key) and
// lives in src/secrets.h (git-ignored, #included above via __has_include):
//
//   #define TODO_ICS_URL    "https://calendar.google.com/calendar/ical/xxx/basic.ics"
//   #define TODO_ICS_LABEL  "Tasks"        // optional, <= 23 chars, shown on screen
//
// TODO_ICS_URL has NO compiled-in default (there is no sensible one): without
// it the firmware still builds — the app shows its empty state ("No Tasks
// calendar (set TODO_ICS_URL)") and "Sync now" reports the same on screen.
#ifndef TODO_ICS_LABEL
  #define TODO_ICS_LABEL "Tasks"          // default label when secrets.h omits it
#endif

// --- Power management -----------------------------------------------------
#define IDLE_SLEEP_SECONDS  120  // enter light sleep after inactivity
#define WEB_AUTO_START    0    // 0 = portal OFF at boot (calendar sync works); 1 = auto-start AP
#define WEB_ACTIVE_MINUTES  10   // keep Wi-Fi/web alive this long after boot (if auto-started)
