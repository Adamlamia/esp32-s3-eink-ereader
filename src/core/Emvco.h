#pragma once
// ===========================================================================
//  core/Emvco.h  —  EMVCo QRPS build + parse + CRC seam (QR·R1)
// ===========================================================================
//  Header-only, heap-free (no new/malloc; fixed buffers bounded by the
//  QR_EMVCO_* macros), HAL-free pure-logic seam for the QR Toolkit's
//  DuitNow capability, in the same style as core/CalendarEvent.h and
//  core/OpenMeteo.h:
//
//    emvcoCrc16       — CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflect,
//                       no final XOR) exactly as the EMVCo QRPS spec defines
//    emvcoAddField    — append one top-level TLV field to a payload struct
//    emvcoBuild       — structured fields -> QRPS string with a valid tag 63
//                       CRC appended ("...6304XXXX")
//    emvcoParse       — QRPS string -> structured fields; validates the TLV
//                       structure AND the CRC; rejects loudly on any error
//    emvcoIsValid     — parse-and-discard convenience validator
//    emvcoSubTag      — extract one sub-tag from a template-tag value
//                       (e.g. the merchant account info carried in tag 26)
//
//  Modelling choice (lossless round-trip): fields are kept at TOP-LEVEL TLV
//  granularity — each field's value is an opaque string. Template tags such
//  as 26 (Merchant Account Information) and 62 (Additional Data) carry their
//  own nested sub-TLVs inside that value verbatim, so parse(build(x)) == x
//  byte-for-byte and no information is ever lost. emvcoSubTag() decodes one
//  level of nesting on demand (the DuitNow "decode" capability) without the
//  model needing a recursive tree (and therefore no heap).
//
//  EMVCo constraints honoured: tags are exactly two ASCII digits; lengths are
//  exactly two ASCII digits (so a value may be at most 99 chars — longer
//  values are REJECTED by emvcoAddField/emvcoBuild, never truncated); tag 63
//  is the CRC trailer, must be the last field, length 04, uppercase hex.
//
//  config.h is a pure macro header (no Arduino/HAL), so this seam compiles
//  under `pio test -e native`; the fallback #ifndef guards below also let the
//  header compile fully standalone.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>

#if __has_include("config.h")
  #include "config.h"   // QR_EMVCO_* sizing constants (pure macros, no HAL)
#endif

// --- Safe fallbacks so the header compiles standalone (no config.h) ---------
#ifndef QR_EMVCO_MAX_FIELDS
  #define QR_EMVCO_MAX_FIELDS 16     // max top-level TLV fields held in memory
#endif
#ifndef QR_EMVCO_VALUE_MAX
  #define QR_EMVCO_VALUE_MAX 100     // field value buffer: 99 chars + NUL (the
#endif                               // EMVCo 2-digit length ceiling)
#ifndef QR_EMVCO_PAYLOAD_MAX
  #define QR_EMVCO_PAYLOAD_MAX 384   // recommended build-output buffer (a full
#endif                               // DuitNow payload is typically < 300 chars)

