#pragma once
// ===========================================================================
//  core/BookmarkStore.h  —  bookmark / resume JSON logic (seam)
// ===========================================================================
//  The ArduinoJson document manipulation from BookmarkManager, lifted out to
//  operate on a plain std::string buffer (the "file contents") with const char*
//  keys/labels. ArduinoJson is header-only and works identically on the host,
//  so abstracting the *storage* boundary is as simple as passing the in-memory
//  string instead of reading/writing a File. BookmarkManager keeps its Arduino
//  String API and delegates here; tests drive these functions directly with a
//  std::string standing in for the on-disk file (the "fake in-memory FS").
//
//  Determinism: addBookmark writes createdAt = 0 (the device has no RTC/NTP),
//  so there is no time or RNG dependency to freeze.
// ===========================================================================
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <ArduinoJson.h>

namespace core {

struct BookmarkRec {
    uint32_t    offset;
    std::string label;
    uint32_t    createdAt;
};

inline const char *emptyBookmarkDoc() { return "{\"lastOpened\":\"\",\"books\":{}}"; }

// Coerce an empty buffer to the default document (mirrors BookmarkManager::_load).
inline void normalize(std::string &json) {
    if (json.empty()) json = emptyBookmarkDoc();
}

inline void setLastPosition(std::string &json, const char *book, uint32_t offset) {
    JsonDocument doc;
    deserializeJson(doc, json);
    doc["books"][book]["pos"] = offset;
    if (!doc["books"][book]["marks"].is<JsonArray>())
        doc["books"][book]["marks"].to<JsonArray>();
    json.clear();
    serializeJson(doc, json);
}

inline uint32_t getLastPosition(const std::string &json, const char *book) {
    JsonDocument doc;
    deserializeJson(doc, json);
    return doc["books"][book]["pos"] | 0u;
}

inline void setLastOpenedBook(std::string &json, const char *book) {
    JsonDocument doc;
    deserializeJson(doc, json);
    doc["lastOpened"] = book;
    json.clear();
    serializeJson(doc, json);
}

inline std::string getLastOpenedBook(const std::string &json) {
    JsonDocument doc;
    deserializeJson(doc, json);
    return std::string((const char *)(doc["lastOpened"] | ""));
}

inline void addBookmark(std::string &json, const char *book, uint32_t offset,
                        const char *label) {
    JsonDocument doc;
    deserializeJson(doc, json);
    JsonArray marks = doc["books"][book]["marks"].is<JsonArray>()
                        ? doc["books"][book]["marks"].as<JsonArray>()
                        : doc["books"][book]["marks"].to<JsonArray>();
    JsonObject m = marks.add<JsonObject>();
    m["offset"]    = offset;
    m["label"]     = label;
    m["createdAt"] = 0;
    json.clear();
    serializeJson(doc, json);
}

inline void removeBookmark(std::string &json, const char *book, size_t index) {
    JsonDocument doc;
    deserializeJson(doc, json);
    JsonArray marks = doc["books"][book]["marks"].as<JsonArray>();
    if (!marks.isNull() && index < marks.size()) marks.remove(index);
    json.clear();
    serializeJson(doc, json);
}

inline std::vector<BookmarkRec> listBookmarks(const std::string &json, const char *book) {
    std::vector<BookmarkRec> out;
    JsonDocument doc;
    deserializeJson(doc, json);
    JsonArray marks = doc["books"][book]["marks"].as<JsonArray>();
    if (marks.isNull()) return out;
    for (JsonObject m : marks) {
        BookmarkRec b;
        b.offset    = m["offset"]    | 0u;
        b.label     = std::string((const char *)(m["label"] | ""));
        b.createdAt = m["createdAt"] | 0u;
        out.push_back(b);
    }
    return out;
}

} // namespace core
