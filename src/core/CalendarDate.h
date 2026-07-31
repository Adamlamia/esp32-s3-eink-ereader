#pragma once
// ===========================================================================
//  core/CalendarDate.h  —  pure-integer civil date/time math (seam)
// ===========================================================================
//  Header-only, heap-free, deterministic date math. Deliberately avoids
//  <ctime> (gmtime/localtime/mktime): those consult the host timezone and are
//  non-deterministic across machines, which would make the unit tests flaky.
//  Instead we use Howard Hinnant's public-domain civil-date algorithms
//  (days_from_civil / civil_from_days), which are pure integer arithmetic over
//  the proleptic Gregorian calendar and identical on every host.
//
//  Weekday convention: 0 = Monday ... 6 = Sunday (ISO-style, Monday-start
//  weeks). This matches the calendar UI's Monday-first grid and differs from
//  <ctime>'s Sunday-first tm_wday — keep it in mind when comparing.
//
//  Timezone model: a single FIXED utc offset (seconds east of UTC) is passed
//  to each conversion. There is no DST handling because the target locale
//  (Malaysia, UTC+8) has none; "local" always means nowUtc + tzOffsetSec.
// ===========================================================================
#include <cstdint>
#include "core/CalendarEvent.h"

namespace core {

// --- Civil <-> day-number conversion (Howard Hinnant, public domain) -------
// Days since 1970-01-01 for a proleptic Gregorian date. Pure integer math.
inline int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                 // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;     // [0, 146096]
    return era * 146097 + (int64_t)doe - 719468;
}

// Inverse of daysFromCivil: day number -> (year, month, day).
inline void civilFromDays(int64_t z, int64_t &y, unsigned &m, unsigned &d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);              // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    y = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);   // [0, 365]
    const unsigned mp  = (5 * doy + 2) / 153;                       // [0, 11]
    d = doy - (153 * mp + 2) / 5 + 1;                               // [1, 31]
    m = mp + (mp < 10 ? 3 : -9);                                    // [1, 12]
}

// --- Epoch <-> civil -------------------------------------------------------
// UTC epoch seconds for a civil date/time interpreted at tzOffsetSec east of
// UTC. wall == UTC when tzOffsetSec == 0; for UTC+8 pass 8*3600 so that a
// local wall time maps to the earlier UTC instant.
inline int64_t epochFromCivil(int64_t y, unsigned m, unsigned d,
                              unsigned hh, unsigned mm, unsigned ss,
                              int32_t tzOffsetSec) {
    return daysFromCivil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss - tzOffsetSec;
}

// Decompose a UTC epoch instant into civil date/time fields at tzOffsetSec.
inline void civilFromUtc(int64_t epochUtc, int32_t tzOffsetSec,
                         int64_t &y, unsigned &m, unsigned &d,
                         unsigned &hh, unsigned &mm, unsigned &ss) {
    int64_t local = epochUtc + tzOffsetSec;
    int64_t days  = local / 86400;
    int64_t rem   = local % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }   // floor division for pre-1970 instants
    hh = (unsigned)(rem / 3600);
    mm = (unsigned)((rem % 3600) / 60);
    ss = (unsigned)(rem % 60);
    civilFromDays(days, y, m, d);
}

// UTC epoch of local midnight (00:00:00) on the given civil date at tzOffsetSec.
inline int64_t dayStartUtc(int64_t y, unsigned m, unsigned d, int32_t tzOffsetSec) {
    return daysFromCivil(y, m, d) * 86400 - tzOffsetSec;
}

// Weekday (0=Monday..6=Sunday) of the civil date that contains epochUtc at
// tzOffsetSec. 1970-01-01 was a Thursday, so weekday == (daysSinceEpoch + 3) % 7.
inline int weekdayFromUtc(int64_t epochUtc, int32_t tzOffsetSec) {
    int64_t local = epochUtc + tzOffsetSec;
    int64_t days  = local / 86400;
    if (local % 86400 < 0) days -= 1;           // floor to the containing day
    return (int)(((days % 7) + 3 + 7) % 7);     // +3 shifts Thu(=0) to Mon-based index
}

// --- Range helpers (drive the "today / next N days / this week" views) ------
// UTC epoch of local midnight on the day containing nowUtc.
inline int64_t todayStartUtc(int64_t nowUtc, int32_t tzOffsetSec) {
    int64_t local = nowUtc + tzOffsetSec;
    int64_t days  = local / 86400;
    if (local % 86400 < 0) days -= 1;
    return days * 86400 - tzOffsetSec;
}

// UTC epoch of local midnight `days` local-days after the day containing nowUtc
// (days may be negative for past days). Used for the "next N days" window end.
inline int64_t nextNDaysUtc(int64_t nowUtc, int days, int32_t tzOffsetSec) {
    return todayStartUtc(nowUtc, tzOffsetSec) + (int64_t)days * 86400;
}

// UTC epoch of local midnight on the Monday starting the week that contains
// epochUtc (Monday-start weeks: a Sunday belongs to the week that began on the
// preceding Monday).
inline int64_t weekStartUtc(int64_t epochUtc, int32_t tzOffsetSec) {
    return todayStartUtc(epochUtc, tzOffsetSec) - (int64_t)weekdayFromUtc(epochUtc, tzOffsetSec) * 86400;
}

// Monday-start week range [outStart, outEnd) (end = following Monday midnight)
// for the week containing nowUtc.
inline void weekRangeUtc(int64_t nowUtc, int32_t tzOffsetSec,
                         int64_t &outStart, int64_t &outEnd) {
    outStart = weekStartUtc(nowUtc, tzOffsetSec);
    outEnd   = outStart + 7 * 86400;
}

// Stable insertion sort of events by ascending startUtc. Stable: events with
// equal startUtc keep their input order. Insertion sort is O(n^2) worst case
// but n <= CAL_MAX_EVENTS (small) and it needs no heap and is deterministic.
inline void sortEventsByStart(CalendarEvent *ev, int n) {
    for (int i = 1; i < n; ++i) {
        CalendarEvent key = ev[i];
        int j = i - 1;
        while (j >= 0 && ev[j].startUtc > key.startUtc) {  // strict > keeps it stable
            ev[j + 1] = ev[j];
            --j;
        }
        ev[j + 1] = key;
    }
}

} // namespace core