namespace core {

// --- CRC16-CCITT (EMVCo QRPS) ------------------------------------------------
// Polynomial 0x1021, initial value 0xFFFF, no input/output reflection, no
// final XOR — the "CRC-16/CCITT-FALSE" variant (check value 0x29B1 for the
// ASCII string "123456789"). The EMVCo spec computes it over every character
// of the payload up to and INCLUDING the "6304" header of the CRC field.
inline uint16_t emvcoCrc16(const char *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = (uint16_t)(crc ^ ((uint16_t)((uint8_t)data[i]) << 8));
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// --- Data types ----------------------------------------------------------------
// One top-level TLV field. `tag` is two ASCII digits + NUL; `value` is the
// opaque field body (nested sub-TLVs for template tags stay verbatim inside).
struct EmvcoField {
    char tag[3];                        // "00".."99", NUL-terminated
    char value[QR_EMVCO_VALUE_MAX];     // NUL-terminated, <= 99 chars
};

// A decoded (or to-be-built) EMVCo QRPS payload: an ORDERED list of fields.
// Order is preserved so a parse -> build round-trip is byte-identical.
struct EmvcoPayload {
    EmvcoField fields[QR_EMVCO_MAX_FIELDS];
    int count;                          // fields in use, 0..QR_EMVCO_MAX_FIELDS
};

// Reset a payload to empty so partially-parsed structs are never garbage
// (mirrors calEventClear / weatherSnapshotClear).
inline void emvcoPayloadClear(EmvcoPayload &p) {
    p.count = 0;
    p.fields[0].tag[0]   = '\0';
    p.fields[0].value[0] = '\0';
}

// --- Helpers ---------------------------------------------------------------------
inline bool emvcoIsTwoDigits(const char *s) {
    return s && s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9';
}

// Append one field to the payload. REJECTS (returns false, payload unchanged)
// when: the list is full, the tag is not two ASCII digits, the value is null,
// or the value is longer than 99 chars (unrepresentable in an EMVCo 2-digit
// length). Never truncates — a payment payload must never be silently mangled.
inline bool emvcoAddField(EmvcoPayload &p, const char *tag, const char *value) {
    if (p.count < 0 || p.count >= QR_EMVCO_MAX_FIELDS) return false;
    if (!emvcoIsTwoDigits(tag) || tag[2] != '\0') return false;  // EXACTLY 2 digits
    if (!value) return false;
    const size_t len = std::strlen(value);
    if (len > 99) return false;                 // EMVCo length field ceiling
    EmvcoField &f = p.fields[p.count];
    f.tag[0] = tag[0];
    f.tag[1] = tag[1];
    f.tag[2] = '\0';
    std::memcpy(f.value, value, len);
    f.value[len] = '\0';
    p.count++;
    return true;
}

// --- Build -----------------------------------------------------------------------
// Serialize the fields as "TTLLVV..." and append the CRC trailer "6304XXXX"
// (XXXX = uppercase hex emvcoCrc16 over everything including the "6304").
// Returns false (out = "") when: out/cap is null, the buffer is too small, or
// any field is invalid (bad tag, value > 99 chars). Fails loudly, never
// produces a partial payload.
inline bool emvcoBuild(const EmvcoPayload &p, char *out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (p.count < 0 || p.count > QR_EMVCO_MAX_FIELDS) return false;

    size_t pos = 0;
    for (int i = 0; i < p.count; ++i) {
        const EmvcoField &f = p.fields[i];
        if (!emvcoIsTwoDigits(f.tag)) { out[0] = '\0'; return false; }
        const size_t vlen = std::strlen(f.value);
        if (vlen > 99) { out[0] = '\0'; return false; }   // unrepresentable length
        // "TTLL" + value
        if (pos + 4 + vlen >= cap) { out[0] = '\0'; return false; }  // too small
        out[pos++] = f.tag[0];
        out[pos++] = f.tag[1];
        out[pos++] = (char)('0' + (vlen / 10));
        out[pos++] = (char)('0' + (vlen % 10));
        std::memcpy(out + pos, f.value, vlen);
        pos += vlen;
    }

    // CRC trailer: "6304" is part of the CRC input per the EMVCo spec.
    if (pos + 8 + 1 > cap) { out[0] = '\0'; return false; }  // "6304"+4 hex+NUL
    out[pos++] = '6';
    out[pos++] = '3';
    out[pos++] = '0';
    out[pos++] = '4';
    const uint16_t crc = emvcoCrc16(out, pos);
    std::snprintf(out + pos, cap - pos, "%04X", (unsigned)crc);
    pos += 4;
    out[pos] = '\0';
    return true;
}

// --- Parse + validate ---------------------------------------------------------------
// Decode a QRPS string into ordered top-level fields, VALIDATING as it goes:
//   * every field header is two ASCII digits (tag) + two ASCII digits (length)
//   * every declared length is fully present (no truncated tail)
//   * tag 63 exists, is the LAST field, has length 04 and valid hex digits
//   * the recomputed CRC over [0, len-4) matches the trailer (case-insensitive)
// Any violation returns false and leaves `out` EMPTY (cleared) — a corrupt
// payment payload must fail loudly, never half-decode. On success `out` holds
// every field EXCEPT the tag-63 trailer (the CRC is validated, not stored).
inline bool emvcoParse(const char *str, EmvcoPayload &out) {
    emvcoPayloadClear(out);

    bool ok = false;
    bool sawCrc = false;
    do {
        if (!str) break;
        const size_t len = std::strlen(str);
        if (len < 14) break;                    // smallest sane payload

        bool structOk = true;
        size_t pos = 0;
        while (pos < len) {
            if (pos + 4 > len) { structOk = false; break; }   // truncated header
            const char t0 = str[pos], t1 = str[pos + 1];
            const char l0 = str[pos + 2], l1 = str[pos + 3];
            if (t0 < '0' || t0 > '9' || t1 < '0' || t1 > '9') { structOk = false; break; }
            if (l0 < '0' || l0 > '9' || l1 < '0' || l1 > '9') { structOk = false; break; }
            const size_t vlen = (size_t)(l0 - '0') * 10 + (size_t)(l1 - '0');
            if (pos + 4 + vlen > len) { structOk = false; break; }  // truncated value

            if (t0 == '6' && t1 == '3') {
                // CRC trailer: must be the final field, length 04.
                if (sawCrc || vlen != 4 || pos + 4 + 4 != len) { structOk = false; break; }
                const uint16_t want = emvcoCrc16(str, pos + 4);
                unsigned got = 0;
                bool hexOk = true;
                for (int i = 0; i < 4; ++i) {
                    const char c = str[pos + 4 + i];
                    unsigned d;
                    if (c >= '0' && c <= '9')      d = (unsigned)(c - '0');
                    else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
                    else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
                    else { hexOk = false; break; }
                    got = (got << 4) | d;
                }
                if (!hexOk || (uint16_t)got != want) { structOk = false; break; }
                sawCrc = true;
                pos += 4 + 4;
                continue;
            }

            if (sawCrc) { structOk = false; break; }        // field after tag 63
            if (out.count >= QR_EMVCO_MAX_FIELDS) { structOk = false; break; }
            EmvcoField &f = out.fields[out.count++];
            f.tag[0] = t0;
            f.tag[1] = t1;
            f.tag[2] = '\0';
            std::memcpy(f.value, str + pos + 4, vlen);
            f.value[vlen] = '\0';
            pos += 4 + vlen;
        }

        ok = structOk && sawCrc;    // a payload without tag 63 is invalid
    } while (false);

    if (!ok) emvcoPayloadClear(out);    // never leak a half-decoded payload
    return ok;
}

// Validate-only convenience: true iff the string is structurally sound AND
// its CRC verifies.
inline bool emvcoIsValid(const char *str) {
    EmvcoPayload scratch;
    return emvcoParse(str, scratch);
}

// --- Template sub-tag extraction (one nesting level) --------------------------------
// Look up sub-tag `subTag` (two ASCII digits) inside a template-tag VALUE
// (e.g. the tag-26 body "0014MY.GOV.BNM.RPP011160123456789...") and copy its
// value into out (NUL-terminated). Returns false when the sub-tag is absent,
// the template value is malformed, or cap is too small (never truncates a
// payment identifier). This is the DuitNow "decode to structured fields"
// capability: tag 26 sub-tag 00 = GUI, 01 = merchant/proxy ID, etc.
inline bool emvcoSubTag(const char *templateValue, const char *subTag,
                        char *out, size_t cap) {
    if (!templateValue || !emvcoIsTwoDigits(subTag) || !out || cap == 0)
        return false;
    const size_t len = std::strlen(templateValue);
    size_t pos = 0;
    while (pos + 4 <= len) {
        const char t0 = templateValue[pos], t1 = templateValue[pos + 1];
        const char l0 = templateValue[pos + 2], l1 = templateValue[pos + 3];
        if (t0 < '0' || t0 > '9' || t1 < '0' || t1 > '9') return false;
        if (l0 < '0' || l0 > '9' || l1 < '0' || l1 > '9') return false;
        const size_t vlen = (size_t)(l0 - '0') * 10 + (size_t)(l1 - '0');
        if (pos + 4 + vlen > len) return false; // malformed tail
        if (t0 == subTag[0] && t1 == subTag[1]) {
            if (vlen + 1 > cap) return false;   // refuse to truncate
            std::memcpy(out, templateValue + pos + 4, vlen);
            out[vlen] = '\0';
            return true;
        }
        pos += 4 + vlen;
    }
    // Reaching here means the sub-tag was not present (a malformed trailing
    // partial field exits early with false via the length check above).
    return false;
}

} // namespace core
