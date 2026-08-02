#pragma once
// ===========================================================================
//  GithubSync  —  self-contained GitHub dashboard sync session (DEV·R1)
// ===========================================================================
//  One blocking call brings up STA-only Wi-Fi, fetches the time via NTP, then
//  for each configured repo (secrets.h GITHUB_REPO_0..3) makes three READ-ONLY
//  HTTPS GETs against the GitHub REST API:
//
//    search/issues?q=type:pr+state:open+repo:{repo}    -> openPRs   (total_count)
//    search/issues?q=type:issue+state:open+repo:{repo} -> openIssues (total_count)
//    repos/{repo}/actions/runs?per_page=1              -> lastCi    (workflow_runs)
//
//  parsing each via the pure core::parseGithubCount / core::parseGithubCi seams,
//  stamping name + fetchedUtc, persisting the snapshot via GithubStore, and
//  ALWAYS powering Wi-Fi back down before returning.
//
//  This class mirrors src/apps/weather/WeatherSync.h/.cpp (and CalendarSync)
//  deliberately — same portal guard, same RAII WifiOffGuard, same 24 KB
//  trampoline task, same STA+NTP plumbing shape (all shared via app/WifiSession)
//  — so the files stay diffable side by side.
//
//  Wi-Fi coordination with the WebPortal (important): identical to WeatherSync —
//  run() REFUSES to start while the portal is running (isRunning() covers both
//  the auto-started boot portal and a user-managed one) and returns a readable
//  message instead of fighting over WiFi.mode. Stop the portal first, then sync.
//
//  Stack design (why run() spawns a task): identical to WeatherSync — Arduino's
//  loopTask stack is only 8 KB, but mbedTLS (WiFiClientSecure) needs several KB
//  of headroom plus the JSON parse buffers on top, so run() executes
//  runInternal() on the dedicated 24 KB FreeRTOS task and blocks the caller on a
//  semaphore.
//
//  Auth: every request carries "Authorization: Bearer {GITHUB_PAT}",
//  "Accept: application/vnd.github+json" and "User-Agent: ESP32-EReader"
//  (GitHub requires a User-Agent). The PAT is a READ-ONLY secret from
//  src/secrets.h (git-ignored, #included by config.h if present); without it the
//  firmware compiles and fails gracefully ("No GITHUB_PAT"). WIFI_STA_SSID /
//  WIFI_STA_PASS gate the whole session exactly like the calendar/weather.
//
//  Clock convention: identical to the other syncs — time(nullptr) returns TRUE
//  UTC epoch seconds once NTP has fixed the clock; fetchedUtc / lastSyncUtc use
//  that value DIRECTLY. The NTP-validity floor is the shared CAL_CLOCK_MIN_EPOCH.
//
//  SECURITY: WiFiClientSecure::setInsecure() skips certificate validation,
//  matching CalendarSync/WeatherSync. Here the bearer PAT IS sensitive, so this
//  is the one place the shared TLS hardening matters most; it is deferred to the
//  same round that fixes the other apps: TODO(TLS).
// ===========================================================================
#include <stdint.h>
#include <stddef.h>
#include "app/SystemContext.h"

// --- Result of one sync session --------------------------------------------
struct GithubSyncResult {
    bool ok;               // true iff >=1 repo fetched AND the cache was written
    int  reposOk;          // repos whose PR+issue counts fetched successfully
    int  reposFailed;      // repos that failed (network / HTTP / parse)
    char message[64];      // short human-readable status/error, always NUL-terminated
};

class GithubSync {
public:
    explicit GithubSync(SystemContext &ctx) : _ctx(ctx) {}

    // Blocking sync session (runs on a dedicated high-stack task; see header).
    GithubSyncResult run();

    // The session body. Public so the trampoline task can call it; not meant
    // to be called directly from the loop stack (stack overflow risk).
    GithubSyncResult runInternal();

private:
    // The STA-join + NTP plumbing (wifiSessionStaNtp), the RAII WifiOffGuard and
    // the 24 KB trampoline (wifiSessionRunOnTask) live in app/WifiSession, shared
    // verbatim with CalendarSync/WeatherSync. run() forwards to the shared
    // trampoline; runInternal() is the GitHub session body.

    SystemContext &_ctx;
};
