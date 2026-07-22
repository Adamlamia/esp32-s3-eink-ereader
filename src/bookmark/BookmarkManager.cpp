// ===========================================================================
//  BookmarkManager.cpp
// ===========================================================================
#include "BookmarkManager.h"
#include "config.h"
#include <ArduinoJson.h>

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

bool BookmarkManager::begin() { return _load(); }

bool BookmarkManager::_load() {
    File f = _fs.open(BOOKMARKS_FILE, "r");
    if (!f) { _json = "{\"lastOpened\":\"\",\"books\":{}}"; return true; }
    _json = f.readString();
    f.close();
    if (_json.length() == 0) _json = "{\"lastOpened\":\"\",\"books\":{}}";
    return true;
}

bool BookmarkManager::save() {
    File f = _fs.open(BOOKMARKS_FILE, "w");
    if (!f) return false;
    f.print(_json);
    f.close();
    return true;
}

void BookmarkManager::setLastPosition(const String &book, uint32_t offset) {
    JsonDocument doc;
    deserializeJson(doc, _json);
    doc["books"][book]["pos"] = offset;
    if (!doc["books"][book]["marks"].is<JsonArray>())
        doc["books"][book]["marks"].to<JsonArray>();
    _json.clear();
    serializeJson(doc, _json);
}

uint32_t BookmarkManager::getLastPosition(const String &book) {
    JsonDocument doc;
    deserializeJson(doc, _json);
    return doc["books"][book]["pos"] | 0u;
}

void BookmarkManager::setLastOpenedBook(const String &book) {
    JsonDocument doc;
    deserializeJson(doc, _json);
    doc["lastOpened"] = book;
    _json.clear();
    serializeJson(doc, _json);
}

String BookmarkManager::getLastOpenedBook() {
    JsonDocument doc;
    deserializeJson(doc, _json);
    return String((const char *)(doc["lastOpened"] | ""));
}

void BookmarkManager::addBookmark(const String &book, uint32_t offset,
                                  const String &label) {
    JsonDocument doc;
    deserializeJson(doc, _json);
    JsonArray marks = doc["books"][book]["marks"].is<JsonArray>()
                        ? doc["books"][book]["marks"].as<JsonArray>()
                        : doc["books"][book]["marks"].to<JsonArray>();
    JsonObject m = marks.add<JsonObject>();
    m["offset"]    = offset;
    m["label"]     = label;
    m["createdAt"] = 0;
    _json.clear();
    serializeJson(doc, _json);
}

void BookmarkManager::removeBookmark(const String &book, size_t index) {
    JsonDocument doc;
    deserializeJson(doc, _json);
    JsonArray marks = doc["books"][book]["marks"].as<JsonArray>();
    if (!marks.isNull() && index < marks.size()) marks.remove(index);
    _json.clear();
    serializeJson(doc, _json);
}

std::vector<Bookmark> BookmarkManager::listBookmarks(const String &book) {
    std::vector<Bookmark> out;
    JsonDocument doc;
    deserializeJson(doc, _json);
    JsonArray marks = doc["books"][book]["marks"].as<JsonArray>();
    if (marks.isNull()) return out;
    for (JsonObject m : marks) {
        Bookmark b;
        b.offset    = m["offset"]    | 0u;
        b.label     = String((const char *)(m["label"] | ""));
        b.createdAt = m["createdAt"] | 0u;
        out.push_back(b);
    }
    return out;
}
