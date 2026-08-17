// ===========================================================================
//  QrApp.cpp  —  QR Toolkit application (QR·R1)
// ===========================================================================
#include "QrApp.h"
#include "app/AppManager.h"       // requestHome()
#include "config.h"
#include "core/Emvco.h"           // payload-kind detection (emvcoIsValid)
#include "core/UiStyle.h"         // ui:: shared baseline anchors (STD·R1)

#include <qrcode.h>               // ricmoo/QRCode (firmware-only lib_deps)
#include <cstring>
#include <cstdio>

// --- QR rendering layout (960x540 panel) ------------------------------------
// The QR bitmap is centred inside a square box of QR_RENDER_PX pixels. 420 is
// the most the 540 px panel height allows once the label (top), the payload-
// type caption and the gesture legend (bottom) are accounted for; the prompt
// target was "~440" — 420 keeps every element on-screen without overlap.
static const int QR_RENDER_PX = 420;    // square box the QR is centred within
static const int QR_AREA_TOP  = 56;     // top of that box (below the label)
static const int QR_LABEL_Y   = 44;     // baseline of the large label
static const int QR_CAPTION_Y = 508;    // baseline of the kind caption

// --- Encoder capacity guard ---------------------------------------------------
// Canonical byte-mode data capacity (CHARACTERS) at ECC LOW per QR version
// (ISO/IEC 18004, versions 1..13). ricmoo/QRCode does NOT return an error
// when the data is too big (upstream "@TODO: Return error if data is too
// big"), so this table guards qrcode_initText: a payload is only encoded at
// a version whose capacity it fits. QR_PAYLOAD_MAX (320) <= capacity[13]
// (425), so any payload that fits our buffers fits some supported version.
static const uint16_t QR_BYTE_CAPACITY_LOW[14] = {
    0, 17, 32, 53, 78, 106, 134, 154, 192, 230, 271, 321, 367, 425
};

// Module-bitmap buffer bytes for a version, mirroring the library's own
// qrcode_getBufferSize() formula: one bit per module of the (4v+17)^2 grid,
// rounded up to whole bytes. v13 -> (69*69+7)/8 = 596 bytes.
static constexpr int qrModuleBufBytes(int version) {
    return ((4 * version + 17) * (4 * version + 17) + 7) / 8;   // C++11: one return
}

// ===========================================================================
QrApp::QrApp(SystemContext &ctx) : _ctx(ctx) {
    core::qrListClear(_entries);
}

// --- Lifecycle -----------------------------------------------------------------
void QrApp::onEnter() {
    buildEntries();
    _index  = 0;
    _screen = Screen::Main;
    renderMain();
}

void QrApp::onExit() {
    // Nothing to persist: the carousel is rebuilt from (immutable) secrets on
    // every onEnter, and no radios were touched.
}

// --- Input ---------------------------------------------------------------------
void QrApp::onButton(ButtonEvent ev) {
    // --- Menu screen ---
    if (_screen == Screen::Menu) {
        if (ev == ButtonEvent::Tap) {
            _menuSel = (_menuSel + tapCount()) % MENU_COUNT;   // wraps (single item)
            renderMenu();
        } else if (ev == ButtonEvent::LongHold) {
            menuSelect();
        }
        // MediumHold in the menu: no-op (same convention as the other apps).
        return;
    }

    // --- Main screen ---
    if (ev == ButtonEvent::Tap) {
        if (!core::qrListIsEmpty(_entries)) {
            _index = (_index + tapCount()) % _entries.count;   // cycle entries, wrap
            renderMain();
        }
        // Tap in the EMPTY state: nothing to cycle — deliberately ignored
        // (the screen already explains how to add entries).
    } else if (ev == ButtonEvent::MediumHold) {
        // Deliberate no-op (documented in the header): single-QR screen with
        // nothing to scroll. Ignored rather than repurposed so the gesture
        // map stays predictable — exactly like WeatherApp's documented no-op.
    } else if (ev == ButtonEvent::LongHold) {
        openMenu();
    }
}

