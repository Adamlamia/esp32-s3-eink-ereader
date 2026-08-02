#pragma once
// ===========================================================================
//  core/RefsIndex.h  —  reference-image index parsing seam (DEV·R1)
// ===========================================================================
//  Header-only, heap-free (no new/malloc; fixed buffers bounded by the REFS_*
//  macros), HAL-free pure-logic seam for the Dev Companion reference viewer,
//  in the same style as core/CalendarEvent.h + core/OpenMeteo.h.
//
//    parseRefsIndex  — tolerant refs_index.txt manifest -> RefsEntry[]
//
//  Manifest format (one reference image per line):
//
//      pinout.raw|ESP32-S3 Pinout
//      schematic.raw|Board Schematic
//
//  i.e. "<filename>.raw|<Human Readable Label>". The manifest is produced by
//  tools/make_refs.py (host side) and copied to /refs/refs_index.txt on SD.
//
//  Tolerance contract (never throws, never crashes on hostile input):
//    - blank / whitespace-only lines are skipped;
//    - a line WITHOUT '|' falls back to the filename sans extension as the
//      label ("pinout.raw" -> label "pinout");
//    - oversized file / label fields are truncated to REFS_FILE_MAX-1 /
//      REFS_LABEL_MAX-1 and always NUL-terminated (never overflowed);
//    - a line whose filename part is empty after trimming is skipped;
//    - parsing stops cleanly once `max` entries are filled (extra lines drop).
//
//  ArduinoJson is NOT needed here (the manifest is a plain line format), so the
//  seam compiles under `pio test -e native` with no dependencies beyond libc.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "config.h"   // REFS_FILE_MAX / REFS_LABEL_MAX (pure macros, no HAL)

// --- Safe fallbacks so the header compiles standalone (no config.h values) --
#ifndef REFS_FILE_MAX
  #define REFS_FILE_MAX 32
#endif
#ifndef REFS_LABEL_MAX
  #define REFS_LABEL_MAX 40
#endif
#ifndef REFS_RAW_SIZE
  #define REFS_RAW_SIZE 259200
#endif

