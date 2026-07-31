#pragma once
// ===========================================================================
//  core/IcsParser.h  —  iCalendar (RFC 5545) VEVENT parser + recurrence expand
// ===========================================================================
//  Header-only, heap-free parser for the subset of iCalendar that Google
//  Calendar actually emits. Parses a whole .ics text buffer into a fixed
//  CalendarEvent[] and tags each event's category with the source feed index.
//
//  Supported:
//    - Line unfolding (RFC 5545 §3.1): a CRLF (or lone LF) immediately followed
//      by one space/tab is a continuation; the break + single whitespace char
//      are removed before parsing.
//    - Text escapes in SUMMARY: \\n \\, \\; \\\\ (and a stray backslash drops).
//    - DTSTART/DTEND in the three forms Google emits:
//        ...T090000Z            UTC instant
//        ;TZID=...:...T090000   local wall-clock at the FIXED tzOffsetSec
//        ;VALUE=DATE:YYYYMMDD   all-day (spans local days)
//    - VEVENT blocks only; VCALENDAR / VTIMEZONE / VTODO / everything else is
//      skipped. Malformed / oversized input is skipped gracefully (never
//      crashes, never overruns the title buffer) — returns what parsed.
//
//  Timezone / floating-time handling (design choice): Google's TZID form names
//  an Olson zone (e.g. Asia/Kuala_Lumpur) with full DST rules. We deliberately
//  do NOT carry a tz database on a microcontroller. Instead any TZID (or
//  "floating" time with no zone and no Z) is treated as local wall-clock at the
//  single fixed CAL_TZ_OFFSET_SEC. This is exact for Malaysia (no DST) and is
//  the documented Round-1 simplification; a real zone engine is out of scope.
//
//  Recurrence (bounded subset): FREQ=DAILY and FREQ=WEEKLY with optional
//  INTERVAL, COUNT, UNTIL and (weekly) BYDAY=MO,TU,... are expanded into
//  concrete single occurrences by expandAndCollect(). Any other FREQ
//  (MONTHLY/YEARLY/BYMONTH/...) is tagged Unsupported and skipped — see the
//  TODO(R2) markers.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "core/CalendarEvent.h"
#include "core/CalendarDate.h"