// --- Entry construction -----------------------------------------------------------
void QrApp::buildEntries() {
    core::qrListClear(_entries);

    // 1. WiFi QR, auto-generated from the existing STA creds when present.
#if defined(WIFI_STA_SSID)
    if (WIFI_STA_SSID[0] != '\0') {
        core::qrListAddWifi(_entries, WIFI_STA_SSID,
#ifdef WIFI_STA_PASS
                            WIFI_STA_PASS,
#else
                            "",                    // SSID but no pass -> open net
#endif
                            "Wi-Fi");
    }
#endif

    // 2. secrets.h QR_PAYLOAD_n / QR_LABEL_n pairs. Each pair is individually
    //    optional; a missing QR_LABEL_n synthesises "QR n+1" in addSecretEntry.
#ifdef QR_PAYLOAD_0
    addSecretEntry(0, QR_PAYLOAD_0
#ifdef QR_LABEL_0
        , QR_LABEL_0
#else
        , nullptr
#endif
    );
#endif
#ifdef QR_PAYLOAD_1
    addSecretEntry(1, QR_PAYLOAD_1
#ifdef QR_LABEL_1
        , QR_LABEL_1
#else
        , nullptr
#endif
    );
#endif
#ifdef QR_PAYLOAD_2
    addSecretEntry(2, QR_PAYLOAD_2
#ifdef QR_LABEL_2
        , QR_LABEL_2
#else
        , nullptr
#endif
    );
#endif
#ifdef QR_PAYLOAD_3
    addSecretEntry(3, QR_PAYLOAD_3
#ifdef QR_LABEL_3
        , QR_LABEL_3
#else
        , nullptr
#endif
    );
#endif
#ifdef QR_PAYLOAD_4
    addSecretEntry(4, QR_PAYLOAD_4
#ifdef QR_LABEL_4
        , QR_LABEL_4
#else
        , nullptr
#endif
    );
#endif
#ifdef QR_PAYLOAD_5
    addSecretEntry(5, QR_PAYLOAD_5
#ifdef QR_LABEL_5
        , QR_LABEL_5
#else
        , nullptr
#endif
    );
#endif
#ifdef QR_PAYLOAD_6
    addSecretEntry(6, QR_PAYLOAD_6
#ifdef QR_LABEL_6
        , QR_LABEL_6
#else
        , nullptr
#endif
    );
#endif
#ifdef QR_PAYLOAD_7
    addSecretEntry(7, QR_PAYLOAD_7
#ifdef QR_LABEL_7
        , QR_LABEL_7
#else
        , nullptr
#endif
    );
#endif
}

// Classify the payload (drives the caption) and append the entry.
void QrApp::addSecretEntry(int idx, const char *payload, const char *label) {
    char defLabel[12];
    if (!label || label[0] == '\0') {
        std::snprintf(defLabel, sizeof(defLabel), "QR %d", idx + 1);
        label = defLabel;
    }
    core::QrKind kind = core::QrKind::Text;
    if (std::strncmp(payload, "http://", 7) == 0 ||
        std::strncmp(payload, "https://", 8) == 0) {
        kind = core::QrKind::Url;
    } else if (core::emvcoIsValid(payload)) {
        kind = core::QrKind::Payment;     // valid EMVCo QRPS (structure + CRC)
    }
    core::qrListAdd(_entries, kind, label, payload);
}

// --- Rendering: main screen ---------------------------------------------------------
void QrApp::renderMain() {
    if (core::qrListIsEmpty(_entries)) {
        renderEmptyState();
        return;
    }
    if (_index < 0 || _index >= _entries.count) _index = 0;

    DisplayManager &d = _ctx.display;
    d.clearBuffer();

    const core::QrEntry &e = _entries.items[_index];
    d.drawTextCentered(QR_LABEL_Y, String(e.label));
    renderQr(e);                      // QR bitmap + kind caption (no flush)

    // Footer: gesture legend + position in the carousel.
    String footer = String("Tap=next   Hold=menu        ")
                  + String(_index + 1) + "/" + String(_entries.count);
    d.drawBookText(MARGIN_X, ui::FOOTER_Y, footer);
    d.flush(true);                    // full refresh: kill QR ghosting
}

void QrApp::renderEmptyState() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(70, "QR Toolkit");
    d.drawBookText(MARGIN_X, 190, "No QR entries configured yet.");
    d.drawBookText(MARGIN_X, 226, "Add them in src/secrets.h (git-ignored):");
    d.drawBookText(MARGIN_X, 262, "  #define QR_PAYLOAD_0  \"https://example.com\"");
    d.drawBookText(MARGIN_X, 290, "  #define QR_LABEL_0    \"Example\"");
    d.drawBookText(MARGIN_X, 326, "A Wi-Fi QR also appears automatically when");
    d.drawBookText(MARGIN_X, 354, "WIFI_STA_SSID / WIFI_STA_PASS are set.");
    d.drawBookText(MARGIN_X, ui::FOOTER_Y, "Hold=menu");
    d.flush(true);
}

