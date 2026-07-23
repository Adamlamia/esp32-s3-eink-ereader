#pragma once
// ===========================================================================
//  TextReader  —  paginates a plain-text book onto the e-paper display
// ===========================================================================
//  Reads a .txt file in chunks and performs word-wrapped, page-based layout
//  sized to the current font. Pages are addressed by byte offset so we never
//  need to hold the whole book in RAM — important for large books.
// ===========================================================================
#include <Arduino.h>
#include "config.h"
#include "display/DisplayManager.h"
#include "storage/BookStorage.h"
#include "bookmark/BookmarkManager.h"
#include "core/Paginator.h"

class TextReader {
public:
    TextReader(DisplayManager &display, BookStorage &storage,
               BookmarkManager &bookmarks)
        : _display(display), _storage(storage), _bookmarks(bookmarks) {}

    // Open a book and jump to a byte offset (0 = start, -1 = last position).
    bool open(const String &bookPath, long startOffset = -1);

    void nextPage();
    void prevPage();
    void goToPageIndex(int idx);         // jump to a cached page index (clamped)
    void render();                      // (re)draw the current page

    void setFontSize(uint8_t size);     // 0..2, triggers relayout

    uint32_t currentOffset() const { return _pager.start(); }
    int      pageIndex() const { return _pager.index(); }
    int      currentPageNumber() const { return _pager.index() + 1; }
    int      estimatedTotalPages() const;
    const String &bookName() const { return _bookName; }

private:
    // Lay out one page starting at _pageStart, return offset where it ends.
    uint32_t layoutPage(uint32_t startOffset, bool draw);

    DisplayManager  &_display;
    BookStorage     &_storage;
    BookmarkManager &_bookmarks;

    String   _bookPath;
    String   _bookName;
    uint32_t _fileSize   = 0;
    uint32_t _pageEnd    = 0;           // byte offset after current page
    uint8_t  _fontSize   = DEFAULT_FONT_SIZE;
    core::Paginator _pager;             // page-offset table + navigation (seam)
};
