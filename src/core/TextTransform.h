#pragma once
// ===========================================================================
//  core/TextTransform.h  —  UTF-8 -> ASCII cleanup for the reading font (seam)
// ===========================================================================
//  Extracted verbatim from TextReader so the exact mapping can be exercised on
//  the host. Templated on the string type (Arduino `String` on-device,
//  std::string in tests) and driven from a raw byte range so no substring API
//  difference (String::substring vs std::string::substr) leaks into callers.
//  Header-only and free of Arduino/FS dependencies: safe to include from both
//  the firmware build and the native test build.
// ===========================================================================
#include <stdint.h>

namespace core {

// Map common UTF-8 "smart" punctuation to ASCII so the ASCII-only reading font
// renders cleanly; other multi-byte characters are dropped. Byte offsets into
// the file are tracked separately from this display-only cleanup, so pagination
// stays correct. Operates on [w, w+n).
template <typename Str>
Str toAsciiRange(const char *w, int n) {
    Str out;
    for (int i = 0; i < n; ) {
        uint8_t c = (uint8_t)w[i];
        if (c < 0x80) { out += (char)c; i++; continue; }
        if (c == 0xE2 && i + 2 < n) {                    // General Punctuation
            uint8_t b1 = (uint8_t)w[i + 1], b2 = (uint8_t)w[i + 2];
            if (b1 == 0x80 && (b2 == 0x98 || b2 == 0x99)) { out += '\''; i += 3; continue; }
            if (b1 == 0x80 && (b2 == 0x9C || b2 == 0x9D)) { out += '"';  i += 3; continue; }
            if (b1 == 0x80 && (b2 == 0x93 || b2 == 0x94)) { out += '-';  i += 3; continue; }
            if (b1 == 0x80 &&  b2 == 0xA6)                { out += "..."; i += 3; continue; }
        }
        if      ((c & 0xE0) == 0xC0) i += 2;             // skip the whole sequence
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else                          i += 1;
    }
    return out;
}

// Convenience overload for a whole string value.
template <typename Str>
Str toAscii(const Str &w) {
    return toAsciiRange<Str>(w.c_str(), (int)w.length());
}

} // namespace core
