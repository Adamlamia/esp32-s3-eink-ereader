// ===========================================================================
//  GithubSync.cpp  —  Wi-Fi + NTP + HTTPS GitHub dashboard sync (DEV·R1)
// ===========================================================================
//  See GithubSync.h for the design notes (portal coordination, task stack,
//  auth, secrets convention). Everything here reuses the DEV·R1 seams:
//    core::parseGithubCount  — search/issues total_count (pure, host-tested)
//    core::parseGithubCi     — actions/runs workflow_runs -> CiState (host-tested)
//    GithubStore             — /github.json persistence
//  The session shape mirrors WeatherSync.cpp / CalendarSync.cpp line for line
//  where it matters (portal guard -> RAII WifiOffGuard -> wifiSessionStaNtp ->
//  HTTPS GET(s) -> persist) so the files stay diffable. The calendar's Wi-Fi
//  lifecycle fixes (true-UTC clock, pre-alloc body-size check) are inherited.
// ===========================================================================
#include "GithubSync.h"
#include "GithubStore.h"
#include "app/AppManager.h"
#include "app/WifiSession.h"
#include "core/GithubModel.h"
#include "core/CalendarEvent.h"   // CAL_CLOCK_MIN_EPOCH lives in config.h (shared floor)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <string.h>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// --- Session tuning (same envelope as the other syncs) -----------------------
// Wi-Fi-join / NTP-wait tries, the clock-sane floor and the 24 KB task stack
// live in app/WifiSession (WIFI_SESSION_*), shared with Calendar/Weather. The
// HTTP timeouts stay here: they are GitHub-specific.
static const int SYNC_HTTP_TIMEOUT_MS = 12000;
static const int SYNC_HTTP_CONNECT_MS = 8000;

// Request-URL staging buffer. Longest URL is the search query
// ("https://api.github.com/search/issues?q=type:issue+state:open+repo:" is
// ~66 chars) plus a GITHUB_NAME_MAX repo (~40) — 192 covers it with headroom.
static const int GH_URL_MAX = 192;

// ===========================================================================
//  One authenticated GitHub REST GET (helper)
// ===========================================================================
//  Returns the HTTP code (0 == begin() failed). On HTTP 200 with a non-empty
//  body <= GITHUB_BODY_MAX, fills `body`; otherwise `body` is left untouched.
//  The PAT is sent as a bearer token; Accept + User-Agent are the headers
//  GitHub's REST v3 expects (User-Agent is mandatory). setInsecure() matches
//  the other syncs: TODO(TLS) defers proper CA validation to a shared round.
static int githubGet(const char *url, const char *pat, String &body) {
    WiFiClientSecure client;
    client.setInsecure();   // TODO(TLS): shared CA-validation hardening round
    HTTPClient http;
    http.setConnectTimeout(SYNC_HTTP_CONNECT_MS);
    http.setTimeout(SYNC_HTTP_TIMEOUT_MS);
    http.setUserAgent("ESP32-EReader");

    int code = 0;
    if (!http.begin(client, url)) {
        Serial.printf("[GhSync] begin() failed: %s\n", url);
        return 0;
    }
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("Authorization", String("Bearer ") + pat);
    code = http.GET();

    // Inherited from the calendar's R4 fix: when Content-Length is known, reject
    // oversized bodies BEFORE allocating them — getString() would otherwise pull
    // the whole body onto the heap first. Chunked / unknown (-1) caught post-fetch.
    int declared = http.getSize();
    if (code == HTTP_CODE_OK && declared > (int)GITHUB_BODY_MAX) {
        Serial.printf("[GhSync] body too large (%d bytes): %s\n", declared, url);
    } else if (code == HTTP_CODE_OK) {
        String b = http.getString();
        if (b.length() > 0 && b.length() <= GITHUB_BODY_MAX) {
            body = b;
        } else {
            Serial.printf("[GhSync] bad body (%u bytes): %s\n",
                          (unsigned)b.length(), url);
            code = 0;   // treat an unusable body as a failed fetch
        }
    }
    http.end();
    return code;
}