// Encode the payload and draw it centred, with the kind caption beneath.
// SECURITY: only the kind caption is drawn — never the payload itself (a
// WiFi payload embeds the password; see the header note).
void QrApp::renderQr(const core::QrEntry &e) {
    DisplayManager &d = _ctx.display;

    // Pick the smallest version whose byte-mode capacity (ECC LOW) fits the
    // payload — guarding the library's missing data-too-big error.
    const size_t len = std::strlen(e.payload);
    uint8_t version = 0;
    for (uint8_t v = QR_MIN_VERSION; v <= QR_MAX_VERSION; ++v) {
        if (len <= QR_BYTE_CAPACITY_LOW[v]) { version = v; break; }
    }
    if (version == 0) {               // cannot happen while QR_PAYLOAD_MAX <=
        renderPayloadTooLong(e);      // capacity[QR_MAX_VERSION], kept as a
        return;                       // loud safety net
    }

    // Static encoder state: keeps ~600 B off the (8 KB) loop-task stack.
    // Single-threaded app code, so static lifetime is safe here.
    static QRCode  s_qr;
    static uint8_t s_qrData[qrModuleBufBytes(QR_MAX_VERSION)];

    const int8_t rc = qrcode_initText(&s_qr, s_qrData, version, ECC_LOW,
                                      e.payload);
    if (rc != 0) {                    // defensive: the capacity guard above
        renderPayloadTooLong(e);      // should make this unreachable
        return;
    }

    // Scale modules to fit the render box and centre the bitmap.
    const int modules = s_qr.size;                // 4*version + 17
    int scale = QR_RENDER_PX / modules;
    if (scale < 1) scale = 1;
    const int side = modules * scale;
    const int x0 = (DISPLAY_WIDTH - side) / 2;
    const int y0 = QR_AREA_TOP + (QR_RENDER_PX - side) / 2;

    // The framebuffer is already PAPER-white (clearBuffer); draw the dark
    // modules only. Each module is a filled square; adjacent modules merge
    // into solid finder/timing patterns naturally.
    for (uint8_t my = 0; my < s_qr.size; ++my) {
        for (uint8_t mx = 0; mx < s_qr.size; ++mx) {
            if (qrcode_getModule(&s_qr, mx, my)) {
                d.fillRect(x0 + (int)mx * scale, y0 + (int)my * scale,
                           scale, scale);
            }
        }
    }

    // Payload-type caption, centred beneath the bitmap.
    d.drawTextCentered(QR_CAPTION_Y, String(core::qrKindCaption(e.kind)));
}

void QrApp::renderPayloadTooLong(const core::QrEntry &e) {
    DisplayManager &d = _ctx.display;
    // The buffer was already cleared by renderMain's caller path; draw the
    // message into it (no extra clear so the label stays put).
    d.drawBookText(MARGIN_X, 220, "This QR payload is too long to encode at");
    d.drawBookText(MARGIN_X, 248, "the supported QR versions (max 425 chars).");
    d.drawBookText(MARGIN_X, 284, String("Entry: ") + String(e.label));
    d.drawTextCentered(QR_CAPTION_Y, String(core::qrKindCaption(e.kind)));
}

// --- Menu ------------------------------------------------------------------------
static String menuLabelFor(int i) {
    return i == 0 ? String("Back to Home") : String("");
}

void QrApp::renderMenu() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(ui::MENU_TITLE_Y, "QR Menu");

    const int lh = d.lineHeightFor(1) + 16;
    const int x  = DISPLAY_WIDTH / 2 - 220;
    int y = ui::MENU_START_Y;
    for (int i = 0; i < MENU_COUNT; i++) {
        String label = menuLabelFor(i);
        if (i == _menuSel) {
            const int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label);
        y += lh;
    }
    d.drawBookText(MARGIN_X, ui::MENU_FOOTER_Y, "Tap = move    Hold = select");
    d.flush(true);
}

void QrApp::openMenu() {
    _menuSel = 0;
    _screen  = Screen::Menu;
    renderMenu();
}

void QrApp::menuSelect() {
    // "Back to Home" (the only menu item).
    if (_ctx.manager) _ctx.manager->requestHome();
}
