#pragma once
// ===========================================================================
//  WebPortal  —  the "local website" for uploading & managing books
// ===========================================================================
//  Brings up Wi-Fi (SoftAP + optional STA), mDNS (http://ereader.local) and
//  an async HTTP server that serves the UI from LittleFS and exposes a small
//  JSON API: list / upload / delete books, read/set bookmarks and settings.
// ===========================================================================
#include <Arduino.h>
#include "storage/BookStorage.h"
#include "bookmark/BookmarkManager.h"

class AsyncWebServer;

class WebPortal {
public:
    WebPortal(BookStorage &storage, BookmarkManager &bookmarks)
        : _storage(storage), _bookmarks(bookmarks) {}

    bool begin();                       // start Wi-Fi + HTTP server
    void stop();                        // tear down to save power
    bool isRunning() const { return _running; }

    String ipAddress() const;           // AP or STA address as text

private:
    void registerRoutes();

    BookStorage     &_storage;
    BookmarkManager &_bookmarks;
    AsyncWebServer  *_server = nullptr;
    bool             _running = false;
};
