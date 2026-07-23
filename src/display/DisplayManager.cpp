// ===========================================================================
//  DisplayManager.cpp
// ===========================================================================
#include "DisplayManager.h"
#include "config.h"

#include "epd_driver.h"     // from LilyGo-EPD47
#include "firasans.h"       // bundled font in the LilyGo-EPD47 examples
#include "ReaderFont.h"     // compact Georgia body font (generated)

// Grayscale levels used across the UI (0 = black, 255 = white).
static const uint8_t INK   = 0;
static const uint8_t PAPER = 255;

bool DisplayManager::begin() {
    epd_init();
    _framebuffer = (uint8_t *)ps_calloc(EPD_WIDTH * EPD_HEIGHT / 2, sizeof(uint8_t));
    if (!_framebuffer) {
        Serial.println("[Display] framebuffer alloc failed (need PSRAM)");
        return false;
    }
    fullClear();
    return true;
}

void DisplayManager::clearBuffer() {
    memset(_framebuffer, PAPER, EPD_WIDTH * EPD_HEIGHT / 2);
}

void DisplayManager::fullClear() {
    epd_poweron();
    epd_clear();
    epd_poweroff();
    _pagesSinceFullRefresh = 0;
}

void DisplayManager::flush(bool fullRefresh) {
    // Periodic full clear keeps the panel free of grey "ghost" residue.
    if (fullRefresh || ++_pagesSinceFullRefresh >= FULL_REFRESH_EVERY) {
        fullClear();
    }
    epd_poweron();
    epd_draw_grayscale_image(epd_full_screen(), _framebuffer);
    epd_poweroff();
}

int DisplayManager::lineHeightFor(uint8_t fontSize) const {
    switch (fontSize) {
        case 0:  return 34;
        case 2:  return 58;
        default: return 44;
    }
}

int DisplayManager::readerLineHeight() const { return ReaderFont.advance_y; }
int DisplayManager::readerAscender()   const { return ReaderFont.ascender;  }

int DisplayManager::textWidth(const String &text, bool book) const {
    if (!text.length()) return 0;
    const GFXfont *f = book ? &ReaderFont : (const GFXfont *)&FiraSans;
    int32_t cx = 0, cy = 0, x1, y1, w, h;
    get_text_bounds((GFXfont *)f, text.c_str(), &cx, &cy, &x1, &y1, &w, &h, NULL);
    return (int)w;
}

int DisplayManager::usableWidth()  const { return DISPLAY_WIDTH  - 2 * MARGIN_X; }
int DisplayManager::usableHeight() const { return DISPLAY_HEIGHT - MARGIN_Y - STATUS_H; }

void DisplayManager::drawText(int x, int y, const String &text, uint8_t /*fontSize*/) {
    int cx = x, cy = y;
    // FiraSans is the reference font shipped with the driver examples. Swapping
    // fonts per size is a TODO; for now scale via the layout line height.
    writeln((GFXfont *)&FiraSans, text.c_str(), &cx, &cy, _framebuffer);
}

void DisplayManager::drawBookText(int x, int y, const String &text) {
    int cx = x, cy = y;
    writeln((GFXfont *)&ReaderFont, text.c_str(), &cx, &cy, _framebuffer);
}

void DisplayManager::drawTextCentered(int y, const String &text, uint8_t fontSize) {
    int32_t cx = 0, cy = 0, x1, y1, w, h;
    // NOTE: get_text_bounds() takes x/y as int32_t* cursors (not values); passing
    // literal 0 would be a NULL pointer and crash on the first dereference.
    get_text_bounds((GFXfont *)&FiraSans, text.c_str(), &cx, &cy, &x1, &y1, &w, &h, NULL);
    int x = (DISPLAY_WIDTH - w) / 2;
    drawText(x, y, text, fontSize);
}

void DisplayManager::drawSelectionBox(int x, int y, int w, int h, int thickness) {
    // A few concentric rectangles give a bold, clearly visible border without an
    // inverted fill (the font path only draws dark text, so white-on-black is not
    // available). Clamped so it never runs off the panel edges.
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int i = 0; i < thickness; i++)
        epd_draw_rect(x - i, y - i, w + 2 * i, h + 2 * i, INK, _framebuffer);
}

void DisplayManager::showMessage(const String &title, const String &body) {
    clearBuffer();
    drawTextCentered(DISPLAY_HEIGHT / 2 - 40, title, 2);
    drawTextCentered(DISPLAY_HEIGHT / 2 + 20, body, 1);
    flush(true);
}

void DisplayManager::showStatusBar(const String &bookTitle, int page,
                                   int totalPages) {
    // Drawn small (reading font) at the very bottom, just above the flush.
    String left  = bookTitle;
    if (left.length() > 46) left = left.substring(0, 43) + "...";
    String right = "p." + String(page) + "/" + String(totalPages) +
                   "   " + String(_batteryPct) + "%";
    // Small on-air marker so you can tell at a glance the upload portal is live.
    if (_wifiOn) right = "WiFi   " + right;

    int y = DISPLAY_HEIGHT - 7;
    drawBookText(MARGIN_X, y, left);
    drawBookText(DISPLAY_WIDTH - MARGIN_X - textWidth(right, true), y, right);
}
