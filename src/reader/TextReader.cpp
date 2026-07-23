// ===========================================================================
//  TextReader.cpp
// ===========================================================================
#include "TextReader.h"
#include "config.h"

// Map common UTF-8 "smart" punctuation to ASCII so the ASCII-only reading
// font renders cleanly; other multi-byte characters are dropped. Byte offsets
// into the file are tracked separately from this display-only cleanup, so
// pagination stays correct.
static String toAscii(const String &w) {
    String out;
    int n = w.length();
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

bool TextReader::open(const String &bookPath, long startOffset) {
    _bookPath = bookPath;
    int slash = bookPath.lastIndexOf('/');
    _bookName = bookPath.substring(slash + 1);
    if (_bookName.endsWith(".txt")) _bookName = _bookName.substring(0, _bookName.length() - 4);

    _fileSize = _storage.fileSize(bookPath);
    if (_fileSize == 0) return false;

    _pageOffsets.clear();
    _pageIndex = 0;

    uint32_t start = (startOffset < 0)
                        ? _bookmarks.getLastPosition(bookPath)
                        : (uint32_t)startOffset;
    if (start >= _fileSize) start = 0;

    // Rebuild the page-offset table from the beginning of the book up to the
    // resume point. Without this a mid-book resume starts its history at the
    // resume offset, so there is no "previous" page to go back to -- and on a
    // short book resumed near the end there is no "next" page either, leaving
    // the reader stuck on one page with both gestures dead.
    _pageOffsets.push_back(0);
    uint32_t off = 0;
    while (off < _fileSize) {
        uint32_t next = layoutPage(off, /*draw=*/false);
        if (next <= off || next >= start) break;   // resume point lies in [off, next)
        _pageOffsets.push_back(next);
        off = next;
    }
    _pageIndex  = (int)_pageOffsets.size() - 1;
    _pageStart  = _pageOffsets[_pageIndex];

    _bookmarks.setLastOpenedBook(bookPath);
    return true;
}

int TextReader::estimatedTotalPages() const {
    int lineH = max(1, _display.readerLineHeight());
    int lines = max(1, _display.usableHeight() / lineH);
    int avgW  = max(6, _display.textWidth("abcdefghijklmnopqrstuvwxyz", true) / 26);
    int charsPerLine = max(8, _display.usableWidth() / avgW);
    int perPage      = max(1, charsPerLine * lines);
    return max(1, (int)((_fileSize + perPage - 1) / perPage));
}

void TextReader::setFontSize(uint8_t size) {
    _fontSize = constrain(size, 0, 2);
    render();  // relayout current page from the same offset
}

// Lay out (optionally draw) a single page beginning at startOffset. Returns
// the byte offset (into the file) where the next page should begin. Wrapping
// is pixel-accurate against the compact reading font.
uint32_t TextReader::layoutPage(uint32_t startOffset, bool draw) {
    const int lineH    = max(1, _display.readerLineHeight());
    const int maxLines = max(1, _display.usableHeight() / lineH);
    const int maxW     = _display.usableWidth();

    // Pull a generous chunk so a full screen of text is always available.
    String raw = _storage.readChunk(_bookPath, startOffset, 6144);
    const int n = raw.length();

    int      y          = MARGIN_Y + _display.readerAscender();
    int      linesDrawn = 0;
    String   line;              // display text of the current line
    int      lineW      = 0;    // pixel width of `line`
    uint32_t drawn      = 0;    // raw bytes belonging to already-committed lines
    uint32_t pending    = 0;    // raw bytes tentatively held in `line`

    auto flushLine = [&](uint32_t upto) {
        if (draw && line.length()) _display.drawBookText(MARGIN_X, y, line);
        y += lineH;
        linesDrawn++;
        drawn = upto;
        line  = "";
        lineW = 0;
    };

    int i = 0;
    while (i < n && linesDrawn < maxLines) {
        char ch = raw[i];
        if (ch == '\n') { flushLine(i + 1); i++; continue; }  // paragraph / blank line
        if (ch == '\r') { i++; continue; }
        if (ch == ' ')  { i++; continue; }                    // collapse run-in spaces

        int wStart = i;
        while (i < n && raw[i] != ' ' && raw[i] != '\n' && raw[i] != '\r') i++;
        String word = toAscii(raw.substring(wStart, i));
        if (i < n && raw[i] == ' ') i++;                      // eat one trailing space
        uint32_t wordConsumed = i;

        if (!word.length()) { pending = wordConsumed; if (lineW == 0) drawn = wordConsumed; continue; }

        int wordW = _display.textWidth(word, true);
        // Measure the *composed* line rather than summing word + space widths:
        // textWidth() returns the tight ink box, so a standalone space (no ink)
        // measures far narrower than its real cursor advance, and word boxes drop
        // their side bearings. Summing those under-counts the line and lets the
        // tail of a sentence run off the right edge. Measuring the exact string
        // we are about to draw includes every advance, so wrapping is accurate.
        String candidate = line.length() ? line + " " + word : word;
        int    candW     = _display.textWidth(candidate, true);
        if (line.length() && candW > maxW) {
            flushLine(pending);                 // commit the line we had
            if (linesDrawn >= maxLines) break;  // page full; this word waits for next page
            line = word; lineW = wordW; pending = wordConsumed;
        } else {
            line = candidate; lineW = candW; pending = wordConsumed;
        }
    }
    if (linesDrawn < maxLines && line.length()) flushLine(pending);

    uint32_t endOffset = startOffset + drawn;
    if (endOffset > _fileSize) endOffset = _fileSize;
    return endOffset;
}

void TextReader::render() {
    _display.clearBuffer();
    _pageEnd = layoutPage(_pageStart, /*draw=*/true);

    _display.showStatusBar(_bookName, currentPageNumber(), estimatedTotalPages());
    // Full refresh every page: the EPD47 grayscale blit does not erase existing
    // ink, so a partial flush leaves the previous page showing through. A clean
    // clear-then-draw each turn is what keeps pages from overlapping.
    _display.flush(true);

    // Persist reading position so we can resume after power-off.
    _bookmarks.setLastPosition(_bookPath, _pageStart);
    _bookmarks.save();
}

void TextReader::nextPage() {
    if (_pageEnd >= _fileSize) return;      // already at the end
    _pageStart = _pageEnd;
    _pageIndex++;
    if ((int)_pageOffsets.size() == _pageIndex) _pageOffsets.push_back(_pageStart);
    render();
}

void TextReader::prevPage() {
    if (_pageIndex == 0) return;            // already at the start
    _pageIndex--;
    _pageStart = _pageOffsets[_pageIndex];
    render();
}

// Jump straight to a previously-visited page index (clamped). Kept as a general
// helper for cached-page navigation.
void TextReader::goToPageIndex(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= (int)_pageOffsets.size()) idx = (int)_pageOffsets.size() - 1;
    _pageIndex = idx;
    _pageStart = _pageOffsets[idx];
    render();
}
