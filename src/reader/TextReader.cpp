// ===========================================================================
//  TextReader.cpp
// ===========================================================================
#include "TextReader.h"
#include "config.h"

// Rough average glyph width (px) per font size — used only for the page
// estimate; real wrapping below measures word by word.
static int avgGlyphWidth(uint8_t fontSize) {
    switch (fontSize) { case 0: return 14; case 2: return 24; default: return 18; }
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

    _pageStart = start;
    _pageOffsets.push_back(_pageStart);

    _bookmarks.setLastOpenedBook(bookPath);
    return true;
}

int TextReader::estimatedTotalPages() const {
    int charsPerLine = _display.usableWidth() / avgGlyphWidth(_fontSize);
    int lines        = _display.usableHeight() / _display.lineHeightFor(_fontSize);
    int perPage      = max(1, charsPerLine * lines);
    return max(1, (int)((_fileSize + perPage - 1) / perPage));
}

void TextReader::setFontSize(uint8_t size) {
    _fontSize = constrain(size, 0, 2);
    render();  // relayout current page from the same offset
}

// Lay out (optionally draw) a single page beginning at startOffset. Returns
// the byte offset where the next page should begin.
uint32_t TextReader::layoutPage(uint32_t startOffset, bool draw) {
    const int lineH   = _display.lineHeightFor(_fontSize);
    const int maxLines = _display.usableHeight() / lineH;
    const int maxW    = _display.usableWidth();
    const int glyphW  = avgGlyphWidth(_fontSize);
    const int maxCharsPerLine = max(4, maxW / glyphW);

    // Pull a generous chunk so a full screen of text is available.
    uint32_t chunkBytes = (uint32_t)maxLines * maxCharsPerLine * 2 + 256;
    String text = _storage.readChunk(_bookPath, startOffset, chunkBytes);

    int   x = MARGIN_X, y = MARGIN_Y + lineH;
    int   linesDrawn = 0;
    uint32_t consumed = 0;      // bytes placed on this page
    String   line;

    auto flushLine = [&]() {
        if (draw && line.length()) _display.drawText(x, y, line, _fontSize);
        y += lineH;
        linesDrawn++;
        line = "";
    };

    // Word-wrap while honouring explicit newlines in the source text.
    int i = 0;
    while (i < (int)text.length() && linesDrawn < maxLines) {
        // Handle newline.
        if (text[i] == '\n') { consumed = i + 1; flushLine(); i++; continue; }
        if (text[i] == '\r') { i++; consumed = i; continue; }

        // Read next word.
        int wStart = i;
        while (i < (int)text.length() && text[i] != ' ' && text[i] != '\n' && text[i] != '\r') i++;
        String word = text.substring(wStart, i);
        if (i < (int)text.length() && text[i] == ' ') i++;   // eat single space

        String trial = line.length() ? line + " " + word : word;
        if ((int)trial.length() > maxCharsPerLine && line.length()) {
            flushLine();
            line = word;
        } else {
            line = trial;
        }
        consumed = i;
    }
    if (linesDrawn < maxLines) flushLine();   // final partial line

    uint32_t endOffset = startOffset + consumed;
    if (endOffset > _fileSize) endOffset = _fileSize;
    return endOffset;
}

void TextReader::render() {
    _display.clearBuffer();
    _pageEnd = layoutPage(_pageStart, /*draw=*/true);

    int battery = 100;   // wired up by main.cpp via a setter in a fuller build
    _display.showStatusBar(_bookName, currentPageNumber(),
                           estimatedTotalPages(), battery);
    _display.flush();

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