namespace core {

// --- Internal limits (bounded, no heap) ------------------------------------
// Largest unfolded .ics buffer we hold on the stack. A 14-day slice of a few
// Google calendars is well under this; anything longer is truncated (the parse
// still returns whatever complete VEVENTs fit).
static constexpr int ICS_BUF_MAX = 8192;
// Longest single (unfolded) content line we examine; longer lines are skipped.
static constexpr int ICS_LINE_MAX = 512;
// Hard cap on recurrence iterations so a hostile RRULE (INTERVAL=0, huge COUNT)
// can never wedge the device. Far beyond any sane 14-day window expansion.
static constexpr int ICS_EXPAND_MAX_ITER = 100000;

// --- Small string helpers (all bounded) ------------------------------------
// True iff `line` begins with `prefix` (case-sensitive, as RFC 5545 requires).
inline bool icsLineStartsWith(const char *line, const char *prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

// Parse up to `max` leading decimal digits; returns the value and advances *pp
// past the consumed digits. Stops at the first non-digit (caller validates).
inline int64_t icsParseDigits(const char **pp, int max) {
    int64_t v = 0;
    int n = 0;
    const char *p = *pp;
    while (n < max && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; ++n; }
    *pp = p;
    return v;
}

// Portable in-place tokenizer (avoids strtok_r, whose availability differs
// between Windows hosts and newlib). Returns the next token before `delim`,
// NUL-terminating it and advancing *pp past the delimiter; nullptr at end.
inline char *icsNextTok(char **pp, char delim) {
    char *p = *pp;
    if (!p || !*p) return nullptr;
    char *start = p;
    while (*p && *p != delim) ++p;
    if (*p == delim) { *p = '\0'; *pp = p + 1; }
    else             { *pp = p; }
    return start;
}

// Decode RFC 5545 text escapes from src into dst (capacity cap incl. NUL).
// Always NUL-terminates; never writes past cap. Handles \n \, \; \\ and drops
// a dangling backslash safely.
inline void icsDecodeText(const char *src, char *dst, int cap) {
    int j = 0;
    for (const char *p = src; *p && j < cap - 1; ++p) {
        if (*p == '\\') {
            char c = *++p;
            if (c == '\0') break;                 // dangling backslash at end
            if (c == 'n' || c == 'N') dst[j++] = '\n';
            else                      dst[j++] = c;   // \, \; \\ and any other -> literal
        } else {
            dst[j++] = *p;
        }
    }
    dst[j] = '\0';
}

// Parse a fixed YYYYMMDD date (and optional T HHMMSS[ Z ]) starting at p.
// On success writes y/m/d/hh/mm/ss, sets *outIsUtc when a trailing Z is seen,
// and returns true. Returns false on any malformed field (short buffer,
// non-digit, missing date).
inline bool icsParseDateTime(const char *p, int64_t &y, unsigned &m, unsigned &d,
                             unsigned &hh, unsigned &mm, unsigned &ss, bool &outIsUtc) {
    const char *s = p;
    y = icsParseDigits(&p, 4); if (p - s != 4) return false;
    s = p; m = (unsigned)icsParseDigits(&p, 2); if (p - s != 2) return false;
    s = p; d = (unsigned)icsParseDigits(&p, 2); if (p - s != 2) return false;
    hh = mm = ss = 0;
    outIsUtc = false;
    if (*p == 'T') {
        ++p;
        s = p; hh = (unsigned)icsParseDigits(&p, 2); if (p - s != 2) return false;
        s = p; mm = (unsigned)icsParseDigits(&p, 2); if (p - s != 2) return false;
        s = p; ss = (unsigned)icsParseDigits(&p, 2); if (p - s != 2) return false;
        if (*p == 'Z') outIsUtc = true;
    }
    return true;
}

// Convert an ICS date/time VALUE (with its parameter block) to UTC epoch.
// Handles VALUE=DATE (all-day), a trailing Z (UTC), and TZID/floating (local
// wall-clock at tzOffsetSec). Sets *allDay. Returns false if unparseable.
inline bool icsValueToEpoch(const char *params, const char *value, int32_t tzOffsetSec,
                            int64_t &outUtc, bool &allDay) {
    allDay = false;
    if (params && strstr(params, "VALUE=DATE")) allDay = true;  // VALUE=DATE-TIME stays timed

    int64_t y; unsigned m, d, hh, mm, ss; bool isUtc = false;
    if (!icsParseDateTime(value, y, m, d, hh, mm, ss, isUtc)) return false;

    if (allDay) {
        // All-day: local midnight of the given date (spans the local day).
        outUtc = dayStartUtc(y, m, d, tzOffsetSec);
        return true;
    }
    if (isUtc) {
        outUtc = epochFromCivil(y, m, d, hh, mm, ss, 0);          // already UTC
    } else {
        outUtc = epochFromCivil(y, m, d, hh, mm, ss, tzOffsetSec); // wall-clock -> UTC
    }
    return true;
}

// Parse an RRULE value into recurrence fields. Unrecognised FREQ (or a missing
// one) yields CalFreq::Unsupported so the expander skips the event (TODO(R2)).
inline void icsParseRrule(const char *value, int32_t tzOffsetSec,
                          CalFreq &freq, int32_t &interval, int32_t &count,
                          int64_t &untilUtc, uint8_t &bydayMask) {
    freq = CalFreq::Unsupported;
    interval = 1;
    count = 0;
    untilUtc = INT64_MIN;
    bydayMask = 0;

    char buf[ICS_LINE_MAX];
    size_t vl = strlen(value);
    if (vl >= sizeof(buf)) vl = sizeof(buf) - 1;
    memcpy(buf, value, vl);
    buf[vl] = '\0';

    bool isDaily = false, isWeekly = false;

    // Split on ';' and inspect each KEY=VALUE part.
    char *rp = buf;
    for (char *tok = icsNextTok(&rp, ';'); tok; tok = icsNextTok(&rp, ';')) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = tok;
        const char *val = eq + 1;

        if (strcmp(key, "FREQ") == 0) {
            if (strcmp(val, "DAILY") == 0)       isDaily = true;
            else if (strcmp(val, "WEEKLY") == 0) isWeekly = true;
            // MONTHLY / YEARLY / etc. leave both false -> Unsupported.
        } else if (strcmp(key, "INTERVAL") == 0) {
            const char *vp = val;
            interval = (int32_t)icsParseDigits(&vp, 6);
            if (interval < 1) interval = 1;
        } else if (strcmp(key, "COUNT") == 0) {
            const char *vp = val;
            count = (int32_t)icsParseDigits(&vp, 6);
        } else if (strcmp(key, "UNTIL") == 0) {
            int64_t y; unsigned m, d, hh, mm, ss; bool isUtc = false;
            if (icsParseDateTime(val, y, m, d, hh, mm, ss, isUtc)) {
                untilUtc = epochFromCivil(y, m, d, hh, mm, ss, isUtc ? 0 : tzOffsetSec);
            }
        } else if (strcmp(key, "BYDAY") == 0) {
            // Comma list of [NN]DAY tokens; we honour the day letters only
            // (positional ordinals like 1MO are a TODO(R2) concern).
            char bbuf[64];
            size_t bl = strlen(val);
            if (bl >= sizeof(bbuf)) bl = sizeof(bbuf) - 1;
            memcpy(bbuf, val, bl);
            bbuf[bl] = '\0';
            char *bp = bbuf;
            for (char *dt = icsNextTok(&bp, ','); dt; dt = icsNextTok(&bp, ',')) {
                size_t L = strlen(dt);
                if (L < 2) continue;
                const char *dw = dt + L - 2;   // last two chars = day code
                int wd = -1;
                if      (strcmp(dw, "MO") == 0) wd = 0;
                else if (strcmp(dw, "TU") == 0) wd = 1;
                else if (strcmp(dw, "WE") == 0) wd = 2;
                else if (strcmp(dw, "TH") == 0) wd = 3;
                else if (strcmp(dw, "FR") == 0) wd = 4;
                else if (strcmp(dw, "SA") == 0) wd = 5;
                else if (strcmp(dw, "SU") == 0) wd = 6;
                if (wd >= 0) bydayMask |= calDayBit(wd);
            }
        }
        // WKST, BYMONTHDAY, BYMONTH, BYSETPOS, ... ignored (unsupported subset).
    }

    if (isDaily)       freq = CalFreq::Daily;
    else if (isWeekly) freq = CalFreq::Weekly;
}

// Parse one .ics text buffer into out[]. Returns the number of events written
// (<= maxEvents). Each event's category is set to feedIdx. Malformed VEVENTs
// (no parseable DTSTART) are skipped; over-capacity events are dropped, not
// crashed on. A NULL/empty buffer or bad out pointer yields 0.
inline int parseIcsFeed(const char *ics, uint8_t feedIdx, CalendarEvent *out,
                        int maxEvents, int32_t tzOffsetSec) {
    if (!ics || !out || maxEvents <= 0) return 0;

    // --- Step 1: unfold into a bounded stack buffer (RFC 5545 §3.1) ---------
    char buf[ICS_BUF_MAX];
    int n = 0;
    for (const char *p = ics; *p && n < ICS_BUF_MAX - 1; ) {
        if (p[0] == '\r' && p[1] == '\n') {          // CRLF
            if (p[2] == ' ' || p[2] == '\t') { p += 3; continue; }  // fold: drop CRLF + 1 ws
            p += 2;                                   // real line break (keep '\n' logic below)
            if (n < ICS_BUF_MAX - 1) buf[n++] = '\n';
            continue;
        }
        if (p[0] == '\n') {                           // lone LF
            if (p[1] == ' ' || p[1] == '\t') { p += 2; continue; }  // fold (lenient)
            p += 1;
            if (n < ICS_BUF_MAX - 1) buf[n++] = '\n';
            continue;
        }
        buf[n++] = *p++;
    }
    buf[n] = '\0';

    // --- Step 2: walk lines, collecting VEVENT blocks ------------------------
    int written = 0;
    const char *cur = buf;
    bool inEvent = false;
    CalendarEvent e;
    calEventClear(e);
    bool haveStart = false, haveEnd = false;

    while (*cur) {
        const char *eol = strchr(cur, '\n');
        int len = eol ? (int)(eol - cur) : (int)strlen(cur);
        char line[ICS_LINE_MAX];
        if (len >= (int)sizeof(line)) len = (int)sizeof(line) - 1;  // over-long line: truncate
        memcpy(line, cur, len);
        line[len] = '\0';
        // Trim a trailing CR (in case of CRLF that survived as '\r' before '\n').
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        if (icsLineStartsWith(line, "BEGIN:VEVENT")) {
            inEvent = true;
            calEventClear(e);
            haveStart = haveEnd = false;
        } else if (icsLineStartsWith(line, "END:VEVENT")) {
            if (inEvent && haveStart) {
                if (!haveEnd) {
                    // No DTEND: default to a 1h timed event, or a single local
                    // day for all-day events (ICS DTEND is exclusive).
                    e.endUtc = e.startUtc + (e.allDay ? 86400 : 3600);
                }
                e.category = feedIdx;
                if (written < maxEvents) out[written++] = e;
                // else: over capacity -> drop silently (count reports the cap).
            }
            inEvent = false;
        } else if (inEvent) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                const char *nameParams = line;        // NAME[;PARAM=..]
                const char *value = colon + 1;
                char name[32];
                const char *semi = strchr(nameParams, ';');
                int nl = semi ? (int)(semi - nameParams) : (int)strlen(nameParams);
                if (nl >= (int)sizeof(name)) nl = (int)sizeof(name) - 1;
                memcpy(name, nameParams, nl);
                name[nl] = '\0';
                const char *params = semi ? semi + 1 : nullptr;

                if (strcmp(name, "DTSTART") == 0) {
                    int64_t st; bool ad = false;
                    if (icsValueToEpoch(params, value, tzOffsetSec, st, ad)) {
                        e.startUtc = st;
                        e.allDay = ad;
                        haveStart = true;
                    }
                } else if (strcmp(name, "DTEND") == 0) {
                    int64_t en; bool ad = false;
                    if (icsValueToEpoch(params, value, tzOffsetSec, en, ad)) {
                        e.endUtc = en;
                        haveEnd = true;
                    }
                } else if (strcmp(name, "SUMMARY") == 0) {
                    icsDecodeText(value, e.title, CAL_TITLE_MAX);  // bounded decode
                } else if (strcmp(name, "RRULE") == 0) {
                    icsParseRrule(value, tzOffsetSec, e.freq, e.interval, e.count,
                                  e.untilUtc, e.bydayMask);
                }
                // DTSTAMP, UID, CREATED, SEQUENCE, STATUS, ... ignored.
            }
        }
        // Any line outside a VEVENT (VCALENDAR/VTIMEZONE/VTODO/garbage) is skipped.

