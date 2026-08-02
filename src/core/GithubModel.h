#pragma once
// ===========================================================================
//  core/GithubModel.h  —  GitHub API parsing + cache seam (DEV·R1)
// ===========================================================================
//  Header-only, heap-free (no new/malloc; fixed buffers bounded by the
//  GITHUB_* macros), HAL-free pure-logic seam for the Dev Companion GitHub
//  dashboard, in the same style as core/OpenMeteo.h + core/CalendarEvent.h:
//
//    parseGithubCount      — search/issues response -> open PR / issue count
//    parseGithubCi         — check-runs / workflow-runs response -> CiState
//    serializeGithubCache  — repo status array -> compact /github.json document
//    deserializeGithubCache— /github.json document -> repo status array (tolerant)
//
//  API shapes consumed (GitHub REST v3, read-only):
//    search/issues  : {"total_count": N, "items": [...]}
//                     (q="type:pr+state:open+repo:X"   -> total_count = openPRs)
//                     (q="type:issue+state:open+repo:X"-> total_count = openIssues)
//    check-runs     : {"check_runs": [{"conclusion": "success"|"failure"|null,
//                                       "status": "completed"|"in_progress"|...}]}
//    actions/runs   : {"workflow_runs": [ {same conclusion/status shape} ]}
//
//  parseGithubCi accepts BOTH "check_runs" and "workflow_runs" arrays (same
//  item shape): the native tests exercise "check_runs" (the documented seam
//  contract) while GithubSync fetches /actions/runs?per_page=1 ("workflow_runs")
//  — one parser covers both without a fork.
//
//  CiState convention (MOST RECENT run = first array element):
//    Success  conclusion == "success"
//    Failure  conclusion == "failure"
//    Pending  conclusion null / absent (still running: in_progress / queued)
//    None     the document parsed but the runs array is missing or empty
//    Unknown  parse error (empty / malformed / non-object) OR a completed run
//             with an unrecognised conclusion (cancelled / skipped / timed_out)
//
//  fetchedUtc convention: parseGithubCount / parseGithubCi leave any timestamp
//  alone — they are pure and cannot know "now". GithubSync stamps fetchedUtc +
//  the repo name before persisting, and the cache round-trip carries them.
//
//  ArduinoJson 7 is header-only and identical on host and device, so the whole
//  seam compiles under `pio test -e native` with no Arduino core.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <ArduinoJson.h>
#include "config.h"   // GITHUB_* sizing constants (pure macros, no HAL)

// --- Safe fallbacks so the header compiles standalone (no config.h values) --
#ifndef GITHUB_NAME_MAX
  #define GITHUB_NAME_MAX 40
#endif
#ifndef GITHUB_MAX_REPOS
  #define GITHUB_MAX_REPOS 4
#endif

