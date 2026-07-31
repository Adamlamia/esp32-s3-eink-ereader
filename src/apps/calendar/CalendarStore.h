#pragma once
// ===========================================================================
//  CalendarStore  —  calendar cache persistence (Round 2)
// ===========================================================================
//  Persists the synced calendar cache (a lastSyncUtc timestamp plus the
//  materialised event list) as a small JSON document at a fixed path on the
//  active filesystem (SD or LittleFS, via BookStorage::fs()).
//
//  Cache format (ArduinoJson 7, compact keys to save flash + RAM):
//    {
//      "v":    1,                 // schema version (bump if fields change)
//      "sync": 1785715200,        // lastSyncUtc, UTC epoch seconds
//      "tz":   28800,             // CAL_TZ_OFFSET_SEC used when materialising
//      "ev": [                    // concrete events (recurrence already expanded)
//        { "s": start, "e": end, "t": "title", "c": category, "a": allDay,
//          "f": freq, "i": interval, "n": count, "u": untilUtc, "b": bydayMask }
//      ]
//    }
//  "u" (RRULE UNTIL) is omitted when it equals the INT64_MIN "none" sentinel.
//
//  Robustness contract: load() MUST tolerate a missing, truncated, oversized
//  or corrupt file — it returns 0 events (and lastSyncUtc = 0) rather than
//  crashing. A power loss mid-save can corrupt the file; the next load simply
//  treats it as empty and the next "Sync now" rewrites it. (Atomic rename
//  would be nicer but fs::FS on SD/LittleFS here has no portable rename.)
//
//  Structure (matches the BookmarkStore.h seam pattern): the pure serialize /
//  deserialize logic lives in namespace core and operates on a std::string
//  standing in for the file contents, so it is host-testable under `pio test
//  -e native` (ArduinoJson is header-only and identical on the host). The
//  thin fs::FS wrapper class is compiled only for the firmware (ARDUINO).
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <ArduinoJson.h>
#include "core/CalendarEvent.h"   // pulls in config.h (CAL_* sizing constants)

#ifndef CAL_CACHE_FILE
  #define CAL_CACHE_FILE "/calendar.json"   // fixed path on the active FS
#endif

namespace core {

// Largest cache file we bother reading; a full cache (CAL_MAX_EVENTS events,
// 64-char titles) is ~12 KB, so anything larger is treated as corrupt.
static constexpr size_t CAL_CACHE_MAX_BYTES = 65536;

// --- Pure serialization seam (host-testable) -------------------------------
// Serialize `n` events + lastSyncUtc into `out` (overwritten). Inputs are
// bounded: a null event pointer or negative count yields an empty event list;
// n is clamped to CAL_MAX_EVENTS. Never throws, never uses unbounded memory.
inline void serializeCalendarCache(std::string &out, const CalendarEvent *ev,
                                   int n, int64_t lastSyncUtc) {
    if (!ev || n < 0) n = 0;
    if (n > CAL_MAX_EVENTS) n = CAL_MAX_EVENTS;

    JsonDocument doc;
    doc["v"]    = 1;
    doc["sync"] = lastSyncUtc;
    doc["tz"]   = (int32_t)CAL_TZ_OFFSET_SEC;
    JsonArray arr = doc["ev"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        const CalendarEvent &e = ev[i];
        JsonObject o = arr.add<JsonObject>();
        o["s"] = e.startUtc;
        o["e"] = e.endUtc;
        o["t"] = e.title;
        o["c"] = e.category;
        o["a"] = e.allDay;
        o["f"] = (int)e.freq;
        o["i"] = e.interval;
        o["n"] = e.count;
        if (e.untilUtc != INT64_MIN) o["u"] = e.untilUtc;   // omit the "none" sentinel
        o["b"] = e.bydayMask;
    }
    out.clear();
    serializeJson(doc, out);
}

// Deserialize a cache document into ev[] (<= max). Returns the number of
// events written. Tolerant by design:
//   - empty / unparseable / non-object input  -> 0 events, lastSyncUtc = 0
//   - valid object but missing "ev" array     -> 0 events, lastSyncUtc kept
//   - per-event fields are validated and sanitised (bounds, ranges, NUL term)
// Events with no parseable start time are skipped, not crashed on.
inline int deserializeCalendarCache(const std::string &in, CalendarEvent *ev,
                                    int max, int64_t &lastSyncUtc) {
    lastSyncUtc = 0;
    if (!ev || max <= 0) return 0;
    if (in.empty()) return 0;

    JsonDocument doc;
    if (deserializeJson(doc, in)) return 0;      // corrupt / truncated -> empty
    if (!doc.is<JsonObject>()) return 0;

    lastSyncUtc = doc["sync"] | (int64_t)0;      // valid doc: keep the timestamp
    JsonArray arr = doc["ev"].as<JsonArray>();
    if (arr.isNull()) return 0;                  // ...even with zero events

    int n = 0;
    for (JsonObject o : arr) {
        if (n >= max) break;                     // bound to caller capacity

        CalendarEvent e;
        calEventClear(e);                        // safe defaults first

        int64_t s = o["s"] | INT64_MAX;          // start is mandatory
        if (s == INT64_MAX) continue;            // no start -> skip this event
        e.startUtc = s;

        int64_t en = o["e"] | s;
        if (en < s) en = s;                      // never negative duration
        e.endUtc = en;

        const char *t = o["t"] | "";
        strncpy(e.title, t, CAL_TITLE_MAX - 1);
        e.title[CAL_TITLE_MAX - 1] = '\0';       // always NUL-terminated

        long c  = o["c"] | 0L;
        e.category = (uint8_t)(c & 0xFF);

        e.allDay = o["a"] | false;

        long f  = o["f"] | 0L;
        e.freq = (f >= 0 && f <= 3) ? (CalFreq)f : CalFreq::None;  // sanitise tag

        long iv = o["i"] | 1L;
        e.interval = (iv > 0) ? (int32_t)iv : 1;

        long cn = o["n"] | 0L;
        e.count = (cn > 0) ? (int32_t)cn : 0;

        e.untilUtc = o["u"] | INT64_MIN;

        long b  = o["b"] | 0L;
        e.bydayMask = (uint8_t)(b & 0x7F);       // 7 weekday bits only

        ev[n++] = e;
    }
    return n;
}

} // namespace core

// ===========================================================================
//  Firmware-only fs::FS wrapper (excluded from the native test build)
// ===========================================================================
#ifdef ARDUINO
#include <FS.h>

class CalendarStore {
public:
    explicit CalendarStore(fs::FS &fs, const char *path = CAL_CACHE_FILE)
        : _fs(fs), _path(path) {}

    // Write the cache. count is bounded to [0, CAL_MAX_EVENTS]. Returns true
    // only if every byte was written.
    bool save(const core::CalendarEvent *events, int count, int64_t lastSyncUtc);

    // Read the cache into events[] (<= max). Missing / corrupt / oversized
    // files yield 0 (never a crash). *lastSyncUtc (optional) receives the
    // stored timestamp, or 0 when nothing usable was found.
    int load(core::CalendarEvent *events, int max, int64_t *lastSyncUtc = nullptr);

private:
    fs::FS      &_fs;
    const char  *_path;
};
#endif // ARDUINO