        if (!eol) break;
        cur = eol + 1;
    }
    return written;
}

// --- Recurrence expansion --------------------------------------------------
// Emit the concrete occurrences of a DAILY rule that fall within [winStart,
// winEnd). Bounded by COUNT and ICS_EXPAND_MAX_ITER. Returns count written.
inline int expandDaily(const CalendarEvent &e, int64_t winStart, int64_t winEnd,
                       CalendarEvent *out, int maxOut) {
    int written = 0;
    int64_t dur = e.endUtc - e.startUtc;
    if (dur < 0) dur = 0;
    int32_t interval = e.interval > 0 ? e.interval : 1;
    int64_t step = (int64_t)interval * 86400;
    int64_t maxCount = (e.count > 0) ? e.count : (int64_t)ICS_EXPAND_MAX_ITER;

    int64_t occ = e.startUtc;
    for (int64_t i = 0; i < maxCount && i < ICS_EXPAND_MAX_ITER; ++i, occ += step) {
        if (e.untilUtc != INT64_MIN && occ > e.untilUtc) break;  // past UNTIL -> done
        if (occ >= winEnd) break;                                // past window -> done
        if (occ + dur > winStart && written < maxOut) {          // overlaps window
            CalendarEvent o = e;
            o.startUtc = occ;
            o.endUtc = occ + dur;
            o.freq = CalFreq::None;                              // concrete single occurrence
            o.count = 0;
            o.untilUtc = INT64_MIN;
            o.bydayMask = 0;
            out[written++] = o;
        }
    }
    return written;
}

