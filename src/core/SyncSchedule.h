#pragma once
// ===========================================================================
//  core/SyncSchedule.h  —  pure scheduling decisions for calendar sync (R3)
// ===========================================================================
//  Header-only, heap-free, deterministic scheduling logic: decides WHEN the
//  calendar should auto-sync and how long until the next scheduled sync. It is
//  built ONLY on the pure integer date math in core/CalendarDate.h — no <ctime>
//  (gmtime/localtime), no HAL, no Wi-Fi — so the decisions are host-testable
//  under `pio test -e native` and identical on every machine (see CalendarDate.h
//  for why we avoid the host timezone-sensitive <ctime> functions).
//
//  Timezone model (matches the rest of the calendar core): a single FIXED utc
//  offset in seconds east of UTC (e.g. CAL_TZ_OFFSET_SEC = 8*3600 for Malaysia,
//  no DST). syncHourLocal is a local wall-clock hour-of-day, nominally [0,23].
//
//  All epoch inputs are TRUE UTC seconds (the same convention the ICS parser
//  and the core date seams use). On the device the caller obtains this from
//  time(nullptr) AFTER an NTP fix — note configTime()'s gmtOffset only steers
//  localtime(); time(nullptr) itself already returns UTC epoch seconds, so it
//  is used directly here with NO extra offset subtraction.
//
//  Two decisions:
//    shouldAutoSync()       — is a sync due right now? (clock valid, and one of:
//                             never synced / crossed the daily sync hour since
//                             the last sync / cache gone stale)
//    secondsUntilNextSync() — seconds from now until the NEXT occurrence of
//                             syncHourLocal (local); always > 0 so it is safe
//                             to feed straight into a timer wakeup.
// ===========================================================================
#include <cstdint>
#include "core/CalendarDate.h"   // todayStartUtc — pure integer date math

namespace core {

// UTC epoch of TODAY's occurrence of syncHourLocal (local) for the day that
// contains nowUtc. Depending on the local time of day this instant may be in
// the past (sync hour already passed today) or the future (not yet reached).
// Shared by both decisions below so they agree on the boundary exactly.
inline int64_t syncHourBoundaryUtc(int64_t nowUtc, int syncHourLocal, int32_t tzOffsetSec) {
    return todayStartUtc(nowUtc, tzOffsetSec) + (int64_t)syncHourLocal * 3600;
}

// Is a calendar sync due right now?
//   nowUtc         current TRUE UTC epoch seconds (<= 0 == clock not valid yet)
//   lastSyncUtc    TRUE UTC epoch of the last successful sync (<= 0 == never)
//   syncHourLocal  local hour-of-day the daily sync targets (e.g. 6 for 06:00)
//   tzOffsetSec    seconds east of UTC (e.g. 8*3600)
//   staleSec       force a resync once the cache is at least this old; a value
//                  <= 0 disables the stale backstop (daily boundary still applies)
//
// Returns false on an invalid clock: we cannot schedule against a bad time, so
// the caller should obtain NTP first rather than sync blindly.
inline bool shouldAutoSync(int64_t nowUtc, int64_t lastSyncUtc,
                           int syncHourLocal, int32_t tzOffsetSec, int64_t staleSec) {
    if (nowUtc <= 0) return false;          // no valid time yet -> never "due"
    if (lastSyncUtc <= 0) return true;      // never synced -> sync as soon as possible

    // Most recent local syncHour boundary at-or-before now: today's occurrence
    // if we have already reached it, otherwise yesterday's.
    int64_t boundary = syncHourBoundaryUtc(nowUtc, syncHourLocal, tzOffsetSec);
    if (nowUtc < boundary) boundary -= 86400;

    // Crossed the hour: the last sync predates that boundary and we are now at
    // or past it — i.e. a new local sync day has begun since we last fetched.
    // (nowUtc >= boundary always holds after the adjustment above; kept explicit
    // to document the intent "last sync before the boundary, now after it".)
    if (lastSyncUtc < boundary && nowUtc >= boundary) return true;

    // Stale backstop: even when no boundary was crossed yet (sync hour still
    // ahead today), resync once the cache is old enough. Guards against a device
    // that keeps missing the daily window. staleSec <= 0 disables this check.
    if (staleSec > 0 && (nowUtc - lastSyncUtc) >= staleSec) return true;

    return false;
}

// Seconds from now until the NEXT occurrence of syncHourLocal (local). Always
// > 0: if we are exactly at or just past today's occurrence, target tomorrow's.
// The result is safe to pass (scaled to microseconds) straight into
// esp_sleep_enable_timer_wakeup().
//
// Invalid clock (nowUtc <= 0): we cannot compute a wall-clock target, so return
// a short fixed interval (1 h) to wake and retry NTP soon. Callers additionally
// guard on clock validity before scheduling, so this is a defensive backstop.
inline int64_t secondsUntilNextSync(int64_t nowUtc, int syncHourLocal, int32_t tzOffsetSec) {
    if (nowUtc <= 0) return 3600;   // invalid clock: wake in 1 h to retry NTP
    int64_t target = syncHourBoundaryUtc(nowUtc, syncHourLocal, tzOffsetSec);
    if (nowUtc >= target) target += 86400;   // at / just past today's -> tomorrow
    int64_t delta = target - nowUtc;
    return delta > 0 ? delta : 86400;        // defensive: never return <= 0
}

} // namespace core
