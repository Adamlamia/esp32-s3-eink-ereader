// ===========================================================================
//  WeatherStore.cpp  —  fs::FS wrapper for the weather cache (WTH·R1)
// ===========================================================================
//  Thin Arduino-side implementation over the pure core::serializeWeatherCache
//  / core::deserializeWeatherCache seam declared in core/OpenMeteo.h (which
//  is unit-tested on the host). All corruption tolerance lives in the seam;
//  this file only moves bytes between the filesystem and an in-memory
//  std::string. Mirrors CalendarStore.cpp.
// ===========================================================================
#include "WeatherStore.h"

#ifdef ARDUINO   // the whole class is firmware-only (see header)

bool WeatherStore::save(const core::WeatherSnapshot &snap) {
    std::string json;
    core::serializeWeatherCache(json, snap);

    File f = _fs.open(_path, "w");
    if (!f) {
        Serial.printf("[WthStore] open(%s, w) failed\n", _path);
        return false;
    }
    size_t wrote = f.write((const uint8_t *)json.data(), json.size());
    f.close();
    if (wrote != json.size()) {
        Serial.printf("[WthStore] short write (%u/%u bytes)\n",
                      (unsigned)wrote, (unsigned)json.size());
        return false;
    }
    Serial.printf("[WthStore] saved snapshot (%u bytes) -> %s\n",
                  (unsigned)json.size(), _path);
    return true;
}

bool WeatherStore::load(core::WeatherSnapshot &snap) {
    core::weatherSnapshotClear(snap);
    if (!_fs.exists(_path)) return false;          // first boot: empty weather

    File f = _fs.open(_path, "r");
    if (!f) return false;
    size_t sz = f.size();
    if (sz == 0 || sz > core::WEATHER_CACHE_MAX_BYTES) {
        f.close();                                 // empty / oversized -> corrupt
        Serial.printf("[WthStore] %s: bad size %u, treating as empty\n",
                      _path, (unsigned)sz);
        return false;
    }
    std::string buf;
    buf.resize(sz);
    size_t rd = f.readBytes(&buf[0], sz);
    f.close();
    if (rd != sz) return false;                    // short read -> treat as corrupt

    bool ok = core::deserializeWeatherCache(buf, snap);
    if (!ok) core::weatherSnapshotClear(snap);     // belt + braces: seam clears too
    Serial.printf("[WthStore] load %s (fetched=%lld) <- %s\n",
                  ok ? "ok" : "failed (corrupt?)", (long long)snap.fetchedUtc, _path);
    return ok;
}

#endif // ARDUINO