// Emit the concrete occurrences of a WEEKLY rule (BYDAY mask) within
// [winStart, winEnd). Weeks are Monday-start and anchored at DTSTART's week;
// COUNT counts matching day occurrences from the anchor week forward.
inline int expandWeekly(const CalendarEvent &e, int64_t winStart, int64_t winEnd,
                        int32_t tzOffsetSec, CalendarEvent *out, int maxOut) {
    int written = 0;
    int64_t dur = e.endUtc - e.startUtc;
    if (dur < 0) dur = 0;
    int32_t interval = e.interval > 0 ? e.interval : 1;
    uint8_t mask = e.bydayMask;
    if (mask == 0) mask = calDayBit(weekdayFromUtc(e.startUtc, tzOffsetSec));  // default: anchor weekday
    int64_t maxCount = (e.count > 0) ? e.count : (int64_t)ICS_EXPAND_MAX_ITER;

    int64_t anchorWeekMonday = weekStartUtc(e.startUtc, tzOffsetSec);
    int64_t emitted = 0;     // occurrences counted toward COUNT
    int64_t week = 0;        // week index from the anchor week
    int guard = 0;

    while (guard++ < ICS_EXPAND_MAX_ITER && emitted < maxCount) {
        int64_t weekMonday = anchorWeekMonday + week * (int64_t)interval * 7 * 86400;
        bool anyInWindow = false;
        for (int wd = 0; wd < 7 && emitted < maxCount; ++wd) {
            if (!(mask & calDayBit(wd))) continue;
            int64_t occ = weekMonday + (int64_t)wd * 86400;
            if (e.untilUtc != INT64_MIN && occ > e.untilUtc) return written;  // past UNTIL
            if (occ >= winEnd) return written;                                // past window
            ++emitted;                                                        // counts toward COUNT
            if (occ + dur > winStart) {
                anyInWindow = true;
                if (written < maxOut) {
                    CalendarEvent o = e;
                    o.startUtc = occ;
                    o.endUtc = occ + dur;
                    o.freq = CalFreq::None;
                    o.count = 0;
                    o.untilUtc = INT64_MIN;
                    o.bydayMask = 0;
                    out[written++] = o;
                }
            }
        }
        (void)anyInWindow;
        ++week;
    }
    return written;
}

