// ===========================================================================
//  WeatherApp.cpp  —  weather UI implementation (WTH·R1)
// ===========================================================================
//  Presentation + gesture routing only. Reuses:
//    WeatherStore     — cache load/save
//    WeatherSync      — on-demand Wi-Fi/NTP/HTTPS refresh
//    core::OpenMeteo  — weatherCodeGlyph / formatTenthsC helpers
//    core::CalendarDate — weekday math for the forecast row (WEATHER_TZ is
//                       UTC+8 == CAL_TZ_OFFSET_SEC, see header)
//  Rendering mirrors CalendarApp: clearBuffer -> draw -> flush(true) (full
//  refresh on every screen change keeps the e-ink panel ghost-free).
// ===========================================================================
#include "WeatherApp.h"
#include "WeatherStore.h"
#include "WeatherSync.h"
#include "app/AppManager.h"
#include "core/CalendarDate.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

// --- Small formatting helpers (fixed-offset UTC+8, mirrors CalendarApp) -----
static const char *WD_SHORT[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

static String pad2(unsigned v) {
    return v < 10 ? String("0") + String(v) : String(v);
}

// "YYYY-MM-DD HH:MM" local date/time for the last-fetch stamp.
static String fmtDateTime(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return String((long)y) + "-" + pad2(m) + "-" + pad2(d) + " " + pad2(hh) + ":" + pad2(mm);
}

// One-decimal float temperature ("26.9"), ASCII only (no '°' in FiraSans).
static String fmt1(float v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", (double)v);
    return String(buf);
}

// Tenths-of-°C -> String via the pure seam.
static String fmtTenths(int16_t t) {
    char buf[8];
    core::formatTenthsC(t, buf, sizeof(buf));
    return String(buf);
}

// Human-facing short name for a WMO code, bucketed exactly like
// core::weatherCodeGlyph (which supplies the compact on-screen glyph).
static const char *codeName(int code) {
    const char *g = core::weatherCodeGlyph(code);
    if (!strcmp(g, "SUN")) return "Clear sky";
    if (!strcmp(g, "PRT")) return "Partly cloudy";
    if (!strcmp(g, "CLD")) return "Overcast";
    if (!strcmp(g, "FOG")) return "Fog";
    if (!strcmp(g, "RAN")) return "Rain";
    if (!strcmp(g, "SNW")) return "Snow";
    if (!strcmp(g, "STM")) return "Thunderstorm";
    return "Unknown";
}

// --- Construction ----------------------------------------------------------
WeatherApp::WeatherApp(SystemContext &ctx)
    : _ctx(ctx) {
    core::weatherSnapshotClear(_snap);
}

// --- Lifecycle -------------------------------------------------------------
void WeatherApp::onEnter() {
    loadCache();
    _screen = Screen::Main;
    _menuSel = 0;

    // On-open resync (see header): only when stale/empty AND the cheap
    // pre-checks pass. shouldAutoSyncOnEnter() never touches the radio
    // itself, so a declined auto-sync simply renders the cache/empty state.
    if (shouldAutoSyncOnEnter()) {
        Serial.printf("[Weather] on-open resync (fetched=%lld batt=%d%%)\n",
                      (long long)_snap.fetchedUtc, _ctx.batteryPct);
        runSync();          // splash -> sync -> reload -> renderMain
    } else {
        renderMain();
    }
}

void WeatherApp::onExit() {
    // Cache stays in memory; just reset navigation for a clean re-entry.
    _screen = Screen::Main;
}

// --- On-open auto-resync decision -------------------------------------------
bool WeatherApp::shouldAutoSyncOnEnter() const {
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    // Cheap pre-checks (mirror CalendarApp::maybeAutoSync): battery is read
    // from AppManager's cached _ctx.batteryPct — ADC2 is unreadable while
    // Wi-Fi is up, so it MUST be sampled before, never during, the sync.
    if (_ctx.portal && _ctx.portal->isRunning()) return false;        // radio in use
    if (_ctx.batteryPct < WEATHER_MIN_BATTERY_FOR_SYNC) return false; // battery floor (AUTO only)

    if (_snap.fetchedUtc == 0) return true;          // never synced -> try now

    // Staleness needs a valid clock. The ESP32-S3 has no battery-backed RTC:
    // before an NTP fix time() is garbage, and we would rather show a stale
    // cache than light up the radio on every boot just to judge freshness
    // (the user's Tap refresh still works, and any calendar NTP pass fixes
    // the clock for the next open).
    int64_t t = (int64_t)time(nullptr);
    if (t < CAL_CLOCK_MIN_EPOCH) return false;
    return (t - _snap.fetchedUtc) > WEATHER_STALE_SEC;
#else
    return false;   // no STA secrets compiled in -> nothing to sync against
#endif
}

// --- Input -----------------------------------------------------------------
void WeatherApp::onButton(ButtonEvent ev) {
    if (_screen == Screen::Menu) {
        // Menu convention matches CalendarApp/ReaderApp: Tap moves, LongHold selects.
        if (ev == ButtonEvent::Tap) {
            _menuSel = (_menuSel + tapCount()) % MENU_COUNT;
            renderMenu();
        } else if (ev == ButtonEvent::LongHold) {
            menuSelect();
        }
        return;
    }

    // --- Main screen ---
    if (ev == ButtonEvent::Tap) {
        // No-op on main screen: tap does nothing (the menu has Refresh now).
        // This prevents accidental refreshes and makes the gesture map predictable.
    } else if (ev == ButtonEvent::MediumHold) {
        // Deliberate no-op (documented in the header): single-screen app with
        // nothing to scroll or cycle. Ignored rather than repurposed so the
        // gesture map stays predictable.
    } else if (ev == ButtonEvent::LongHold) {
        openMenu();
    }
}

// --- Cache -----------------------------------------------------------------
void WeatherApp::loadCache() {
    WeatherStore store(_ctx.storage.fs());
    if (!store.load(_snap)) core::weatherSnapshotClear(_snap);  // empty state
}

String WeatherApp::lastSyncLine() const {
    if (_snap.fetchedUtc == 0)
        return "Never fetched - Hold -> menu -> Refresh now";
    return String("Last fetch: ") + fmtDateTime(_snap.fetchedUtc)
         + "   " + String(_snap.dayCount) + " day(s) cached";
}

// --- Rendering: main screen --------------------------------------------------
void WeatherApp::renderMain() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();

    // --- Title bar (tighter to top for better vertical balance) ---
    d.drawTextCentered(30, String("Weather - ") + String(_snap.label), 2);
    d.drawBookText(MARGIN_X, 62, lastSyncLine());

    const bool haveData = _snap.cur.valid || _snap.dayCount > 0;
    if (!haveData) {
        // Empty state: never synced (or every load failed).
        // Center the message vertically for better visual balance.
        d.drawBookText(MARGIN_X, 240, "No weather yet - Hold -> menu -> Refresh now.");
        d.drawBookText(MARGIN_X, 272, "Or wait for the next automatic fetch.");
        d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14, "Hold=menu");
        d.flush(true);
        return;
    }

    // --- Current conditions section ---
    // Tighter spacing: start at y=100 (was 160) to reduce the large gap.
    int y = 100;
    if (_snap.cur.valid) {
        // Weather glyph + description (compact single line).
        String glyphLine = String("[") + core::weatherCodeGlyph(_snap.cur.weatherCode)
                         + "]  " + codeName(_snap.cur.weatherCode);
        d.drawBookText(MARGIN_X, y, glyphLine);
        y += 36;

        // The headline temperature: the hero element.
        // Add a subtle shaded background to make it stand out visually.
        String tempLine = fmt1(_snap.cur.tempC) + " C   feels " + fmt1(_snap.cur.feelsC) + " C";
        int tempBoxW = d.textWidth(tempLine, false) + 32;
        int tempBoxH = 52;
        d.fillRectShade(MARGIN_X - 8, y - 8, tempBoxW, tempBoxH, 230);  // light gray background
        d.drawText(MARGIN_X, y, tempLine, 2);
        y += 58;

        // Humidity + wind on same line (compact grouping).
        d.drawBookText(MARGIN_X, y,
                       String("Humidity ") + String(_snap.cur.humidityPct) + " %"
                       + "    Wind " + fmt1(_snap.cur.windKph) + " km/h");
    } else {
        d.drawBookText(MARGIN_X, y, "Current conditions unavailable.");
    }

    // --- Visual divider between current and forecast sections ---
    // A thin shaded band creates clear separation without heavy ink.
    int dividerY = 275;
    d.fillRectShade(MARGIN_X, dividerY, DISPLAY_WIDTH - 2 * MARGIN_X, 3, 180);

    // --- 3-day forecast section ---
    // Better vertical balance: forecast starts at y=300.
    y = 300;
    d.drawTextCentered(y, "--- Next 3 Days ---", 1);
    y += 44;

    // Day labels anchor at local midnight of the FETCH day: Open-Meteo's
    // daily arrays start at "today" in WEATHER_TZ (UTC+8 == CAL_TZ_OFFSET_SEC),
    // so weekday(todayStartUtc(fetchedUtc) + i*86400) labels day i correctly.
    int64_t d0 = core::todayStartUtc(_snap.fetchedUtc, CAL_TZ_OFFSET_SEC);
    for (int i = 0; i < _snap.dayCount && i < WEATHER_FORECAST_DAYS; i++, y += 42) {
        const core::WeatherDay &day = _snap.days[i];
        int wd = core::weekdayFromUtc(d0 + (int64_t)i * 86400, CAL_TZ_OFFSET_SEC);

        // Better aligned forecast rows: consistent column positions.
        // Day label (fixed width), temp range, glyph, description.
        String row = String(WD_SHORT[wd]) + "  "
                   + fmtTenths(day.tMin) + " .. " + fmtTenths(day.tMax) + " C"
                   + "   [" + core::weatherCodeGlyph(day.weatherCode) + "] "
                   + codeName(day.weatherCode);
        d.drawBookText(MARGIN_X + 20, y, row);
    }

    // Footer: gesture legend.
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14, "Hold=menu");
    d.flush(true);   // full refresh: whole screen rewritten, kill ghosting
}

