// ===========================================================================
//  DisplayManager.cpp
// ===========================================================================
#include "DisplayManager.h"
#include "config.h"

#include "epd_driver.h"     // from LilyGo-EPD47
#include "firasans.h"       // bundled font in the LilyGo-EPD47 examples

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

int DisplayManager::usableWidth()  const { return DISPLAY_WIDTH  - 2 * MARGIN_X; }
int DisplayManager::usableHeight() const { return DISPLAY_HEIGHT - 2 * MARGIN_Y - 30; }

void DisplayManager::drawText(int x, int y, const String &text, uint8_t /*fontSize*/) {
    int cx = x, cy = y;
    // FiraSans is the reference font shipped with the driver examples. Swapping
    // fonts per size is a TODO; for now scale via the layout line height.
    writeln((GFXfont *)&FiraSans, text.c_str(), &cx, &cy, _framebuffer);
}

void DisplayManager::drawTextCentered(int y, const String &text, uint8_t fontSize) {
    int32_t x1, y1, w, h;
    get_text_bounds((GFXfont *)&FiraSans, text.c_str(), 0, 0, &x1, &y1, &w, &h, NULL);
    int x = (DISPLAY_WIDTH - w) / 2;
    drawText(x, y, text, fontSize);
}

void DisplayManager::showMessage(const String &title, const String &body) {
    clearBuffer();
    drawTextCentered(DISPLAY_HEIGHT / 2 - 40, title, 2);
    drawTextCentered(DISPLAY_HEIGHT / 2 + 20, body, 1);
    flush(true);
}

void DisplayManager::showStatusBar(const String &bookTitle, int page,
                                   int totalPages, int batteryPct) {
    // Drawn at the very bottom of the framebuffer, just above the flush.
    String left  = bookTitle;
    if (left.length() > 40) left = left.substring(0, 37) + "...";
    String right = "p." + String(page) + "/" + String(totalPages) +
                   "  " + String(batteryPct) + "%";

    int y = DISPLAY_HEIGHT - 12;
    drawText(MARGIN_X, y, left, 0);

    int32_t x1, y1, w, h;
    get_text_bounds((GFXfont *)&FiraSans, right.c_str(), 0, 0, &x1, &y1, &w, &h, NULL);
    drawText(DISPLAY_WIDTH - MARGIN_X - w, y, right, 0);
}
