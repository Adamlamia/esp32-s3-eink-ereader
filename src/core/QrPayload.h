#pragma once
// ===========================================================================
//  core/QrPayload.h  —  QR entry model + WiFi QR builder seam (QR·R1)
// ===========================================================================
//  Header-only, heap-free (no new/malloc; fixed buffers bounded by the QR_*
//  macros), HAL-free pure-logic seam for the QR Toolkit, in the same style
//  as core/CalendarEvent.h and core/OpenMeteo.h:
//
//    QrEntry / QrEntryList — fixed-capacity model of the app's QR carousel
//                            (label + payload + kind), sized by config.h
//    qrListAdd             — append an entry (truncates label/payload to fit
//                            their fixed buffers; fails only when the list is
//                            full)
//    wifiQrEscape          — zxing WIFI: escaping of \ ; , : " (both SSID and
//                            password)
//    wifiQrBuild           — "WIFI:T:WPA;S:<ssid>;P:<pass>;;" builder, or
//                            "WIFI:T:nopass;S:<ssid>;;" for an open network
//    qrListAddWifi         — convenience: build the WiFi payload + append the
//                            entry in one call
//    qrKindCaption         — payload-type caption shown under each QR
//
//  The QR ENCODING (payload string -> bitmap) is deliberately NOT here: it
//  lives in the firmware-only QrApp (ricmoo/QRCode), so this seam — and its
//  native Unity tests — stay free of any QR-library or HAL dependency and
//  test the string logic only.
//
//  config.h is a pure macro header (no Arduino/HAL), so this seam compiles
//  under `pio test -e native`; the fallback #ifndef guards below also let the
//  header compile fully standalone.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>

#if __has_include("config.h")
  #include "config.h"   // QR_* sizing constants (pure macros, no HAL)
#endif

// --- Safe fallbacks so the header compiles standalone (no config.h) ---------
#ifndef QR_LABEL_MAX
  #define QR_LABEL_MAX 32        // entry label buffer (chars, incl. NUL)
#endif
#ifndef QR_PAYLOAD_MAX
  #define QR_PAYLOAD_MAX 320     // entry payload buffer (chars, incl. NUL);
#endif                           // fits a QR v13 byte-mode code (425 cap)
#ifndef QR_MAX_ENTRIES
  #define QR_MAX_ENTRIES 8       // max QR entries in the carousel
#endif
#ifndef QR_WIFI_QR_MAX
  #define QR_WIFI_QR_MAX 192     // WiFi QR string buffer: "WIFI:T:WPA;S:" +
#endif                           // 2x escaped SSID + ";P:" + 2x escaped pass

