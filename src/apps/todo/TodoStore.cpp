// ===========================================================================
//  TodoStore.cpp  —  fs::FS wrapper for the Todo cache (TODO·R1)
// ===========================================================================
//  Thin Arduino-side implementation over the pure core::serializeTodoCache /
//  core::deserializeTodoCache seam declared in core/TodoModel.h (which is
//  unit-tested on the host). All corruption tolerance lives in the seam;
//  this file only moves bytes between the filesystem and an in-memory
//  std::string. Mirrors CalendarStore.cpp / WeatherStore.cpp.
// ===========================================================================
#include "TodoStore.h"

#ifdef ARDUINO   // the whole class is firmware-only (see header)

bool TodoStore::save(const core::TodoTask *tasks, int count,
                     const core::TodoDoneSet &done, int64_t lastSyncUtc) {
    if (count < 0) count = 0;
    if (count > TODO_MAX_TASKS) count = TODO_MAX_TASKS;

    std::string json;
    core::serializeTodoCache(json, tasks, count, done, lastSyncUtc);

    File f = _fs.open(_path, "w");
    if (!f) {
        Serial.printf("[TodoStore] open(%s, w) failed\n", _path);
        return false;
    }
    size_t wrote = f.write((const uint8_t *)json.data(), json.size());
    f.close();
    if (wrote != json.size()) {
        Serial.printf("[TodoStore] short write (%u/%u bytes)\n",
                      (unsigned)wrote, (unsigned)json.size());
        return false;
    }
    Serial.printf("[TodoStore] saved %d tasks + %d done-keys (%u bytes) -> %s\n",
                  count, done.count, (unsigned)json.size(), _path);
    return true;
}

int TodoStore::load(core::TodoTask *tasks, int max,
                    core::TodoDoneSet *done, int64_t *lastSyncUtc) {
    int64_t sync = 0;
    if (lastSyncUtc) *lastSyncUtc = 0;
    if (done) core::todoDoneClear(*done);
    if (!_fs.exists(_path)) return 0;            // first boot: empty todo list

    File f = _fs.open(_path, "r");
    if (!f) return 0;
    size_t sz = f.size();
    if (sz == 0 || sz > core::TODO_CACHE_MAX_BYTES) {
        f.close();                               // empty / oversized -> corrupt
        Serial.printf("[TodoStore] %s: bad size %u, treating as empty\n",
                      _path, (unsigned)sz);
        return 0;
    }
    std::string buf;
    buf.resize(sz);
    size_t rd = f.readBytes(&buf[0], sz);
    f.close();
    if (rd != sz) return 0;                      // short read -> treat as corrupt

    int n = core::deserializeTodoCache(buf, tasks, max, done, sync);
    if (lastSyncUtc) *lastSyncUtc = sync;
    Serial.printf("[TodoStore] loaded %d tasks + %d done-keys (lastSync=%lld) <- %s\n",
                  n, done ? done->count : -1, (long long)sync, _path);
    return n;
}

#endif // ARDUINO