// Expand parsed[] (n events) into concrete single occurrences within
// [winStartUtc, winEndUtc), written to out[] (<= maxOut), sorted by startUtc.
// Non-recurring events are passed through when they overlap the window;
// DAILY/WEEKLY rules are expanded; Unsupported frequencies are skipped
// (TODO(R2): MONTHLY/YEARLY/BYMONTH expansion). Returns count written.
inline int expandAndCollect(const CalendarEvent *parsed, int n,
                            int64_t winStartUtc, int64_t winEndUtc,
                            CalendarEvent *out, int maxOut,
                            int32_t tzOffsetSec = 0) {
    if (!parsed || !out || n <= 0 || maxOut <= 0) return 0;
    int written = 0;
    for (int i = 0; i < n && written < maxOut; ++i) {
        const CalendarEvent &e = parsed[i];
        if (e.freq == CalFreq::None) {
            // Single event: include iff it overlaps the half-open window.
            if (e.startUtc < winEndUtc && e.endUtc > winStartUtc) {
                out[written++] = e;
            }
        } else if (e.freq == CalFreq::Daily) {
            written += expandDaily(e, winStartUtc, winEndUtc, out + written, maxOut - written);
        } else if (e.freq == CalFreq::Weekly) {
            written += expandWeekly(e, winStartUtc, winEndUtc, tzOffsetSec,
                                    out + written, maxOut - written);
        } else {
            // TODO(R2): expand MONTHLY / YEARLY / BYMONTH / BYSETPOS rules.
            // Unsupported frequencies are intentionally skipped in Round 1.
        }
    }
    sortEventsByStart(out, written);
    return written;
}

} // namespace core
