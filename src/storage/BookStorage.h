#pragma once
// ===========================================================================
//  BookStorage  —  filesystem abstraction for the book library
// ===========================================================================
//  Transparently uses the microSD card when it mounts, otherwise falls back
//  to internal LittleFS. Exposes listing, reading (by byte offset, so pages
//  can be seeked cheaply), saving uploads and deleting books.
// ===========================================================================
#include <Arduino.h>
#include <FS.h>
#include <vector>

struct BookInfo {
    String   name;      // display name (file name without extension)
    String   path;      // full path on the active filesystem
    uint32_t size;      // bytes
};

class BookStorage {
public:
    bool begin();                       // mount SD, else LittleFS
    bool usingSD() const { return _usingSD; }

    std::vector<BookInfo> listBooks();
    bool     exists(const String &path);
    File     open(const String &path, const char *mode = "r");

    // Streamed upload target for the web portal.
    File     createForUpload(const String &fileName);
    bool     remove(const String &path);

    // Read a chunk of a book starting at a byte offset (for paging).
    String   readChunk(const String &path, uint32_t offset, uint32_t maxBytes);
    uint32_t fileSize(const String &path);

    // Capacity of the active filesystem, in bytes.
    uint64_t totalBytes();
    uint64_t usedBytes();
    uint64_t freeBytes();

    fs::FS  &fs();                      // active filesystem handle

private:
    bool _usingSD = false;
};