namespace core {

// --- Data types ----------------------------------------------------------------
// What a payload IS (drives the on-screen caption; the payload string itself
// is what gets encoded into the QR bitmap).
enum class QrKind : uint8_t {
    Text    = 0,    // free text
    Url     = 1,    // http(s):// link
    Wifi    = 2,    // WIFI:T:...;S:...;P:...;;
    Payment = 3,    // EMVCo QRPS (DuitNow)
};

// One QR carousel entry. Plain-old-data, fixed size, safe to hold in a static
// array. label/payload are always NUL-terminated (truncated to fit, never
// overflowed — see qrCopyTrunc).
struct QrEntry {
    char   label[QR_LABEL_MAX];
    char   payload[QR_PAYLOAD_MAX];
    QrKind kind;
};

// The whole carousel. `count` is the number of entries in use; the app cycles
// items[0 .. count-1] on tap.
struct QrEntryList {
    QrEntry items[QR_MAX_ENTRIES];
    int count;                          // 0..QR_MAX_ENTRIES (0 = empty state)
};

// Reset the list to the empty state (mirrors calEventClear's convention).
inline void qrListClear(QrEntryList &l) {
    l.count = 0;
    l.items[0].label[0]   = '\0';
    l.items[0].payload[0] = '\0';
    l.items[0].kind       = QrKind::Text;
}

// True iff the carousel has no entries at all (drives the app's "add entries
// in src/secrets.h" empty-state screen).
inline bool qrListIsEmpty(const QrEntryList &l) { return l.count <= 0; }

// --- Helpers ---------------------------------------------------------------------
// Bounded string copy: truncates to cap-1 chars and ALWAYS NUL-terminates.
// A null source copies an empty string. Never overflows the destination.
inline void qrCopyTrunc(const char *src, char *dst, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < cap; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// Append an entry. label/payload are truncated to their fixed buffers (UI
// cosmetics only — a long LABEL never matters for correctness). Returns
// false ONLY when the list is full (count == QR_MAX_ENTRIES); the entry is
// not added in that case. kind is stored verbatim.
inline bool qrListAdd(QrEntryList &l, QrKind kind, const char *label,
                      const char *payload) {
    if (l.count < 0 || l.count >= QR_MAX_ENTRIES) return false;
    QrEntry &e = l.items[l.count];
    qrCopyTrunc(label,   e.label,   QR_LABEL_MAX);
    qrCopyTrunc(payload, e.payload, QR_PAYLOAD_MAX);
    e.kind = kind;
    l.count++;
    return true;
}

// Short payload-type caption rendered under each QR bitmap.
inline const char* qrKindCaption(QrKind k) {
    switch (k) {
        case QrKind::Url:     return "URL";
        case QrKind::Wifi:    return "Wi-Fi network";
        case QrKind::Payment: return "DuitNow payment";
        case QrKind::Text:
        default:              return "Text";
    }
}

// --- WiFi QR payload (zxing "WIFI:" convention) ------------------------------------
// Escape one WiFi field (SSID or password) per the zxing WIFI: spec: the five
// characters  \  ;  ,  :  "  are each prefixed with a backslash. Writes into
// out (NUL-terminated); returns false (out = "") if cap is too small — it
// FAILS LOUDLY rather than truncating a credential mid-string (a half-escaped
// password would encode a QR that silently joins the wrong network).
inline bool wifiQrEscape(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!in) return true;                       // null == empty field
    size_t pos = 0;
    for (size_t i = 0; in[i] != '\0'; ++i) {
        const char c = in[i];
        const bool special = (c == '\\' || c == ';' || c == ',' ||
                              c == ':'  || c == '"');
        const size_t need = special ? 2 : 1;
        if (pos + need + 1 > cap) { out[0] = '\0'; return false; }  // refuse, don't trim
        if (special) out[pos++] = '\\';
        out[pos++] = c;
    }
    out[pos] = '\0';
    return true;
}

// Build the full WiFi QR payload string:
//   with password   ->  WIFI:T:WPA;S:<escaped ssid>;P:<escaped pass>;;
//   open network    ->  WIFI:T:nopass;S:<escaped ssid>;;   (no P: field, per
//                       the zxing convention, when pass is null or "")
// Returns false (out = "") when ssid is null/empty or the buffer is too small
// for the fully-escaped result. WPA2/WPA3 networks scan fine as T:WPA.
inline bool wifiQrBuild(const char *ssid, const char *pass,
                        char *out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!ssid || ssid[0] == '\0') return false; // a WiFi QR with no SSID is useless

    char escSsid[QR_WIFI_QR_MAX];
    char escPass[QR_WIFI_QR_MAX];
    if (!wifiQrEscape(ssid, escSsid, sizeof(escSsid))) return false;
    const bool open = (!pass || pass[0] == '\0');
    if (!open && !wifiQrEscape(pass, escPass, sizeof(escPass))) return false;

    // Assemble into a local scratch first so a too-small caller buffer yields
    // an empty out ("fail loudly, never partial"), never a truncated payload.
    char tmp[2 * QR_WIFI_QR_MAX + 24];
    size_t pos = 0;
    const char *prefix = open ? "WIFI:T:nopass;S:" : "WIFI:T:WPA;S:";
    for (size_t i = 0; prefix[i] != '\0'; ++i) tmp[pos++] = prefix[i];
    for (size_t i = 0; escSsid[i] != '\0'; ++i) tmp[pos++] = escSsid[i];
    if (!open) {
        const char *mid = ";P:";
        for (size_t i = 0; mid[i] != '\0'; ++i) tmp[pos++] = mid[i];
        for (size_t i = 0; escPass[i] != '\0'; ++i) tmp[pos++] = escPass[i];
    }
    tmp[pos++] = ';';
    tmp[pos++] = ';';
    tmp[pos] = '\0';

    if (pos + 1 > cap) return false;            // caller buffer too small
    std::memcpy(out, tmp, pos + 1);
    return true;
}

// Convenience used by QrApp: build the WiFi QR payload from raw credentials
// and append it as a QrKind::Wifi entry labelled `label` (e.g. "Wi-Fi").
// Returns false when the list is full, the creds are empty, or the payload
// buffer could not hold the escaped string (all fail loudly).
inline bool qrListAddWifi(QrEntryList &l, const char *ssid, const char *pass,
                          const char *label) {
    char payload[QR_WIFI_QR_MAX];
    if (!wifiQrBuild(ssid, pass, payload, sizeof(payload))) return false;
    // A WiFi payload always fits QR_PAYLOAD_MAX (192 < 320), so a false here
    // can only mean "list full" — exactly what the caller needs to know.
    return qrListAdd(l, QrKind::Wifi, label, payload);
}

} // namespace core
