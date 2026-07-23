#pragma once
// ===========================================================================
//  core/PageLayout.h  —  word-wrap / page-layout core (seam)
// ===========================================================================
//  The exact wrapping algorithm from TextReader::layoutPage, lifted out so it
//  can run on the host with the pixel-measurement injected as a functor. The
//  algorithm measures the *composed* line string rather than summing word and
//  space widths (see the rationale in the loop below); this is the property the
//  host tests pin down. Header-only, no Arduino/display/FS dependency.
//
//  Callbacks:
//    measure(const Str&) -> int   pixel width of a composed string
//    emit(const Str&)             called once per committed line (empty lines
//                                 included, so vertical advance matches on-device)
// ===========================================================================
#include <stdint.h>
#include "core/TextTransform.h"

namespace core {

struct LayoutResult {
    uint32_t consumed;    // raw bytes belonging to committed lines (page end delta)
    int      linesDrawn;  // number of line slots consumed (incl. blank lines)
};

template <typename Str, typename Measure, typename Emit>
LayoutResult layoutTextPage(const char *raw, int n, int maxLines, int maxW,
                            Measure measure, Emit emit) {
    int      linesDrawn = 0;
    Str      line;              // display text of the current line
    int      lineW      = 0;    // pixel width of `line`
    uint32_t drawn      = 0;    // raw bytes belonging to already-committed lines
    uint32_t pending    = 0;    // raw bytes tentatively held in `line`

    auto flushLine = [&](uint32_t upto) {
        emit(line);                     // caller decides whether/what to draw
        linesDrawn++;
        drawn = upto;
        line  = Str();
        lineW = 0;
    };

    int i = 0;
    while (i < n && linesDrawn < maxLines) {
        char ch = raw[i];
        if (ch == '\n') { flushLine((uint32_t)(i + 1)); i++; continue; }  // paragraph / blank line
        if (ch == '\r') { i++; continue; }
        if (ch == ' ')  { i++; continue; }                    // collapse run-in spaces

        int wStart = i;
        while (i < n && raw[i] != ' ' && raw[i] != '\n' && raw[i] != '\r') i++;
        Str word = toAsciiRange<Str>(raw + wStart, i - wStart);
        if (i < n && raw[i] == ' ') i++;                      // eat one trailing space
        uint32_t wordConsumed = (uint32_t)i;

        if (!word.length()) { pending = wordConsumed; if (lineW == 0) drawn = wordConsumed; continue; }

        int wordW = measure(word);
        // Measure the *composed* line rather than summing word + space widths:
        // textWidth() returns the tight ink box, so a standalone space (no ink)
        // measures far narrower than its real cursor advance, and word boxes drop
        // their side bearings. Summing those under-counts the line and lets the
        // tail of a sentence run off the right edge. Measuring the exact string
        // we are about to draw includes every advance, so wrapping is accurate.
        Str candidate = line.length() ? line + " " + word : word;
        int candW     = measure(candidate);
        if (line.length() && candW > maxW) {
            flushLine(pending);                 // commit the line we had
            if (linesDrawn >= maxLines) break;  // page full; this word waits for next page
            line = word; lineW = wordW; pending = wordConsumed;
        } else {
            line = candidate; lineW = candW; pending = wordConsumed;
        }
    }
    if (linesDrawn < maxLines && line.length()) flushLine(pending);

    LayoutResult r;
    r.consumed   = drawn;
    r.linesDrawn = linesDrawn;
    return r;
}

} // namespace core