// --- Menu --------------------------------------------------------------------
static String menuLabelFor(int i) {
    return i == 0 ? String("Refresh now") : String("Back to Home");
}

void WeatherApp::renderMenu() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(74, "Weather Menu", 2);

    int lh = d.lineHeightFor(1) + 16;
    int x  = DISPLAY_WIDTH / 2 - 220;
    int y  = 230;
    for (int i = 0; i < MENU_COUNT; i++) {
        String label = menuLabelFor(i);
        if (i == _menuSel) {
            int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label, 1);
        y += lh;
    }
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16, "Tap = move    Hold = select");
    d.flush(true);
}

void WeatherApp::openMenu() {
    _menuSel = 0;
    _screen  = Screen::Menu;
    renderMenu();
}

void WeatherApp::menuSelect() {
    if (_menuSel == 0) {
        runSync();
        return;
    }
    // "Back to Home"
    if (_ctx.manager) _ctx.manager->requestHome();
}

// --- Actions -------------------------------------------------------------------
void WeatherApp::runSync() {
    _screen  = Screen::Main;
    _syncing = true;                    // wantsSleep() == false for the duration
    _ctx.display.showMessage("Weather", "Fetching... (Wi-Fi + NTP)");

    WeatherSync sync(_ctx);
    WeatherSyncResult r = sync.run();   // blocking; safe stack (own 24 KB task)
    _syncing = false;

    loadCache();                        // pick up whatever the sync wrote

    Serial.printf("[Weather] sync: ok=%d http=%d msg='%s'\n",
                  (int)r.ok, r.httpStatus, r.message);
    _ctx.display.showMessage(r.ok ? "Fetch OK" : "Fetch failed", String(r.message));
    delay(1600);                        // let the user read the result
    renderMain();
}