namespace core {

// One reference-image entry: the .raw filename (relative to /refs/) + a
// human-readable label for the on-screen overlay. Plain-old-data, fixed size,
// safe to copy by value and to hold in a static array.
struct RefsEntry {
    char file[REFS_FILE_MAX];    // e.g. "pinout.raw"
    char label[REFS_LABEL_MAX];  // e.g. "ESP32-S3 Pinout"
};

// Reset an entry to safe defaults (empty strings) so partially-parsed fields
// are never garbage (mirrors calEventClear / weatherSnapshotClear).
inline void refsEntryClear(RefsEntry &e) {
    e.file[0]  = '\0';
    e.label[0] = '\0';
}

// --- Full-frame .raw size contract ------------------------------------------
// A blittable reference dump is EXACTLY one panel framebuffer:
// REFS_RAW_SIZE == EPD_WIDTH*EPD_HEIGHT/2 == 259200 bytes, the exact format
// tools/make_refs.py writes (480 bytes/row x 540 rows, no padding). Anything
// else is a corrupt / truncated / wrong-converter file and must NOT be handed
// to DisplayManager::blitRaw: blitRaw clamps a short buffer and leaves the
// (never-cleared) framebuffer tail stale, i.e. garbage on the panel instead of
// the viewer's readable placeholder. The viewer therefore rejects non-exact
// sizes (DEV·R2 short-file regression). Pure + host-testable.
inline bool refsRawSizeValid(size_t sz) { return sz == (size_t)REFS_RAW_SIZE; }

// --- Internal helpers (header-local) ----------------------------------------
namespace refs_detail {

// True for ASCII space / tab / CR / LF — the whitespace trimmed around fields.
inline bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Copy [src, src+len) into dst (capacity cap), trimming leading/trailing
// whitespace, truncating to cap-1 chars and always NUL-terminating. Returns
// the number of characters written (excluding the NUL). Never overflows.
inline size_t copyTrimmed(char *dst, size_t cap, const char *src, size_t len) {
    if (!dst || cap == 0) return 0;
    // trim leading whitespace
    size_t a = 0;
    while (a < len && isSpace(src[a])) ++a;
    // trim trailing whitespace
    size_t b = len;
    while (b > a && isSpace(src[b - 1])) --b;
    size_t n = b - a;
    if (n > cap - 1) n = cap - 1;          // truncate to fit (+ NUL)
    if (n > 0) memcpy(dst, src + a, n);
    dst[n] = '\0';
    return n;
}

// Derive a fallback label from a filename: the stem (filename sans extension).
// "pinout.raw" -> "pinout"; "board.v2.raw" -> "board.v2" (last '.' only);
// "readme" -> "readme". A leading directory ("refs/x.raw") is NOT expected here
// (the manifest stores bare filenames) but a '/' is honoured defensively.
inline void labelFromFilename(const char *file, char *label, size_t cap) {
    if (!label || cap == 0) return;
    // start after any final path separator
    const char *base = file;
    for (const char *p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    size_t len = strlen(base);
    // find the last '.' (extension separator); a leading dot (".hidden") is
    // NOT treated as an extension.
    const char *dot = nullptr;
    for (const char *p = base; *p; ++p) {
        if (*p == '.' && p != base) dot = p;
    }
    if (dot) len = (size_t)(dot - base);
    if (len > cap - 1) len = cap - 1;
    if (len > 0) memcpy(label, base, len);
    label[len] = '\0';
}

} // namespace refs_detail

// --- Manifest parser --------------------------------------------------------
// Parse a refs_index.txt document (`text`, NUL-terminated) into entries[]
// (overwritten, up to `max`). Returns the number of entries written (0..max).
// Tolerant by contract — see the header note: blank lines skipped, missing '|'
// falls back to the filename-sans-extension label, oversized fields truncated.
// A null text / null entries / max <= 0 yields 0 (never a crash).
inline int parseRefsIndex(const char *text, RefsEntry *entries, int max) {
    if (!text || !entries || max <= 0) return 0;

    int count = 0;
    const char *p = text;
    while (*p && count < max) {
        // delimit one line (LF-terminated; a trailing CR is trimmed as space)
        const char *lineStart = p;
        while (*p && *p != '\n') ++p;
        size_t lineLen = (size_t)(p - lineStart);
        if (*p == '\n') ++p;               // consume the newline

        // locate the '|' separator (first one wins)
        const char *pipe = nullptr;
        for (size_t i = 0; i < lineLen; ++i) {
            if (lineStart[i] == '|') { pipe = lineStart + i; break; }
        }

        RefsEntry e;
        refsEntryClear(e);
        if (pipe) {
            // "file|label"
            refs_detail::copyTrimmed(e.file, REFS_FILE_MAX,
                                     lineStart, (size_t)(pipe - lineStart));
            size_t labelOff = (size_t)(pipe - lineStart) + 1;
            refs_detail::copyTrimmed(e.label, REFS_LABEL_MAX,
                                     lineStart + labelOff, lineLen - labelOff);
        } else {
            // no '|': the whole line is the filename; label = stem fallback
            refs_detail::copyTrimmed(e.file, REFS_FILE_MAX, lineStart, lineLen);
            refs_detail::labelFromFilename(e.file, e.label, REFS_LABEL_MAX);
        }

        if (e.file[0] == '\0') continue;   // blank / separator-only line: skip

        // A '|' line with an empty label also falls back to the stem so the
        // on-screen overlay is never blank.
        if (e.label[0] == '\0')
            refs_detail::labelFromFilename(e.file, e.label, REFS_LABEL_MAX);

        entries[count++] = e;
    }
    return count;
}

} // namespace core
