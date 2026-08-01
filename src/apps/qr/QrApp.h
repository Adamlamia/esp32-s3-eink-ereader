#pragma once
// ===========================================================================
//  QrApp  —  QR Toolkit application (QR·R1)
// ===========================================================================
//  Renders a carousel of QR codes full-screen on the e-ink panel, modelled
//  on WeatherApp (single-screen app + menu):
//
//    Entries (built once in onEnter, in this order):
//      1. a WiFi QR auto-generated from WIFI_STA_SSID / WIFI_STA_PASS when
//         those secrets exist (core::qrListAddWifi — zxing WIFI: format,
//         specials escaped, T:nopass for open networks);
//      2. QR_PAYLOAD_n / QR_LABEL_n pairs from secrets.h, n = 0..QR_MAX_ENTRIES
//         -1 — every line individually optional (#ifdef-guarded), so the
//         firmware builds with ZERO QR secrets.
//
//    Payload KIND detection (drives the on-screen caption): http(s):// prefix
//    -> URL; a payload that validates as EMVCo QRPS (core::emvcoIsValid —
//    structure + CRC16-CCITT over tag 63) -> DuitNow payment; else free text.
//
//  Screen:  label (centered, large) / QR bitmap centered and scaled to fit a
//  ~420 px box (the most the 540 px panel allows under the label + caption +
//  footer chrome) / payload-type caption / "n/total" + gesture legend footer.
//  Every screen is flushed with flush(true): a redrawn QR must never ghost.
//
//  SECURITY: the raw payload is NEVER drawn on screen — a WiFi payload embeds
//  the network password, so only the label + kind caption are shown. The
//  password lives only in secrets.h -> RAM -> QR bitmap.
//
//  Gestures (decoded by AppManager, delivered as ButtonEvent):
//    Main:   Tap        = next QR entry (wraps around)
//            MediumHold = deliberate NO-OP (nothing to scroll on a single-QR
//                screen; kept in the gesture map so behaviour is explicit,
//                exactly like WeatherApp's documented no-op)
//            LongHold   = open menu
//    Menu:   Tap = move highlight    LongHold = select item
//                (matches CalendarApp / WeatherApp's menu convention exactly)
//
//  Menu items: "Back to Home" (AppManager::requestHome()).
//
//  Empty state: no WiFi creds AND no QR_PAYLOAD_n -> a helpful "Add QR
//  entries in src/secrets.h" screen. The firmware still builds and runs;
//  LongHold still opens the menu so the user can navigate home.
//
//  QR encoding: ricmoo/QRCode (Nayuki-derived, MIT) — a firmware-only
//  lib_deps entry; the native test env never sees it. NOTE: that lib does
//  NOT report "data too big" (upstream @TODO), so renderQr() guards the
//  payload length against the canonical byte-mode capacity table (ECC LOW,
//  versions QR_MIN_VERSION..QR_MAX_VERSION from config.h) BEFORE calling
//  qrcode_initText and picks the smallest fitting version. A payload that
//  fits no supported version shows a readable "payload too long" screen
//  instead of ever emitting a corrupt, unscannable QR.
//
//  No scheduled work: like WeatherApp there is no onLoop()/sleepWakeupSec()
//  override — the QR carousel is purely reactive, so button-only wakeup is
//  correct.
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"
#include "core/QrPayload.h"

class QrApp : public App {
public:
    explicit QrApp(SystemContext &ctx);

    // --- Identity ---
    const char* name() const override { return "QR Toolkit"; }
    const char* icon() const override { return "[##]"; }   // QR finder-pattern

    // --- Lifecycle ---
    void onEnter() override;
    void onExit()  override;

    // --- Input ---
    void onButton(ButtonEvent ev) override;

    // --- Optional hooks ---
    // No onLoop() / sleepWakeupSec() overrides: nothing runs in the
    // background (see header) — button-only wakeup is correct here.

private:
    // --- Screen state machine ---
    enum class Screen : uint8_t { Main = 0, Menu = 1 };

    // --- Entry construction (secrets.h -> carousel) ---
    void buildEntries();
    void addSecretEntry(int idx, const char *payload, const char *label);

    // --- Rendering ---
    void renderMain();              // current entry, or the empty state
    void renderEmptyState();
    void renderQr(const core::QrEntry &e);   // encode + scale + centre + caption
    void renderPayloadTooLong(const core::QrEntry &e);
    void renderMenu();

    // --- Menu / navigation ---
    void openMenu();
    void menuSelect();

    // --- State ---
    SystemContext     &_ctx;
    core::QrEntryList  _entries;    // WiFi entry + secrets.h entries
    Screen _screen     = Screen::Main;
    int      _index    = 0;         // carousel position, 0..count-1
    int      _menuSel  = 0;

    static const int MENU_COUNT = 1;    // "Back to Home"
};
