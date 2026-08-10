#pragma once
// ===========================================================================
//  app/WifiSession  --  shared STA Wi-Fi + NTP + sync-task facility (WTH-R2)
// ===========================================================================
//  The single place the radio lifecycle lives, extracted in the WTH-R2 review
//  from the near-verbatim parallels in CalendarSync and WeatherSync (the two
//  WTH-R2 review markers that asked for exactly this). Both sync sessions reuse:
//
//    (portal guard)            - documented rule; each caller checks inline so
//                                its serial diagnostics stay verbatim
//    WifiOffGuard              - RAII WIFI_OFF on every exit path
//    wifiSessionStaNtp         - STA join + configTime()/NTP -> TRUE UTC seconds
//    wifiSessionRunOnTask      - run a session body on the dedicated 24 KB task,
//                                blocking the caller on a semaphore
//
//  Behaviour-preserving by construction: this is the calendar's live-verified
//  plumbing moved verbatim (only the serial log tag is parameterised). The
//  callers keep their own portal-guard logging and result structs, so nothing
//  observable changes for the calendar: CalendarSync::run() / syncTimeOnly()
//  and CalendarSyncResult keep the exact same shape and semantics.
//
//  Clock convention (unchanged): configTime()'s gmtOffset only steers
//  localtime(); once NTP has fixed the clock, time(nullptr) returns TRUE UTC
//  epoch seconds directly - used with NO offset subtraction. The NTP-validity
//  floor is the shared CAL_CLOCK_MIN_EPOCH from config.h.
//
//  Firmware-only: like the sync sessions themselves, this needs the Arduino
//  Wi-Fi + FreeRTOS stack, so it is compiled for the device build alone. The
//  native test build excludes src/ (platformio.ini build_src_filter) and never
//  sees it; the pure-logic seams under test stay HAL-free.
// ===========================================================================
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>             // FreeRTOS (semaphore/task) + snprintf; the
                                 // portal guard itself stays inline at callers

class WiFiClientSecure;          // forward declaration for TLS helper

// --- Session tuning (one envelope, shared by calendar + weather) ------------
static const int     WIFI_SESSION_WIFI_TRIES  = 30;        // x 500 ms -> <= 15 s join
static const int     WIFI_SESSION_NTP_TRIES   = 30;        // x 500 ms -> <= 15 s NTP
static const size_t  WIFI_SESSION_STACK_BYTES = 24 * 1024; // mbedTLS + JSON headroom

// --- Portal guard -----------------------------------------------------------
// A sync session must REFUSE to start while the Wi-Fi upload portal is running
// (auto-started at boot OR toggled on by hand): it needs WIFI_STA only, and
// switching WiFi.mode() while the portal's async HTTP server is live would yank
// the radio out from under it. Each caller performs this check inline
// (_ctx.portal && _ctx.portal->isRunning()) so its own serial diagnostics stay
// verbatim; the rule is documented here, the check stays at the call site.

// --- RAII Wi-Fi-off guard ---------------------------------------------------
// Powers the radio down on EVERY exit path (success, failure, early return) so
// it never stays hot after a session. Declare one at the top of the
// secrets-guarded session body. (Definition in WifiSession.cpp.)
struct WifiOffGuard { ~WifiOffGuard(); };

// --- STA + NTP plumbing -----------------------------------------------------
// Bring up STA-only Wi-Fi and obtain a valid NTP time. Returns true and sets
// nowUtc (TRUE UTC epoch seconds) on success; on failure returns false with a
// short readable reason in msg. The CALLER owns the radio lifecycle (holds the
// WifiOffGuard), so this never leaves Wi-Fi on by itself. `tag` prefixes the
// serial diagnostics so calendar ([CalSync]) and weather ([WthSync]) stay
// distinguishable in the log. Compiled only when STA secrets exist; callers
// guard their use with the same #if. (Definition in WifiSession.cpp.)
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
bool wifiSessionStaNtp(int64_t &nowUtc, char *msg, size_t msgLen, const char *tag);
#endif

// --- TLS CA certificate helper (P0-3) --------------------------------------
// Configure a WiFiClientSecure with proper CA certificate validation.
// Returns true if a CA cert was found for the host and applied.
// Returns false if no cert available (caller should decide whether to proceed).
// When false is returned, the client is left unconfigured (caller may fall back
// to setInsecure() with a serial warning, or abort).
// (Definition in WifiSession.cpp.)
bool wifiSessionApplyCa(WiFiClientSecure &client, const char *host, const char *tag);

// --- Dedicated sync-task trampoline -----------------------------------------
// Run a blocking session body on the dedicated high-stack (24 KB) FreeRTOS task
// and block the caller until it completes. Arduino's loopTask stack is only
// 8 KB, but mbedTLS (WiFiClientSecure) plus the parse buffers need far more, so
// the session never runs on the caller's stack.
//
// ResultT must be default-constructible, copy-assignable, and expose a
//   char message[...];   // both sync result structs do
// so the trampoline can report a task-creation failure readably. It is value-
// initialised (zeroed) before the body runs. `name` is the FreeRTOS task name
// shown in traces ("calSync" / "wthSync"). The wait is bounded in practice:
// every session body has internal Wi-Fi / NTP / HTTP timeouts.
//
// Header-only template (instantiated per result type in each sync .cpp). The
// job + entry live in an anonymous namespace so the two translation units do
// not collide on symbols.
namespace {
template <typename ResultT, typename Fn>
struct WifiSessionJob { Fn *body; ResultT *out; SemaphoreHandle_t done; };

template <typename ResultT, typename Fn>
void wifiSessionTaskEntry(void *arg) {
    WifiSessionJob<ResultT, Fn> *job = static_cast<WifiSessionJob<ResultT, Fn> *>(arg);
    *job->out = (*job->body)();
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);   // task frees itself; job is owned by the caller
}
} // namespace

template <typename ResultT, typename Fn>
ResultT wifiSessionRunOnTask(const char *name, Fn body) {
    ResultT r = {};

    WifiSessionJob<ResultT, Fn> job;
    job.body = &body;
    job.out  = &r;
    job.done = xSemaphoreCreateBinary();
    if (!job.done) {
        snprintf(r.message, sizeof(r.message), "Sync task alloc failed");
        return r;
    }

    // NOTE: on ESP-IDF, xTaskCreate's stack size is in BYTES (unlike vanilla
    // FreeRTOS words). 24 KB covers mbedTLS + the parse buffers with headroom.
    BaseType_t started = xTaskCreate(wifiSessionTaskEntry<ResultT, Fn>, name,
                                     WIFI_SESSION_STACK_BYTES, &job, 1, nullptr);
    if (started != pdPASS) {
        vSemaphoreDelete(job.done);
        snprintf(r.message, sizeof(r.message), "Sync task create failed");
        return r;
    }

    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return r;
}