// ===========================================================================
//  Session body (runs on the dedicated sync task — NOT the loop stack)
// ===========================================================================
GithubSyncResult GithubSync::runInternal() {
    GithubSyncResult r = {};   // zeroed: ok=false, counts 0, message ""

    // --- Wi-Fi coordination (see header): never fight the portal over the
    // radio. isRunning() covers both the auto-started boot portal and one the
    // user toggled on by hand; either way the async HTTP server is live and a
    // WiFi.mode() switch would break it.
    if (_ctx.portal && _ctx.portal->isRunning()) {
        snprintf(r.message, sizeof(r.message), "Wi-Fi portal active - stop it first");
        Serial.printf("[GhSync] aborted: WebPortal running (userManaged=%d)\n",
                      _ctx.manager ? (int)_ctx.manager->webUserManaged() : -1);
        return r;
    }

#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    // RAII guard: Wi-Fi is powered down on EVERY exit path below — success,
    // failure, early return — so the radio never stays hot after a sync.
    WifiOffGuard wifiGuard;   // shared app/WifiSession RAII Wi-Fi-off

    // --- Repo table (secrets.h GITHUB_REPO_0..3, each individually optional) ---
    // The ORIGINAL index is irrelevant here (unlike the calendar feeds): the
    // dashboard just lists whatever repos are defined, in order.
    const char *repos[GITHUB_MAX_REPOS];
    int repoCount = 0;
#ifdef GITHUB_REPO_0
    repos[repoCount++] = GITHUB_REPO_0;
#endif
#ifdef GITHUB_REPO_1
    repos[repoCount++] = GITHUB_REPO_1;
#endif
#ifdef GITHUB_REPO_2
    repos[repoCount++] = GITHUB_REPO_2;
#endif
#ifdef GITHUB_REPO_3
    repos[repoCount++] = GITHUB_REPO_3;
#endif
    if (repoCount == 0) {
        snprintf(r.message, sizeof(r.message), "No GITHUB_REPO_n in secrets.h");
        Serial.println("[GhSync] aborted: no repos defined");
        return r;
    }

    // --- PAT (read-only secret). No compiled-in default: without it the sync
    // fails loudly and readably, and the UI shows its empty state.
    const char *pat =
#ifdef GITHUB_PAT
        GITHUB_PAT;
#else
        nullptr;
#endif
    if (!pat || pat[0] == '\0') {
        snprintf(r.message, sizeof(r.message), "No GITHUB_PAT (src/secrets.h)");
        Serial.println("[GhSync] GITHUB_PAT not defined - sync disabled");
        return r;
    }

    // --- 1+2. STA-only Wi-Fi + NTP (shared app/WifiSession plumbing) ---------
    int64_t nowUtc = 0;
    if (!wifiSessionStaNtp(nowUtc, r.message, sizeof(r.message), "[GhSync]")) return r;
    Serial.printf("[GhSync] time synced, nowUtc=%lld\n", (long long)nowUtc);

    // --- 3. Fetch each repo (3 read-only GETs apiece) ------------------------
    core::GithubRepoStatus results[GITHUB_MAX_REPOS];
    char url[GH_URL_MAX];

    for (int i = 0; i < repoCount; i++) {
        core::GithubRepoStatus st;
        core::githubRepoClear(st);
        strncpy(st.name, repos[i], GITHUB_NAME_MAX - 1);
        st.name[GITHUB_NAME_MAX - 1] = '\0';
        st.fetchedUtc = nowUtc;

        String body;
        bool countsOk = true;   // a repo is "ok" iff BOTH counts fetched

        // open PRs: search/issues?q=type:pr+state:open+repo:{repo}
        // per_page=1 keeps the body tiny: we only read total_count, but GitHub's
        // default per_page=30 returns up to 30 full issue objects (~tens of KB),
        // which would exceed GITHUB_BODY_MAX and be rejected -> bogus 0 counts.
        body = "";
        snprintf(url, sizeof(url),
                 "https://api.github.com/search/issues?q=type:pr+state:open+repo:%s&per_page=1",
                 repos[i]);
        if (githubGet(url, pat, body) == HTTP_CODE_OK) {
            int c = core::parseGithubCount(std::string(body.c_str()));
            if (c >= 0) st.openPRs = c; else countsOk = false;
        } else {
            countsOk = false;
        }

        // open issues: search/issues?q=type:issue+state:open+repo:{repo}
        // per_page=1 as above (only total_count is consumed).
        body = "";
        snprintf(url, sizeof(url),
                 "https://api.github.com/search/issues?q=type:issue+state:open+repo:%s&per_page=1",
                 repos[i]);
        if (githubGet(url, pat, body) == HTTP_CODE_OK) {
            int c = core::parseGithubCount(std::string(body.c_str()));
            if (c >= 0) st.openIssues = c; else countsOk = false;
        } else {
            countsOk = false;
        }

        // last CI: repos/{repo}/actions/runs?per_page=1 (best-effort — a failure
        // here leaves lastCi = Unknown rather than failing the whole repo).
        body = "";
        snprintf(url, sizeof(url),
                 "https://api.github.com/repos/%s/actions/runs?per_page=1",
                 repos[i]);
        if (githubGet(url, pat, body) == HTTP_CODE_OK) {
            st.lastCi = core::parseGithubCi(std::string(body.c_str()));
        }

        results[i] = st;
        if (countsOk) r.reposOk++; else r.reposFailed++;
        Serial.printf("[GhSync] %s: PRs=%d issues=%d ci=%d (%s)\n",
                      repos[i], st.openPRs, st.openIssues, (int)st.lastCi,
                      countsOk ? "ok" : "partial/failed");
    }

    if (r.reposOk == 0) {
        snprintf(r.message, sizeof(r.message), "All %d repo(s) failed", r.reposFailed);
        return r;
    }

    // --- 4. Persist ----------------------------------------------------------
    GithubStore store(_ctx.storage.fs());
    if (!store.save(results, repoCount, nowUtc)) {
        snprintf(r.message, sizeof(r.message), "Cache write failed");
        return r;
    }

    r.ok = true;
    if (r.reposFailed == 0)
        snprintf(r.message, sizeof(r.message), "%d repo(s) synced", r.reposOk);
    else
        snprintf(r.message, sizeof(r.message), "%d repo(s), %d failed",
                 r.reposOk, r.reposFailed);
    Serial.printf("[GhSync] done: %s\n", r.message);
    return r;   // wifiGuard: WiFi.mode(WIFI_OFF) here
#else
    // No STA credentials compiled in: fail loudly and readably, never crash.
    snprintf(r.message, sizeof(r.message), "No Wi-Fi secrets (src/secrets.h)");
    Serial.println("[GhSync] WIFI_STA_SSID/PASS not defined - sync disabled");
    return r;
#endif
}

// ===========================================================================
//  Blocking entry point (loop-stack safe: work happens on a 24 KB task)
// ===========================================================================
//  The semaphore + xTaskCreate trampoline is shared with Calendar/Weather
//  (app/WifiSession::wifiSessionRunOnTask); run() just supplies the GitHub
//  session body. Stack-safety + Wi-Fi-lifecycle behaviour are unchanged.
GithubSyncResult GithubSync::run() {
    return wifiSessionRunOnTask<GithubSyncResult>("ghSync",
        [this]() { return runInternal(); });
}
