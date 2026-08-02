#pragma once
// ===========================================================================
//  GithubStore  —  /github.json cache persistence (DEV·R1)
// ===========================================================================
//  Persists the last GitHub dashboard snapshot (per-repo open PRs + issues +
//  last CI state + fetchedUtc, plus a lastSyncUtc stamp) as a small JSON
//  document at a fixed path on the active filesystem (SD or LittleFS, via
//  BookStorage::fs()).
//
//  Cache format: schema v1 lives in core/GithubModel.h (serializeGithubCache /
//  deserializeGithubCache) — compact keys {v, sync, repos:[{name,prs,iss,ci,fet}]}.
//
//  Robustness contract (mirrors WeatherStore/CalendarStore): load() MUST
//  tolerate a missing, truncated, oversized or corrupt file — it returns 0
//  repos (and lastSyncUtc = 0) rather than crashing. A power loss mid-save can
//  corrupt the file; the next load treats it as empty and the next refresh
//  rewrites it.
//
//  Structure: the pure serialize / deserialize logic lives in namespace core
//  (core/GithubModel.h) and is host-tested under `pio test -e native`; this
//  header only declares the thin fs::FS wrapper, compiled for the firmware
//  (ARDUINO) alone — exactly the WeatherStore.h seam pattern.
// ===========================================================================
#include <cstddef>
#include "core/GithubModel.h"   // core seam + config.h GITHUB_* constants

namespace core {

// Largest cache file we bother reading; a full snapshot (GITHUB_MAX_REPOS repos)
// serialises to well under 1 KB, so anything larger is treated as corrupt.
static constexpr size_t GITHUB_CACHE_MAX_BYTES = GITHUB_BODY_MAX;

} // namespace core

// ===========================================================================
//  Firmware-only fs::FS wrapper (excluded from the native test build)
// ===========================================================================
#ifdef ARDUINO
#include <FS.h>

class GithubStore {
public:
    explicit GithubStore(fs::FS &fs, const char *path = GITHUB_CACHE_FILE)
        : _fs(fs), _path(path) {}

    // Write the cache. Returns true only if every byte was written.
    bool save(const core::GithubRepoStatus *repos, int n, int64_t lastSyncUtc);

    // Read the cache into repos[] (up to max) and set lastSyncUtc. Missing /
    // corrupt / oversized files yield 0 repos (lastSyncUtc = 0) and return 0 —
    // never a crash. Returns the number of repos loaded (0 == empty/corrupt).
    int load(core::GithubRepoStatus *repos, int max, int64_t &lastSyncUtc);

private:
    fs::FS      &_fs;
    const char  *_path;
};
#endif // ARDUINO
