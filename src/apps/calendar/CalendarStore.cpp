// ===========================================================================
//  CalendarStore.cpp  —  fs::FS wrapper for the calendar cache (Round 2)
// ===========================================================================
//  Thin Arduino-side implementation over the pure core::serialize /
//  core::deserialize seam declared in CalendarStore.h (which is unit-tested
//  on the host). All corruption tolerance lives in the seam; this file only
//  moves bytes between the filesystem and the in-memory std::string.
// ===========================================================================
#include "CalendarStore.h"

#ifdef ARDUINO   // the whole class is firmware-only (see header)

bool CalendarStore::save(const core::CalendarEvent *events, int count,
                         int64_t lastSyncUtc) {
    if (count < 0) count = 0;
    if (count > CAL_MAX_EVENTS) count = CAL_MAX_EVENTS;

    std::string json;
    core::serializeCalendarCache(json, events, count, lastSyncUtc);

    File f = _fs.open(_path, "w");
    if (!f) {
        Serial.printf("[CalStore] open(%s, w) failed\n", _path);
        return false;
    }
    size_t wrote = f.write((const uint8_t *)json.data(), json.size());
    f.close();
    if (wrote != json.size()) {
        Serial.printf("[CalStore] short write (%u/%u bytes)\n",
                      (unsigned)wrote, (unsigned)json.size());
        return false;
    }
    Serial.printf("[CalStore] saved %d events (%u bytes) -> %s\n",
                  count, (unsigned)json.size(), _path);
    return true;
}

int CalendarStore::load(core::CalendarEvent *events, int max,
                        int64_t *lastSyncUtc) {
    int64_t sync = 0;
    if (lastSyncUtc) *lastSyncUtc = 0;
    if (!events || max <= 0) return 0;
    if (!_fs.exists(_path)) return 0;            // first boot: empty calendar

    File f = _fs.open(_path, "r");
    if (!f) return 0;
    size_t sz = f.size();
    if (sz == 0 || sz > core::CAL_CACHE_MAX_BYTES) {
        f.close();                               // empty / oversized -> corrupt
        Serial.printf("[CalStore] %s: bad size %u, treating as empty\n",
                      _path, (unsigned)sz);
        return 0;
    }
    std::string buf;
    buf.resize(sz);
    size_t rd = f.readBytes(&buf[0], sz);
    f.close();
    if (rd != sz) return 0;                      // short read -> treat as corrupt

    int n = core::deserializeCalendarCache(buf, events, max, sync);
    if (lastSyncUtc) *lastSyncUtc = sync;
    Serial.printf("[CalStore] loaded %d events (lastSync=%lld) <- %s\n",
                  n, (long long)sync, _path);
    return n;
}

#endif // ARDUINO
