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

    // Bold outline box used to make the selected menu / library row obvious.
    void drawSelectionBox(int x, int y, int w, int h, int thickness = 3);

    // Compact reading font (Georgia ~14pt) used for book body + status bar.
    void drawBookText(int x, int y, const String &text);
    int  readerLineHeight() const;      // advance_y of the reading font
    int  readerAscender()   const;      // baseline offset of the reading font
    // Pixel width of a string; book=true measures with the reading font.
    int  textWidth(const String &text, bool book) const;

    // High-level screens
    void showMessage(const String &title, const String &body);
    void showStatusBar(const String &bookTitle, int page, int totalPages);

    int  lineHeightFor(uint8_t fontSize) const;
    int  usableWidth()  const;
    int  usableHeight() const;

    // Wi-Fi portal state, surfaced in the status bar as a small indicator.
    void setWifiState(bool on) { _wifiOn = on; }
    bool wifiOn() const { return _wifiOn; }

    // Latest battery reading (0..100), shown in the status bar.
    void setBattery(int pct) { _batteryPct = pct; }
    int  battery() const { return _batteryPct; }

private:
    uint8_t *_framebuffer = nullptr;        // full-screen 4-bpp buffer (PSRAM)
    int      _pagesSinceFullRefresh = 0;
    bool     _wifiOn = false;
    int      _batteryPct = 100;
};
