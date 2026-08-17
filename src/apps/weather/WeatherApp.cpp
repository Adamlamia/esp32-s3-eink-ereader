// ===========================================================================
//  WeatherApp.cpp  —  weather UI implementation (WTH·R1 + R2 redesign)
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
    _view   = View::Today;
    _menuSel = 0;

    // On-open resync (see header): only when stale/empty AND the cheap
    // pre-checks pass. shouldAutoSyncOnEnter() never touches the radio
    // itself, so a declined auto-sync simply renders the cache/empty state.
    if (shouldAutoSyncOnEnter()) {
        Serial.printf("[Weather] on-open resync (fetched=%lld batt=%d%%)\n",
                      (long long)_snap.fetchedUtc, _ctx.batteryPct);
        runSync();          // splash -> sync -> reload -> renderCurrent
    } else {
        renderCurrent();
    }
}

void WeatherApp::onExit() {
    // Cache stays in memory; just reset navigation for a clean re-entry.
    _screen = Screen::Main;
    _view   = View::Today;
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

    // --- Views ---
    if (ev == ButtonEvent::Tap) {
        _view = (View)(((int)_view + 1) % 2);   // Today -> Week -> Today
        renderCurrent();
    } else if (ev == ButtonEvent::MediumHold) {
        if (_ctx.manager) _ctx.manager->requestHome();
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

// --- Rendering: dispatch ----------------------------------------------------
void WeatherApp::renderCurrent() {
    switch (_view) {
        case View::Today: renderToday(); break;
        case View::Week:  renderWeek();  break;
    }
}

// --- Rendering: Today (current conditions + 12-slot 2-hour hourly grid) ------
void WeatherApp::renderToday() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();

    // --- Title + sync time (well separated to avoid overlap) ---
    d.drawTextCentered(24, String(_snap.label), 2);     // fontSize=2 ~40px -> 24..64
    if (_snap.fetchedUtc > 0) {
        int64_t y_dummy; unsigned m_dummy, dd, hh, mm, ss;
        core::civilFromUtc(_snap.fetchedUtc, CAL_TZ_OFFSET_SEC, y_dummy, m_dummy, dd, hh, mm, ss);
        char timeBuf[16];
        snprintf(timeBuf, sizeof(timeBuf), "Updated %02u:%02u", hh, mm);
        d.drawTextCentered(72, String(timeBuf), 1);     // fontSize=1 ~20px -> 72..92
    } else {
        d.drawTextCentered(72, "Never fetched", 1);
    }

    const bool haveData = _snap.cur.valid || _snap.hourCount > 0;
    if (!haveData) {
        d.drawTextCentered(260, "No weather yet", 2);
        d.drawTextCentered(310, "LongHold -> menu -> Refresh now", 1);
        d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 20, "Tap=view   M-Hold=home   Hold=menu");
        d.flush(true);
        return;
    }

    // --- Current conditions block ---
    int y = 100;
    if (_snap.cur.valid) {
        // Temperature hero (left side, shaded background)
        String tempStr = fmt1(_snap.cur.tempC) + " C";
        int tw = d.textWidth(tempStr, false);
        d.fillRectShade(MARGIN_X, y - 4, tw + 24, 44, 230);
        d.drawText(MARGIN_X + 12, y, tempStr, 2);       // fontSize=2 ~40px -> y..y+40

        // Condition badge (right side, shaded badge for icon-like appearance)
        String glyph = String(core::weatherCodeGlyph(_snap.cur.weatherCode));
        String desc  = String(codeName(_snap.cur.weatherCode));
        int gw = d.textWidth(glyph, false);
        int badgeW = gw + 20;
        int badgeX = 480;
        d.fillRectShade(badgeX, y + 6, badgeW, 28, 210);   // darker badge bg
        d.drawBookText(badgeX + 10, y + 10, glyph);        // glyph inside badge
        d.drawBookText(badgeX + badgeW + 10, y + 10, desc); // description next to badge

        y += 48;   // below the temp hero

        // Details line
        d.drawBookText(MARGIN_X, y,
            String("Feels ") + fmt1(_snap.cur.feelsC) + " C"
            + "  |  Humidity " + String(_snap.cur.humidityPct) + "%"
            + "  |  Wind " + fmt1(_snap.cur.windKph) + " km/h");
    } else {
        d.drawBookText(MARGIN_X, y, "Current conditions unavailable");
    }

    // --- Hourly section header (no divider — clean transition) ---
    d.drawText(MARGIN_X, 170, "TODAY  -  2-HOUR FORECAST", 1);

    // --- Hourly grid: 2 rows x 6 columns ---
    int colW = (DISPLAY_WIDTH - 2 * MARGIN_X) / 6;   // ~151px per column
    int rowY = 200;   // first row baseline

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 6; col++) {
            int idx = row * 6 + col;
            int cx = MARGIN_X + col * colW;   // column left edge
            int centerX = cx + colW / 2;       // center of column

            if (idx < _snap.hourCount && idx < WEATHER_HOURLY_SLOTS && _snap.hours[idx].valid) {
                const core::WeatherHour &h = _snap.hours[idx];

                // Hour label (centered in column)
                char hBuf[4];
                snprintf(hBuf, sizeof(hBuf), "%02d", h.hourLocal);
                int hw = d.textWidth(String(hBuf), false);
                d.drawText(centerX - hw / 2, rowY, String(hBuf), 1);

                // Weather glyph badge (shaded background = icon-like)
                String glyph = String(core::weatherCodeGlyph(h.weatherCode));
                int gw = d.textWidth(glyph, false);
                int badgeW = gw + 18;
                int badgeX = centerX - badgeW / 2;
                d.fillRectShade(badgeX, rowY + 30, badgeW, 26, 210);
                d.drawBookText(centerX - gw / 2, rowY + 34, glyph);

                // Temperature (centered, with light shaded bg)
                String tStr = fmtTenths(h.tempTenths);
                int ttw = d.textWidth(tStr, false);
                d.fillRectShade(centerX - ttw / 2 - 8, rowY + 64, ttw + 16, 24, 240);
                d.drawText(centerX - ttw / 2, rowY + 66, tStr, 1);
            } else {
                // Empty slot
                d.drawText(centerX - 4, rowY + 34, "-", 1);
            }
        }
        rowY += 130;   // next row: 130px below (90px cell + 40px gap)
    }

    // Footer
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 20, "Tap=view   M-Hold=home   Hold=menu");
    d.flush(true);   // full refresh: whole screen rewritten, kill ghosting
}

