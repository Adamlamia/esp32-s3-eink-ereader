#pragma once
// ===========================================================================
//  DisplayManager  —  thin wrapper around the LilyGo-EPD47 driver
// ===========================================================================
//  Owns the e-paper framebuffer and offers convenience helpers for the
//  reader: clearing, drawing wrapped text pages, status bars and full/partial
//  refresh handling to keep ghosting under control.
// ===========================================================================
#include <Arduino.h>

class DisplayManager {
public:
    bool begin();

    // Frame lifecycle: clearBuffer() -> draw* -> flush()
    void clearBuffer();
    void flush(bool fullRefresh = false);
    void fullClear();                       // hardware clear (removes ghosting)

    // Text helpers (coordinates in pixels, origin top-left)
    void drawText(int x, int y, const String &text, uint8_t fontSize = 1);
    void drawTextCentered(int y, const String &text, uint8_t fontSize = 1);

    // High-level screens
    void showMessage(const String &title, const String &body);
    void showStatusBar(const String &bookTitle, int page, int totalPages,
                       int batteryPct);

    int  lineHeightFor(uint8_t fontSize) const;
    int  usableWidth()  const;
    int  usableHeight() const;

private:
    uint8_t *_framebuffer = nullptr;        // full-screen 4-bpp buffer (PSRAM)
    int      _pagesSinceFullRefresh = 0;
};
