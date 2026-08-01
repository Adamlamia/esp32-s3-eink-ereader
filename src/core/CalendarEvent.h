#pragma once
// ===========================================================================
//  core/CalendarEvent.h  —  fixed-size calendar event type (seam)
// ===========================================================================
//  Header-only, heap-free data type shared by the whole calendar core. Times
//  are UTC epoch seconds (int64_t); the fixed UTC offset for the wall-clock
//  conversion lives in config.h (CAL_TZ_OFFSET_SEC) and is applied by the
//  date/parser seams, never stored per-event.
//
//  Design note (config coupling): unlike the generic seams (ButtonClassify,
//  Paginator) this type is calendar-app-specific, so its title buffer is sized
//  by the project constant CAL_TITLE_MAX from config.h. config.h is a pure
//  macro header (no Arduino/HAL), so the seam stays host-testable; the native
//  test env exposes it via -Isrc.
//
//  UID capture (TODO·R1): the struct also carries the event's ICS UID, bounded
//  by CAL_UID_MAX. The parser previously ignored UID; the Todo app needs it as
//  a STABLE identity for device-local done-state (Google keeps the UID fixed
//  across phone-side edits and re-syncs). Capturing it here is additive: the
//  calendar app never reads the field, and recurrence expansion copies it into
//  each concrete occurrence automatically (struct copy).
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include "config.h"   // CAL_TITLE_MAX / CAL_UID_MAX (pure macros, no HAL)

namespace core {

// Recurrence frequency tag carried alongside an event (parsed from RRULE).
enum class CalFreq : uint8_t {
    None = 0,     // single, non-recurring event
    Daily,        // RRULE:FREQ=DAILY
    Weekly,       // RRULE:FREQ=WEEKLY
    Unsupported,  // MONTHLY / YEARLY / etc. — parsed but skipped (see TODO(R2))
};

// One calendar event. Plain-old-data, fixed size, safe to copy by value and to
// hold in a static array. All times are UTC epoch seconds.
struct CalendarEvent {
    int64_t startUtc;                 // inclusive start, UTC epoch seconds
    int64_t endUtc;                   // exclusive end, UTC epoch seconds
    char    title[CAL_TITLE_MAX];     // NUL-terminated, ICS escapes already decoded
    char    uid[CAL_UID_MAX];         // NUL-terminated ICS UID ("" when the feed
                                      // omits it); stable identity for Todo done-state
    uint8_t category;                 // index of the source feed (Google calendar)
    bool    allDay;                   // true for VALUE=DATE events (spans local days)

    // --- Recurrence (bounded subset; see IcsParser.h) ---
    CalFreq freq;                     // None for a single event
    int32_t interval;                 // RRULE INTERVAL (>=1; 0/1 == every step)
    int32_t count;                    // RRULE COUNT (<=0 == unbounded)
    int64_t untilUtc;                 // RRULE UNTIL as UTC epoch (INT64_MIN == none)
    uint8_t bydayMask;                // weekly BYDAY bitmask, bit i == weekday i (0=Mon..6=Sun)
};

// Weekday bitmask helpers for bydayMask (bit index matches the weekday
// convention used across the calendar core: 0=Monday ... 6=Sunday).
inline constexpr uint8_t calDayBit(int wd) { return (uint8_t)(1u << wd); }

// Reset an event to safe defaults so partially-parsed fields are never garbage.
inline void calEventClear(CalendarEvent &e) {
    e.startUtc  = 0;
    e.endUtc    = 0;
    e.title[0]  = '\0';
    e.uid[0]    = '\0';
    e.category  = 0;
    e.allDay    = false;
    e.freq      = CalFreq::None;
    e.interval  = 0;
    e.count     = 0;
    e.untilUtc  = INT64_MIN;
    e.bydayMask = 0;
}

// True iff the event carries an actionable recurrence rule (Daily/Weekly).
// Unsupported frequencies are reported as non-recurring here: the parser tags
// them CalFreq::Unsupported and the expander skips them (TODO(R2)).
inline bool calIsRecurring(const CalendarEvent &e) {
    return e.freq == CalFreq::Daily || e.freq == CalFreq::Weekly;
}

} // namespace core
