// ===========================================================================
//  GithubStore.cpp  —  fs::FS wrapper for the GitHub cache (DEV·R1)
// ===========================================================================
//  Thin Arduino-side implementation over the pure core::serializeGithubCache /
//  core::deserializeGithubCache seam declared in core/GithubModel.h (which is
//  unit-tested on the host). All corruption tolerance lives in the seam; this
//  file only moves bytes between the filesystem and an in-memory std::string.
//  Mirrors WeatherStore.cpp.
// ===========================================================================
#include "GithubStore.h"

#ifdef ARDUINO   // the whole class is firmware-only (see header)

bool GithubStore::save(const core::GithubRepoStatus *repos, int n,
                       int64_t lastSyncUtc) {
    std::string json;
    core::serializeGithubCache(json, repos, n, lastSyncUtc);

    File f = _fs.open(_path, "w");
    if (!f) {
        Serial.printf("[GhStore] open(%s, w) failed\n", _path);
        return false;
    }
    size_t wrote = f.write((const uint8_t *)json.data(), json.size());
    f.close();
    if (wrote != json.size()) {
        Serial.printf("[GhStore] short write (%u/%u bytes)\n",
                      (unsigned)wrote, (unsigned)json.size());
        return false;
    }
    Serial.printf("[GhStore] saved %d repo(s) (%u bytes) -> %s\n",
                  n, (unsigned)json.size(), _path);
    return true;
}

int GithubStore::load(core::GithubRepoStatus *repos, int max, int64_t &lastSyncUtc) {
    lastSyncUtc = 0;
    if (!repos || max <= 0) return 0;
    if (!_fs.exists(_path)) return 0;            // first boot: empty dashboard

    File f = _fs.open(_path, "r");
    if (!f) return 0;
    size_t sz = f.size();
    if (sz == 0 || sz > core::GITHUB_CACHE_MAX_BYTES) {
        f.close();                               // empty / oversized -> corrupt
        Serial.printf("[GhStore] %s: bad size %u, treating as empty\n",
                      _path, (unsigned)sz);
        return 0;
    }
    std::string buf;
    buf.resize(sz);
    size_t rd = f.readBytes(&buf[0], sz);
    f.close();
    if (rd != sz) return 0;                      // short read -> treat as corrupt

    int n = core::deserializeGithubCache(buf, repos, max, lastSyncUtc);
    Serial.printf("[GhStore] load %d repo(s) (sync=%lld) <- %s\n",
                  n, (long long)lastSyncUtc, _path);
    return n;
}

#endif // ARDUINO
