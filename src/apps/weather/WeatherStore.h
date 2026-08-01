#pragma once
// ===========================================================================
//  WeatherStore  —  /weather.json cache persistence (WTH·R1)
// ===========================================================================
//  Persists the last Open-Meteo snapshot (current conditions + 3-day forecast
//  + fetchedUtc + location label) as a small JSON document at a fixed path on
//  the active filesystem (SD or LittleFS, via BookStorage::fs()).
//
//  Cache format: schema v1 lives in core/OpenMeteo.h (serializeWeatherCache /
//  deserializeWeatherCache) — compact keys {v, sync, lbl, cur, days}.
//
//  Robustness contract (mirrors CalendarStore): load() MUST tolerate a
//  missing, truncated, oversized or corrupt file — it clears the snapshot and
//  returns false rather than crashing. A power loss mid-save can corrupt the
//  file; the next load treats it as empty and the next refresh rewrites it.
//
//  Structure: the pure serialize / deserialize logic lives in namespace core
//  (core/OpenMeteo.h) and is host-tested under `pio test -e native`; this
//  header only declares the thin fs::FS wrapper, compiled for the firmware
//  (ARDUINO) alone — exactly the CalendarStore.h seam pattern.
// ===========================================================================
#include <cstddef>
#include "core/OpenMeteo.h"   // core seam + config.h WEATHER_* constants

#ifndef WEATHER_CACHE_FILE
  #define WEATHER_CACHE_FILE "/weather.json"   // fixed path on the active FS
#endif

namespace core {

// Largest cache file we bother reading; a full snapshot (current + 3 days)
// serialises to well under 1 KB, so anything larger is treated as corrupt.
static constexpr size_t WEATHER_CACHE_MAX_BYTES = WEATHER_BODY_MAX;

} // namespace core

// ===========================================================================
//  Firmware-only fs::FS wrapper (excluded from the native test build)
// ===========================================================================
#ifdef ARDUINO
#include <FS.h>

class WeatherStore {
public:
    explicit WeatherStore(fs::FS &fs, const char *path = WEATHER_CACHE_FILE)
        : _fs(fs), _path(path) {}

    // Write the cache. Returns true only if every byte was written.
    bool save(const core::WeatherSnapshot &snap);

    // Read the cache into `snap`. Missing / corrupt / oversized files yield a
    // CLEARED snapshot (fetchedUtc = 0, cur.valid = false) and return false —
    // never a crash. True iff a usable document was loaded.
    bool load(core::WeatherSnapshot &snap);

private:
    fs::FS      &_fs;
    const char  *_path;
};
#endif // ARDUINO