// --- Rendering: Week (7-day forecast, compact rows) --------------------------
void WeatherApp::renderWeek() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();

    // --- Title + sync time (same spacing as Today view) ---
    d.drawTextCentered(24, String(_snap.label), 2);
    if (_snap.fetchedUtc > 0) {
        int64_t y_dummy; unsigned m_dummy, dd, hh, mm, ss;
        core::civilFromUtc(_snap.fetchedUtc, CAL_TZ_OFFSET_SEC, y_dummy, m_dummy, dd, hh, mm, ss);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "Updated %02u:%02u  -  %d days", hh, mm, _snap.dayCount);
        d.drawTextCentered(72, String(timeBuf), 1);
    } else {
        d.drawTextCentered(72, "Never fetched", 1);
    }

    const bool haveData = _snap.dayCount > 0;
    if (!haveData) {
        d.drawTextCentered(260, "No forecast data", 2);
        d.drawTextCentered(310, "LongHold -> menu -> Refresh now", 1);
        d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 20, "Tap=view   M-Hold=home   Hold=menu");
        d.flush(true);
        return;
    }

    // Section header
    d.drawText(MARGIN_X, 100, "7-DAY FORECAST", 1);

    // Day rows — 42px spacing for clear separation
    int y = 130;
    int64_t d0 = core::todayStartUtc(_snap.fetchedUtc, CAL_TZ_OFFSET_SEC);
    for (int i = 0; i < _snap.dayCount && i < WEATHER_FORECAST_DAYS; i++, y += 42) {
        const core::WeatherDay &day = _snap.days[i];
        int wd = core::weekdayFromUtc(d0 + (int64_t)i * 86400, CAL_TZ_OFFSET_SEC);

        // Highlight today (day 0) with a shaded background
        if (i == 0) {
            d.fillRectShade(MARGIN_X - 4, y - 4, DISPLAY_WIDTH - 2 * MARGIN_X + 8, 30, 235);
        }

        // Day name + temps + glyph badge + description
        String dayName = String(WD_SHORT[wd]);
        String temps   = fmtTenths(day.tMin) + " .. " + fmtTenths(day.tMax) + " C";
        String glyph   = String(core::weatherCodeGlyph(day.weatherCode));
        String desc    = String(codeName(day.weatherCode));

        // Draw day name (fixed position)
        d.drawBookText(MARGIN_X + 8, y, dayName);

        // Draw temps (after day name, aligned)
        int tx = MARGIN_X + 80;
        d.drawBookText(tx, y, temps);

        // Draw glyph badge (shaded background)
        int gx = MARGIN_X + 320;
        int gw = d.textWidth(glyph, false);
        d.fillRectShade(gx - 6, y - 2, gw + 12, 24, 215);
        d.drawBookText(gx, y, glyph);

        // Draw description (after badge)
        d.drawBookText(gx + gw + 16, y, desc);
    }

    // Footer
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 20, "Tap=view   M-Hold=home   Hold=menu");
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
    renderCurrent();
}
