// ===========================================================================
//  DisplayManager.cpp
// ===========================================================================
#include "DisplayManager.h"
#include "config.h"
#include "core/UiStyle.h"   // ui::FOOTER_Y baseline token

#include "epd_driver.h"     // from LilyGo-EPD47
#include "firasans.h"       // bundled font in the LilyGo-EPD47 examples
#include "ReaderFont.h"      // compact Georgia body font (generated, ~14pt)
#include "ReaderFontSmall.h" // smaller Georgia body font (generated, ~10pt)

// Grayscale levels used across the UI (0 = black, 255 = white).
static const uint8_t INK   = 0;
static const uint8_t PAPER = 255;

// Active book/body font for the current size selection.
static const GFXfont *readerFontFor(bool small) {
    return small ? &ReaderFontSmall : &ReaderFont;
}

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

int DisplayManager::readerLineHeight() const { return readerFontFor(_readerSmall)->advance_y; }
int DisplayManager::readerAscender()   const { return readerFontFor(_readerSmall)->ascender;  }

int DisplayManager::textWidth(const String &text, bool book) const {
    if (!text.length()) return 0;
    const GFXfont *f = book ? readerFontFor(_readerSmall) : (const GFXfont *)&FiraSans;
    int32_t cx = 0, cy = 0, x1, y1, w, h;
    get_text_bounds((GFXfont *)f, text.c_str(), &cx, &cy, &x1, &y1, &w, &h, NULL);
    return (int)w;
}

int DisplayManager::usableWidth()  const { return DISPLAY_WIDTH  - 2 * MARGIN_X; }
int DisplayManager::usableHeight() const { return DISPLAY_HEIGHT - MARGIN_Y - STATUS_H; }

void DisplayManager::drawText(int x, int y, const String &text) {
    int cx = x, cy = y;
    // FiraSans is the single UI font shipped with the driver examples — one
    // size, no hierarchy (the old fontSize parameter was a no-op; STD·R1
    // removed it). Visual hierarchy comes from baseline placement (ui::
    // tokens) and the font choice (FiraSans vs Georgia drawBookText).
    writeln((GFXfont *)&FiraSans, text.c_str(), &cx, &cy, _framebuffer);
}

void DisplayManager::drawBookText(int x, int y, const String &text) {
    int cx = x, cy = y;
    writeln((GFXfont *)readerFontFor(_readerSmall), text.c_str(), &cx, &cy, _framebuffer);
}

void DisplayManager::drawTextCentered(int y, const String &text) {
    int32_t cx = 0, cy = 0, x1, y1, w, h;
    // NOTE: get_text_bounds() takes x/y as int32_t* cursors (not values); passing
    // literal 0 would be a NULL pointer and crash on the first dereference.
    get_text_bounds((GFXfont *)&FiraSans, text.c_str(), &cx, &cy, &x1, &y1, &w, &h, NULL);
    int x = (DISPLAY_WIDTH - w) / 2;
    drawText(x, y, text);
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

void DisplayManager::fillRect(int x, int y, int w, int h) {
    // Solid-ink filled rectangle; the driver clips to the panel bounds.
    epd_fill_rect(x, y, w, h, INK, _framebuffer);
}

void DisplayManager::fillRectShade(int x, int y, int w, int h, uint8_t shade) {
    // Grayscale filled rectangle (0 = black .. 255 = white); the driver maps the
    // shade's high nibble into the 4-bpp framebuffer and clips to the panel.
    epd_fill_rect(x, y, w, h, shade, _framebuffer);
}

void DisplayManager::blitRaw(const uint8_t *data, size_t len) {
    if (!data || !_framebuffer) return;
    const size_t fbBytes = (size_t)EPD_WIDTH * EPD_HEIGHT / 2;   // 259200
    if (len > fbBytes) len = fbBytes;                            // clamp to buffer
    memcpy(_framebuffer, data, len);
}

void DisplayManager::showMessage(const String &title, const String &body) {
    clearBuffer();
    drawTextCentered(DISPLAY_HEIGHT / 2 - 40, title);
    drawTextCentered(DISPLAY_HEIGHT / 2 + 20, body);
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

void DisplayManager::drawFooter(const String &left, const String &right) {
    // Shared footer contract: gesture legend on the left, optional status/
    // sync stamp right-aligned on the same ui::FOOTER_Y baseline (Georgia),
    // so every app's "last update" info lives in the bottom-right corner.
    // The right side is truncated if the pair would overflow the usable width.
    drawBookText(MARGIN_X, ui::FOOTER_Y, left);
    if (right.length() == 0) return;
    const int usable = DISPLAY_WIDTH - 2 * MARGIN_X;
    const int lw = textWidth(left, true);
    String r = right;
    while (r.length() > 0 && lw + textWidth(r, true) > usable)
        r = r.substring(0, r.length() - 1);
    if (r.length() > 0)
        drawBookText(DISPLAY_WIDTH - MARGIN_X - textWidth(r, true),
                     ui::FOOTER_Y, r);
}
