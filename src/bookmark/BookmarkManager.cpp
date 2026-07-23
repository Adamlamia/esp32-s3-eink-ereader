// ===========================================================================
//  BookmarkManager.cpp
// ===========================================================================
#include "BookmarkManager.h"
#include "config.h"
#include "core/BookmarkStore.h"

// Document shape:
// {
//   "lastOpened": "/books/foo.txt",
//   "books": {
//     "/books/foo.txt": {
//       "pos": 12345,
//       "marks": [ { "offset": 100, "label": "ch.2", "createdAt": 0 } ]
//     }
//   }
// }
//
// All JSON manipulation lives in the header-only core::BookmarkStore seam so it
// can be unit-tested on the host with a std::string standing in for the file.
// This class owns only the filesystem (fs::FS) boundary and the Arduino String
// public API; behavior is unchanged.

bool BookmarkManager::begin() { return _load(); }

bool BookmarkManager::_load() {
    File f = _fs.open(BOOKMARKS_FILE, "r");
    if (!f) { _json = core::emptyBookmarkDoc(); return true; }
    _json = f.readString().c_str();
    f.close();
    core::normalize(_json);
    return true;
}

bool BookmarkManager::save() {
    File f = _fs.open(BOOKMARKS_FILE, "w");
    if (!f) return false;
    f.print(_json.c_str());
    f.close();
    return true;
}

void BookmarkManager::setLastPosition(const String &book, uint32_t offset) {
    core::setLastPosition(_json, book.c_str(), offset);
}

uint32_t BookmarkManager::getLastPosition(const String &book) {
    return core::getLastPosition(_json, book.c_str());
}

void BookmarkManager::setLastOpenedBook(const String &book) {
    core::setLastOpenedBook(_json, book.c_str());
}

String BookmarkManager::getLastOpenedBook() {
    return String(core::getLastOpenedBook(_json).c_str());
}

void BookmarkManager::addBookmark(const String &book, uint32_t offset,
                                  const String &label) {
    core::addBookmark(_json, book.c_str(), offset, label.c_str());
}

void BookmarkManager::removeBookmark(const String &book, size_t index) {
    core::removeBookmark(_json, book.c_str(), index);
}

std::vector<Bookmark> BookmarkManager::listBookmarks(const String &book) {
    std::vector<Bookmark> out;
    for (auto &r : core::listBookmarks(_json, book.c_str())) {
        Bookmark b;
        b.offset    = r.offset;
        b.label     = String(r.label.c_str());
        b.createdAt = r.createdAt;
        out.push_back(b);
    }
    return out;
}
