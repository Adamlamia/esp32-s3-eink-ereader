#pragma once
// ===========================================================================
//  BookmarkManager  —  persistent bookmarks & "continue reading" state
// ===========================================================================
//  Stores, per book, the last read byte offset plus any number of named
//  bookmarks, in a single JSON file on the active filesystem. Also records
//  the globally last-opened book so the reader can resume on boot.
// ===========================================================================
#include <Arduino.h>
#include <FS.h>
#include <string>
#include <vector>

struct Bookmark {
    uint32_t offset;    // byte offset into the book
    String   label;     // user label or auto snippet
    uint32_t createdAt; // epoch seconds (0 if RTC/NTP unavailable)
};

class BookmarkManager {
public:
    explicit BookmarkManager(fs::FS &fs) : _fs(fs) {}

    bool begin();                                   // load JSON from disk

    // "Continue reading" — auto-saved reading position per book.
    void     setLastPosition(const String &book, uint32_t offset);
    uint32_t getLastPosition(const String &book);

    // Last opened book across reboots.
    void     setLastOpenedBook(const String &book);
    String   getLastOpenedBook();

    // Named bookmarks.
    void                  addBookmark(const String &book, uint32_t offset,
                                      const String &label);
    void                  removeBookmark(const String &book, size_t index);
    std::vector<Bookmark> listBookmarks(const String &book);

    bool save();                                    // persist to disk

private:
    fs::FS &_fs;
    std::string _json;                              // in-memory document cache
    bool    _load();
};
