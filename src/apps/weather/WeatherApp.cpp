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
#include "core/UiStyle.h"      // ui:: shared baseline anchors (STD·R1)

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
// Spacing model: every y value is a BASELINE (text sits on it, ascender above,
// descender below).  FiraSans lineHeightFor(1)=44 (~30px ascender + ~8px gap).
// drawText always renders FiraSans (single UI font, no size hierarchy), so
// visual hierarchy comes from baseline spacing and font choice (drawText=
// FiraSans vs drawBookText=Georgia), not from font size.
void WeatherApp::renderToday() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();

    // --- Title (sync stamp lives in the footer, bottom-right) ---
    d.drawTextCentered(ui::TITLE_Y, String(_snap.label));

    const bool haveData = _snap.cur.valid || _snap.hourCount > 0;
    if (!haveData) {
        d.drawTextCentered(260, "No weather yet");
        d.drawTextCentered(294, "LongHold -> menu -> Refresh now");
        d.drawFooter("Tap=view   M-Hold=home   Hold=menu", lastSyncLine());
        d.flush(true);
        return;
    }

    // --- Current conditions (two-column: temp left, condition right) ---
    int y = 122;   // baseline for temp
    if (_snap.cur.valid) {
        String tempStr = fmt1(_snap.cur.tempC) + " C";
        d.drawText(MARGIN_X, y, tempStr);

        // Condition: glyph (Georgia) + description (Georgia) on right side
        String glyph = String(core::weatherCodeGlyph(_snap.cur.weatherCode));
        String desc  = String(codeName(_snap.cur.weatherCode));
        d.drawBookText(500, y, glyph + "  " + desc);

        // Details line (one line below: ~34px advance)
        y += 34;
        d.drawBookText(MARGIN_X, y,
            String("Feels ") + fmt1(_snap.cur.feelsC) + " C"
            + "  |  Humidity " + String(_snap.cur.humidityPct) + "%"
            + "  |  Wind " + fmt1(_snap.cur.windKph) + " km/h");
    } else {
        d.drawBookText(MARGIN_X, y, "Current conditions unavailable");
    }

    // --- Hourly section header ---
    d.drawText(MARGIN_X, 200, "TODAY - 2-HOUR FORECAST");

    // --- Hourly grid: 2 rows x 6 columns ---
    // Each cell has 3 stacked elements at 30px baseline intervals, all in the
    // Georgia reading font (drawBookText) — genuinely smaller than FiraSans,
    // which keeps the grid quiet and lets the current-conditions block lead.
    int colW = (DISPLAY_WIDTH - 2 * MARGIN_X) / 6;   // ~151px per column
    int rowY = 238;   // first row: hour baseline

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 6; col++) {
            int idx = row * 6 + col;
            int cx = MARGIN_X + col * colW;   // column left edge
            int centerX = cx + colW / 2;       // center of column

            if (idx < _snap.hourCount && idx < WEATHER_HOURLY_SLOTS && _snap.hours[idx].valid) {
                const core::WeatherHour &h = _snap.hours[idx];

                // Hour label (Georgia)
                char hBuf[4];
                snprintf(hBuf, sizeof(hBuf), "%02d", h.hourLocal);
                int hw = d.textWidth(String(hBuf), true);
                d.drawBookText(centerX - hw / 2, rowY, String(hBuf));

                // Weather glyph (Georgia)
                String glyph = String(core::weatherCodeGlyph(h.weatherCode));
                int gw = d.textWidth(glyph, true);
                d.drawBookText(centerX - gw / 2, rowY + 30, glyph);

                // Temperature (Georgia)
                String tStr = fmtTenths(h.tempTenths);
                int ttw = d.textWidth(tStr, true);
                d.drawBookText(centerX - ttw / 2, rowY + 60, tStr);
            } else {
                d.drawBookText(centerX - 4, rowY + 30, "-");
            }
        }
        rowY += 125;   // next row: 125px (60px cell content + 65px gap)
    }

    // Footer: legend left, last-fetch stamp right (shared contract)
    d.drawFooter("Tap=view   M-Hold=home   Hold=menu", lastSyncLine());
    d.flush(true);   // full refresh: whole screen rewritten, kill ghosting
}

// --- Rendering: Week (7-day forecast, compact rows) --------------------------
void WeatherApp::renderWeek() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();

    // --- Title (sync stamp lives in the footer, bottom-right) ---
    d.drawTextCentered(ui::TITLE_Y, String(_snap.label));

    const bool haveData = _snap.dayCount > 0;
    if (!haveData) {
        d.drawTextCentered(260, "No forecast data");
        d.drawTextCentered(294, "LongHold -> menu -> Refresh now");
        d.drawFooter("Tap=view   M-Hold=home   Hold=menu", lastSyncLine());
        d.flush(true);
        return;
    }

    // Section header (lowered per user request)
    d.drawText(MARGIN_X, 118, "7-DAY FORECAST");

    // Day rows — baseline advance matches CalendarApp (readerLineHeight + ROW_GAP).
    // Extra gap below the section header keeps the grid from feeling cramped.
    int lh = d.readerLineHeight() + ui::ROW_GAP;   // ~41px per row
    int y  = 174;                        // first row baseline
    int64_t d0 = core::todayStartUtc(_snap.fetchedUtc, CAL_TZ_OFFSET_SEC);
    for (int i = 0; i < _snap.dayCount && i < WEATHER_FORECAST_DAYS; i++, y += lh) {
        const core::WeatherDay &day = _snap.days[i];
        int wd = core::weekdayFromUtc(d0 + (int64_t)i * 86400, CAL_TZ_OFFSET_SEC);

        // Highlight today (day 0) with a selection box
        if (i == 0) {
            int rowW = d.usableWidth() + 20;
            d.drawSelectionBox(MARGIN_X - 10, y - d.readerAscender() - 4, rowW, lh - 2);
        }

        // Day name + temps + glyph + description (fixed x positions)
        String dayName = String(WD_SHORT[wd]);
        String temps   = fmtTenths(day.tMin) + " .. " + fmtTenths(day.tMax) + " C";
        String glyph   = String(core::weatherCodeGlyph(day.weatherCode));
        String desc    = String(codeName(day.weatherCode));

        d.drawBookText(MARGIN_X + 8, y, dayName);
        d.drawBookText(MARGIN_X + 80, y, temps);
        d.drawBookText(MARGIN_X + 320, y, glyph);          // glyph (Georgia, distinct)
        d.drawBookText(MARGIN_X + 370, y, desc);
    }

    // Footer: legend left, last-fetch stamp right (shared contract)
    d.drawFooter("Tap=view   M-Hold=home   Hold=menu", lastSyncLine());
    d.flush(true);   // full refresh: whole screen rewritten, kill ghosting
}

// --- Menu --------------------------------------------------------------------
static String menuLabelFor(int i) {
    return i == 0 ? String("Refresh now") : String("Back to Home");
}

void WeatherApp::renderMenu() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(ui::MENU_TITLE_Y, "Weather Menu");

    int lh = d.lineHeightFor(1) + 16;
    int x  = DISPLAY_WIDTH / 2 - 220;
    int y  = ui::MENU_START_Y;
    for (int i = 0; i < MENU_COUNT; i++) {
        String label = menuLabelFor(i);
        if (i == _menuSel) {
            int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label);
        y += lh;
    }
    d.drawBookText(MARGIN_X, ui::MENU_FOOTER_Y, "Tap = move    Hold = select");
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