namespace core {

// --- Data types -------------------------------------------------------------
// Last CI status of a repo's most recent run (see the CiState convention above).
enum class CiState : uint8_t {
    Unknown = 0,   // parse error / unrecognised conclusion
    Success = 1,   // conclusion "success"
    Failure = 2,   // conclusion "failure"
    Pending = 3,   // still running (null conclusion)
    None    = 4,   // no runs at all (empty / missing array)
};

// One repo's dashboard snapshot. Plain-old-data, fixed size, safe to copy by
// value and to hold in a static array.
struct GithubRepoStatus {
    char     name[GITHUB_NAME_MAX];   // "owner/repo" display, NUL-terminated
    int      openPRs;                 // open pull requests (search total_count)
    int      openIssues;              // open issues (search total_count)
    CiState  lastCi;                  // most recent CI run state
    int64_t  fetchedUtc;              // when THIS snapshot was taken (0 = never)
};

// Reset a status to safe defaults so partially-parsed fields are never garbage
// (mirrors calEventClear / weatherSnapshotClear).
inline void githubRepoClear(GithubRepoStatus &s) {
    s.name[0]     = '\0';
    s.openPRs     = 0;
    s.openIssues  = 0;
    s.lastCi      = CiState::Unknown;
    s.fetchedUtc  = 0;
}

// --- search/issues count parser ---------------------------------------------
// Parse one search/issues response and return its "total_count" (>= 0). The
// caller chooses the query (type:pr vs type:issue), so the same parser yields
// openPRs OR openIssues. Returns -1 on any error: empty / malformed / non-object
// input, or a missing / non-numeric / negative total_count. Never crashes.
inline int parseGithubCount(const std::string &json) {
    if (json.empty()) return -1;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return -1;   // corrupt / truncated
    if (!doc.is<JsonObject>()) return -1;        // array / scalar / null

    JsonVariant tv = doc.as<JsonObject>()["total_count"];
    if (tv.isNull() || !tv.is<int>()) return -1; // missing / non-numeric
    long n = tv.as<long>();
    if (n < 0) return -1;                        // nonsensical
    return (int)n;
}

// --- check-runs / workflow-runs CI parser -----------------------------------
// Map a single run object's conclusion/status to a CiState (see convention).
inline CiState ciStateFromRun(JsonObject run) {
    // A run still in progress has a null conclusion (status in_progress/queued).
    JsonVariant concl = run["conclusion"];
    if (concl.isNull()) {
        // null conclusion: not concluded yet -> Pending (covers in_progress /
        // queued, and the odd completed-with-no-conclusion case).
        return CiState::Pending;
    }
    const char *c = concl.as<const char *>();
    if (!c) return CiState::Unknown;
    if (strcmp(c, "success") == 0) return CiState::Success;
    if (strcmp(c, "failure") == 0) return CiState::Failure;
    return CiState::Unknown;   // cancelled / skipped / timed_out / neutral / ...
}

// Parse a check-runs OR workflow-runs response and return the CiState of the
// MOST RECENT run (first array element). Returns None when the document parsed
// but the runs array is missing/empty, and Unknown on any parse error (empty /
// malformed / non-object). Never crashes.
inline CiState parseGithubCi(const std::string &json) {
    if (json.empty()) return CiState::Unknown;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return CiState::Unknown;   // corrupt
    if (!doc.is<JsonObject>()) return CiState::Unknown;        // not an object
    JsonObject root = doc.as<JsonObject>();

    // Accept either array key (same item shape): "check_runs" (documented seam
    // contract / native tests) first, then "workflow_runs" (/actions/runs).
    JsonArray runs = root["check_runs"].as<JsonArray>();
    if (runs.size() == 0) runs = root["workflow_runs"].as<JsonArray>();
    if (runs.size() == 0) return CiState::None;                // no runs at all

    JsonVariant first = runs[0];
    if (!first.is<JsonObject>()) return CiState::Unknown;
    return ciStateFromRun(first.as<JsonObject>());
}

// --- /github.json cache serialization (ArduinoJson 7, compact keys) ---------
// Schema v1 (mirrors the weather/calendar cache shape):
//   {
//     "v":    1,                    // schema version (bump if fields change)
//     "sync": 1785715200,           // lastSyncUtc, UTC epoch seconds
//     "repos": [
//       { "name": "owner/repo", "prs": 2, "iss": 5, "ci": 1, "fet": 1785715200 },
//       ...
//     ]
//   }
//   repos: name=display  prs=openPRs  iss=openIssues  ci=CiState(int)  fet=fetchedUtc
inline void serializeGithubCache(std::string &out, const GithubRepoStatus *repos,
                                 int n, int64_t lastSyncUtc) {
    JsonDocument doc;
    doc["v"]    = 1;
    doc["sync"] = lastSyncUtc;

    JsonArray arr = doc["repos"].to<JsonArray>();
    if (repos && n > 0) {
        if (n > GITHUB_MAX_REPOS) n = GITHUB_MAX_REPOS;        // clamp to capacity
        for (int i = 0; i < n; ++i) {
            const GithubRepoStatus &s = repos[i];
            JsonObject o = arr.add<JsonObject>();
            o["name"] = s.name;
            o["prs"]  = s.openPRs;
            o["iss"]  = s.openIssues;
            o["ci"]   = (int)s.lastCi;
            o["fet"]  = s.fetchedUtc;
        }
    }
    out.clear();
    serializeJson(doc, out);
}

// Deserialize a cache document into repos[] (overwritten, up to `max`) and set
// lastSyncUtc. Returns the number of repos loaded (0..min(max,GITHUB_MAX_REPOS)).
// Tolerant by design (this is the GithubStore::load corrupt-file contract,
// host-testable):
//   - empty / malformed / non-object input -> 0, lastSyncUtc = 0 (never a crash,
//     never stale half-data);
//   - a valid object always sanitises: name truncated to GITHUB_NAME_MAX-1 and
//     NUL-terminated, prs/iss clamped to >= 0, ci normalised to a valid CiState
//     (out-of-range -> Unknown), longer arrays clamped to capacity.
inline int deserializeGithubCache(const std::string &in, GithubRepoStatus *repos,
                                  int max, int64_t &lastSyncUtc) {
    lastSyncUtc = 0;
    if (!repos || max <= 0) return 0;
    if (in.empty()) return 0;

    JsonDocument doc;
    if (deserializeJson(doc, in)) return 0;      // corrupt / truncated
    if (!doc.is<JsonObject>()) return 0;

    lastSyncUtc = doc["sync"] | (int64_t)0;

    int cap = max;
    if (cap > GITHUB_MAX_REPOS) cap = GITHUB_MAX_REPOS;        // bound to capacity

    JsonArray arr = doc.as<JsonObject>()["repos"].as<JsonArray>();
    int n = 0;
    for (JsonObject o : arr) {
        if (n >= cap) break;                     // clamp to capacity
        GithubRepoStatus &s = repos[n];
        githubRepoClear(s);

        const char *nm = o["name"] | "";
        strncpy(s.name, nm, GITHUB_NAME_MAX - 1);
        s.name[GITHUB_NAME_MAX - 1] = '\0';      // always NUL-terminated

        long prs = o["prs"] | 0L;
        long iss = o["iss"] | 0L;
        s.openPRs    = (prs < 0) ? 0 : (int)prs; // never negative on screen
        s.openIssues = (iss < 0) ? 0 : (int)iss;

        long ci = o["ci"] | 0L;
        s.lastCi = (ci >= 0 && ci <= 4) ? (CiState)ci : CiState::Unknown;

        s.fetchedUtc = o["fet"] | (int64_t)0;
        ++n;
    }
    return n;
}

} // namespace core
