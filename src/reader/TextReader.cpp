// ===========================================================================
//  TextReader.cpp
// ===========================================================================
#include "TextReader.h"
#include "config.h"
#include "core/TextTransform.h"
#include "core/PageLayout.h"

// Word-wrap, UTF-8->ASCII cleanup and the page-offset table now live in
// header-only seams under src/core/ so they can be unit-tested on the host
// (see test/). This file wires the display / storage / bookmark hardware
// boundaries to that pure logic; runtime behavior is unchanged.

bool TextReader::open(const String &bookPath, long startOffset) {
    _bookPath = bookPath;
    int slash = bookPath.lastIndexOf('/');
    _bookName = bookPath.substring(slash + 1);
    if (_bookName.endsWith(".txt")) _bookName = _bookName.substring(0, _bookName.length() - 4);

    _fileSize = _storage.fileSize(bookPath);
    if (_fileSize == 0) return false;

    uint32_t start = (startOffset < 0)
                        ? _bookmarks.getLastPosition(bookPath)
                        : (uint32_t)startOffset;

    // Rebuild the page-offset table from the beginning of the book up to the
    // resume point so "previous" (and, on a short book, "next") pages exist --
    // otherwise a mid-book resume leaves the reader stuck on one page. The pixel
    // layout is injected; the bookkeeping lives in core::Paginator.
    _pager.openAt(start, _fileSize,
                  [this](uint32_t off) { return layoutPage(off, /*draw=*/false); });

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
// is pixel-accurate against the compact reading font. The wrapping algorithm
// itself lives in core::layoutTextPage (seam); here we only bind the display
// measurement and drawing, and clamp the end offset to the file size.
uint32_t TextReader::layoutPage(uint32_t startOffset, bool draw) {
    const int lineH    = max(1, _display.readerLineHeight());
    const int maxLines = max(1, _display.usableHeight() / lineH);
    const int maxW     = _display.usableWidth();

    // Pull a generous chunk so a full screen of text is always available.
    String raw = _storage.readChunk(_bookPath, startOffset, 6144);

    int y = MARGIN_Y + _display.readerAscender();
    auto measure = [this](const String &s) { return _display.textWidth(s, true); };
    auto emit    = [&](const String &line) {
        if (draw && line.length()) _display.drawBookText(MARGIN_X, y, line);
        y += lineH;                          // advance for every line, blank included
    };
    core::LayoutResult r = core::layoutTextPage<String>(
        raw.c_str(), (int)raw.length(), maxLines, maxW, measure, emit);

    uint32_t endOffset = startOffset + r.consumed;
    if (endOffset > _fileSize) endOffset = _fileSize;   // clamp; no deferred word double-count
    return endOffset;
}

void TextReader::render() {
    _display.clearBuffer();
    _pageEnd = layoutPage(_pager.start(), /*draw=*/true);

    _display.showStatusBar(_bookName, currentPageNumber(), estimatedTotalPages());
    // Full refresh every page: the EPD47 grayscale blit does not erase existing
    // ink, so a partial flush leaves the previous page showing through. A clean
    // clear-then-draw each turn is what keeps pages from overlapping.
    _display.flush(true);

    // Persist reading position so we can resume after power-off.
    _bookmarks.setLastPosition(_bookPath, _pager.start());
    _bookmarks.save();
}

void TextReader::nextPage() {
    if (_pager.nextPage(_pageEnd)) render();     // no-op at EOF
}

void TextReader::prevPage() {
    if (_pager.prevPage()) render();             // no-op at start
}

// Jump straight to a previously-visited page index (clamped). Kept as a general
// helper for cached-page navigation.
void TextReader::goToPageIndex(int idx) {
    _pager.goToPageIndex(idx);
    render();
}
