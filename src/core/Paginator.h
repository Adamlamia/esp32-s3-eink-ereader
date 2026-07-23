#pragma once
// ===========================================================================
//  core/Paginator.h  —  byte-offset page table & navigation (seam)
// ===========================================================================
//  The page-offset bookkeeping from TextReader (the _pageOffsets table plus the
//  next/prev/open-rebuild index<->offset math), lifted out so it can be tested
//  without a display. The pixel layout that decides where a page ends is
//  injected as a functor, so this class holds only the pure navigation logic.
//  Behavior mirrors TextReader exactly. Header-only, no Arduino dependency.
// ===========================================================================
#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace core {

class Paginator {
public:
    // Rebuild the page-offset table from the beginning of the book up to the
    // resume point, reproducing sequential page boundaries. Without this a
    // mid-book resume starts its history at the resume offset, so there is no
    // "previous" page to go back to -- and on a short book resumed near the end
    // there is no "next" page either, leaving the reader stuck on one page.
    // `endOfPage(off)` returns the byte offset where the page starting at `off`
    // ends (i.e. TextReader::layoutPage(off, false)).
    template <typename EndOfPage>
    void openAt(uint32_t resume, uint32_t fileSize, EndOfPage endOfPage) {
        _fileSize = fileSize;
        _offsets.clear();
        _index = 0;
        if (resume >= _fileSize) resume = 0;

        _offsets.push_back(0);
        uint32_t off = 0;
        while (off < _fileSize) {
            uint32_t next = endOfPage(off);
            if (next <= off || next >= resume) break;   // resume point lies in [off, next)
            _offsets.push_back(next);
            off = next;
        }
        _index = (int)_offsets.size() - 1;
        _start = _offsets[_index];
    }

    // Advance one page. `pageEnd` is where the current page ended (the value
    // TextReader stores as _pageEnd from the last layout). No-op at EOF.
    bool nextPage(uint32_t pageEnd) {
        if (pageEnd >= _fileSize) return false;          // already at the end
        _start = pageEnd;
        _index++;
        if ((int)_offsets.size() == _index) _offsets.push_back(_start);
        return true;
    }

    bool prevPage() {
        if (_index == 0) return false;                   // already at the start
        _index--;
        _start = _offsets[_index];
        return true;
    }

    // Jump to a previously-visited page index (clamped).
    void goToPageIndex(int idx) {
        if (idx < 0) idx = 0;
        if (idx >= (int)_offsets.size()) idx = (int)_offsets.size() - 1;
        _index = idx;
        _start = _offsets[idx];
    }

    uint32_t start()     const { return _start; }
    int      index()     const { return _index; }
    size_t   pageCount() const { return _offsets.size(); }
    uint32_t offsetAt(int i) const { return _offsets[i]; }

private:
    std::vector<uint32_t> _offsets;   // cache of visited page boundaries
    int      _index    = 0;
    uint32_t _start    = 0;
    uint32_t _fileSize = 0;
};

} // namespace core
