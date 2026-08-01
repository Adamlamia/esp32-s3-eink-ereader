// ===========================================================================
//  test_calendar  —  Unit tests for the calendar core seams (Round 1)
// ===========================================================================
//  Hardware-free tests for the pure-logic calendar seams:
//    - core/CalendarDate.h : pure-integer civil date math, weekday, ranges, sort
//    - core/IcsParser.h    : RFC 5545 VEVENT parsing + bounded recurrence expand
//  No <ctime>, no heap, no Arduino: results are deterministic on any host.
//
//  Anchor facts (verified independently against Python datetime):
//    2026-08-03 is a MONDAY.  days_from_civil(2026,8,3) = 20668.
//    2026-08-03 00:00:00 UTC = 1785715200 (= 20668 * 86400).
//    CAL_TZ_OFFSET_SEC = +8h (Malaysia, no DST) => local midnight 2026-08-03
//    = 1785715200 - 28800 = 1785686400 (= 2026-08-02 16:00 UTC).
// ===========================================================================
#include <unity.h>
#include <cstring>
#include "config.h"
#include "core/CalendarEvent.h"
#include "core/CalendarDate.h"
#include "core/IcsParser.h"

using namespace core;

static const int32_t TZ = CAL_TZ_OFFSET_SEC;   // +8h
static const int64_t MON_AUG3_UTC = INT64_C(1785715200);  // 2026-08-03 00:00:00 UTC
static const int64_t MON_AUG3_LOCAL_MIDNIGHT = INT64_C(1785686400);  // = 2026-08-02 16:00 UTC

void setUp(void) {}
void tearDown(void) {}

// ===========================================================================
//  CalendarDate — civil/epoch anchors and round-trips
// ===========================================================================
void test_date_anchor_monday(void) {
    // 2026-08-03 is a Monday (weekday 0 in our Mon=0 convention).
    TEST_ASSERT_EQUAL_INT(0, weekdayFromUtc(MON_AUG3_UTC, 0));
    TEST_ASSERT_EQUAL_INT64(INT64_C(20668), daysFromCivil(2026, 8, 3));
}

void test_date_epoch_roundtrip_utc(void) {
    int64_t e = epochFromCivil(2026, 8, 3, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC, e);
    int64_t y; unsigned m, d, hh, mm, ss;
    civilFromUtc(e, 0, y, m, d, hh, mm, ss);
    TEST_ASSERT_EQUAL_INT64(2026, y);
    TEST_ASSERT_EQUAL_UINT(8, m);
    TEST_ASSERT_EQUAL_UINT(3, d);
    TEST_ASSERT_EQUAL_UINT(0, hh);
}

void test_date_epoch_roundtrip_offset(void) {
    // Local 2026-08-03 09:00 at +8h == 01:00 UTC.
    int64_t e = epochFromCivil(2026, 8, 3, 9, 0, 0, TZ);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 3600, e);
    int64_t y; unsigned m, d, hh, mm, ss;
    civilFromUtc(e, TZ, y, m, d, hh, mm, ss);
    TEST_ASSERT_EQUAL_UINT(9, hh);   // back to local wall-clock
    TEST_ASSERT_EQUAL_UINT(3, d);
}

void test_date_leap_year(void) {
    // 2024 is a leap year: Feb 29 exists, weekday Thursday (3 in Mon=0).
    int64_t e = epochFromCivil(2024, 2, 29, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT(3, weekdayFromUtc(e, 0));
    int64_t y; unsigned m, d, hh, mm, ss;
    civilFromUtc(e + 86400, 0, y, m, d, hh, mm, ss);   // +1 day -> Mar 1
    TEST_ASSERT_EQUAL_UINT(3, m);
    TEST_ASSERT_EQUAL_UINT(1, d);
}

void test_date_civil_january_february_year(void) {
    // REGRESSION (R4): civilFromDays must restore the year for Jan/Feb dates.
    // Hinnant's algorithm counts Jan/Feb as months 13/14 of the PREVIOUS
    // March-based year (daysFromCivil does y -= m <= 2); the inverse must add
    // it back. It did not, so every Jan/Feb instant rendered with year-1
    // (2027-01-15 showed as 2026-01-15) while Mar..Dec were correct. None of
    // the older anchors exercised Jan/Feb (Aug + Mar only), hiding the bug.
    int64_t y; unsigned m, d;
    civilFromDays(0, y, m, d);                    // day 0 == 1970-01-01
    TEST_ASSERT_EQUAL_INT64(1970, y);
    TEST_ASSERT_EQUAL_UINT(1, m);
    TEST_ASSERT_EQUAL_UINT(1, d);

    // Civil round-trips for a January and a leap-day February instant.
    int64_t yy; unsigned mm, dd, hh, mi, ss;
    int64_t jan = epochFromCivil(2027, 1, 15, 3, 0, 0, 0);
    civilFromUtc(jan, 0, yy, mm, dd, hh, mi, ss);
    TEST_ASSERT_EQUAL_INT64(2027, yy);
    TEST_ASSERT_EQUAL_UINT(1, mm);
    TEST_ASSERT_EQUAL_UINT(15, dd);
    TEST_ASSERT_EQUAL_UINT(3, hh);

    int64_t feb = epochFromCivil(2024, 2, 29, 23, 59, 59, TZ);
    civilFromUtc(feb, TZ, yy, mm, dd, hh, mi, ss);
    TEST_ASSERT_EQUAL_INT64(2024, yy);            // leap day keeps its year
    TEST_ASSERT_EQUAL_UINT(2, mm);
    TEST_ASSERT_EQUAL_UINT(29, dd);
    TEST_ASSERT_EQUAL_UINT(23, hh);

    // Mar..Dec unchanged (guard against over-correcting the fix).
    int64_t aug = epochFromCivil(2026, 8, 3, 0, 0, 0, 0);
    civilFromUtc(aug, 0, yy, mm, dd, hh, mi, ss);
    TEST_ASSERT_EQUAL_INT64(2026, yy);
    TEST_ASSERT_EQUAL_UINT(8, mm);
}

void test_date_weekday_sequence(void) {
    // Mon..Sun from the anchor week.
    for (int i = 0; i < 7; ++i) {
        TEST_ASSERT_EQUAL_INT(i, weekdayFromUtc(MON_AUG3_UTC + (int64_t)i * 86400, 0));
    }
}

void test_date_daystart_offset(void) {
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_LOCAL_MIDNIGHT, dayStartUtc(2026, 8, 3, TZ));
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC, dayStartUtc(2026, 8, 3, 0));
}

// ===========================================================================
//  CalendarDate — today / next-N-days / Monday-start week ranges
// ===========================================================================
void test_today_start(void) {
    // 2026-08-03 15:30 UTC is 23:30 local (still Aug 3 locally).
    int64_t now = MON_AUG3_UTC + 15 * 3600 + 30 * 60;
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_LOCAL_MIDNIGHT, todayStartUtc(now, TZ));
}

void test_next_n_days_boundaries(void) {
    int64_t base = MON_AUG3_LOCAL_MIDNIGHT;
    TEST_ASSERT_EQUAL_INT64(base + 86400, nextNDaysUtc(base, 1, TZ));
    TEST_ASSERT_EQUAL_INT64(base + 3 * 86400, nextNDaysUtc(base + 1000, 3, TZ));
}

void test_event_2359_vs_0001_boundary(void) {
    // An event ending at 23:59 local Aug 3 must NOT appear in the Aug 4 window;
    // one at 00:01 local Aug 4 MUST.
    int64_t aug4 = MON_AUG3_LOCAL_MIDNIGHT + 86400;
    int64_t winStart = aug4, winEnd = aug4 + 86400;

    CalendarEvent evs[2];
    calEventClear(evs[0]);
    evs[0].startUtc = aug4 - 120;  evs[0].endUtc = aug4 - 60;   // 23:58..23:59 Aug 3
    strcpy(evs[0].title, "late");
    calEventClear(evs[1]);
    evs[1].startUtc = aug4 + 60;   evs[1].endUtc = aug4 + 120;  // 00:01..00:02 Aug 4
    strcpy(evs[1].title, "early");

    CalendarEvent out[8];
    int n = expandAndCollect(evs, 2, winStart, winEnd, out, 8, TZ);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("early", out[0].title);
}

void test_week_start_sunday_belongs_to_previous_week(void) {
    // Sunday 2026-08-09 belongs to the week that started Monday 2026-08-03.
    int64_t sunday = MON_AUG3_UTC + 6 * 86400;
    TEST_ASSERT_EQUAL_INT(6, weekdayFromUtc(sunday, 0));
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC, weekStartUtc(sunday, 0));
    // Monday itself anchors its own week.
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC, weekStartUtc(MON_AUG3_UTC, 0));
}

void test_week_range_monday_start(void) {
    int64_t s, e;
    weekRangeUtc(MON_AUG3_UTC + 3 * 86400, 0, s, e);  // a Thursday
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC, s);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 7 * 86400, e);
}

// ===========================================================================
//  CalendarDate — stable sort by startUtc
// ===========================================================================
void test_sort_by_start_stable(void) {
    CalendarEvent evs[4];
    for (int i = 0; i < 4; ++i) calEventClear(evs[i]);
    evs[0].startUtc = 300; strcpy(evs[0].title, "c");
    evs[1].startUtc = 100; strcpy(evs[1].title, "a1");
    evs[2].startUtc = 100; strcpy(evs[2].title, "a2");   // tie with a1
    evs[3].startUtc = 200; strcpy(evs[3].title, "b");
    sortEventsByStart(evs, 4);
    TEST_ASSERT_EQUAL_STRING("a1", evs[0].title);   // stable: a1 before a2
    TEST_ASSERT_EQUAL_STRING("a2", evs[1].title);
    TEST_ASSERT_EQUAL_STRING("b",  evs[2].title);
    TEST_ASSERT_EQUAL_STRING("c",  evs[3].title);
}

// ===========================================================================
//  IcsParser — realistic multi-event Google fixture
// ===========================================================================
static const char *GOOGLE_FIXTURE =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Google Inc//Google Calendar 70.9054//EN\r\n"
    "BEGIN:VTIMEZONE\r\n"
    "TZID:Asia/Kuala_Lumpur\r\n"
    "BEGIN:STANDARD\r\n"
    "TZOFFSETFROM:+0800\r\n"
    "TZOFFSETTO:+0800\r\n"
    "TZNAME:+08\r\n"
    "END:STANDARD\r\n"
    "END:VTIMEZONE\r\n"
    "BEGIN:VEVENT\r\n"
    "DTSTART:20260803T010000Z\r\n"
    "DTEND:20260803T020000Z\r\n"
    "SUMMARY:UTC Event\r\n"
    "UID:utc-1@google.com\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Asia/Kuala_Lumpur:20260803T090000\r\n"
    "DTEND;TZID=Asia/Kuala_Lumpur:20260803T100000\r\n"
    "SUMMARY:Local TZID Event\r\n"
    "UID:tzid-1@google.com\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\n"
    "DTSTART;VALUE=DATE:20260804\r\n"
    "DTEND;VALUE=DATE:20260805\r\n"
    "SUMMARY:All Day Event\r\n"
    "UID:allday-1@google.com\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\n"
    "DTSTART:20260805T030000Z\r\n"
    "DTEND:20260805T040000Z\r\n"
    "SUMMARY:Team\\; Sync\\, Q3\\nNotes\r\n"
    "UID:esc-1@google.com\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VTODO\r\n"
    "SUMMARY:Should be ignored\r\n"
    "END:VTODO\r\n"
    "END:VCALENDAR\r\n";

void test_ics_parse_multi_event_fixture(void) {
    CalendarEvent out[CAL_MAX_EVENTS];
    int n = parseIcsFeed(GOOGLE_FIXTURE, 2, out, CAL_MAX_EVENTS, TZ);
    TEST_ASSERT_EQUAL_INT(4, n);   // VTIMEZONE + VTODO are skipped

    // UTC event
    TEST_ASSERT_EQUAL_STRING("UTC Event", out[0].title);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 3600, out[0].startUtc);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 7200, out[0].endUtc);
    TEST_ASSERT_FALSE(out[0].allDay);
    TEST_ASSERT_EQUAL_UINT8(2, out[0].category);   // tagged with feedIdx

    // TZID event: 09:00 local == 01:00 UTC
    TEST_ASSERT_EQUAL_STRING("Local TZID Event", out[1].title);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 3600, out[1].startUtc);
    TEST_ASSERT_FALSE(out[1].allDay);

    // All-day event: spans local Aug 4
    TEST_ASSERT_EQUAL_STRING("All Day Event", out[2].title);
    TEST_ASSERT_TRUE(out[2].allDay);
    TEST_ASSERT_EQUAL_INT64(dayStartUtc(2026, 8, 4, TZ), out[2].startUtc);
    TEST_ASSERT_EQUAL_INT64(dayStartUtc(2026, 8, 5, TZ), out[2].endUtc);

    // Escaped title: \; \, \n decoded
    TEST_ASSERT_EQUAL_STRING("Team; Sync, Q3\nNotes", out[3].title);
}

void test_ics_folded_long_title(void) {
    const char *ics =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T010000Z\r\n"
        "SUMMARY:This is a very long sum\r\n"
        " mary that was folded across lines\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    CalendarEvent out[4];
    int n = parseIcsFeed(ics, 0, out, 4, TZ);
    TEST_ASSERT_EQUAL_INT(1, n);
    // RFC 5545 §3.1: CRLF + single space removed -> words rejoin directly.
    TEST_ASSERT_EQUAL_STRING("This is a very long summary that was folded across lines", out[0].title);
}

void test_ics_title_truncated_to_capacity(void) {
    // 80-char SUMMARY must be clipped to CAL_TITLE_MAX-1 chars + NUL, no overrun.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T010000Z\r\n"
        "SUMMARY:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\r\n"
        "END:VEVENT\r\n";
    CalendarEvent out[2];
    int n = parseIcsFeed(ics, 0, out, 2, TZ);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(CAL_TITLE_MAX - 1, (int)strlen(out[0].title));
}

// ===========================================================================
//  IcsParser — malformed-input resilience
// ===========================================================================
void test_ics_value_date_time_stays_timed(void) {
    // REGRESSION (R4): an explicit VALUE=DATE-TIME parameter must NOT be
    // treated as VALUE=DATE (all-day). The old substring match on
    // "VALUE=DATE" also matched "VALUE=DATE-TIME" and shifted the event's
    // start to local midnight with allDay=true.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE-TIME:20260803T010000Z\r\n"
        "DTEND;VALUE=DATE-TIME:20260803T020000Z\r\n"
        "SUMMARY:Explicit timed\r\n"
        "END:VEVENT\r\n";
    CalendarEvent out[2];
    int n = parseIcsFeed(ics, 0, out, 2, TZ);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_FALSE(out[0].allDay);                          // timed, not all-day
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 3600, out[0].startUtc);   // 01:00Z kept
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 7200, out[0].endUtc);

    // VALUE=DATE followed by another parameter (';' terminator) is all-day.
    const char *ics2 =
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE;X-FOO=BAR:20260804\r\n"
        "SUMMARY:All day with extra param\r\n"
        "END:VEVENT\r\n";
    CalendarEvent out2[2];
    int n2 = parseIcsFeed(ics2, 0, out2, 2, TZ);
    TEST_ASSERT_EQUAL_INT(1, n2);
    TEST_ASSERT_TRUE(out2[0].allDay);
    TEST_ASSERT_EQUAL_INT64(dayStartUtc(2026, 8, 4, TZ), out2[0].startUtc);
}

void test_ics_null_and_garbage(void) {
    CalendarEvent out[4];
    TEST_ASSERT_EQUAL_INT(0, parseIcsFeed(nullptr, 0, out, 4, TZ));
    TEST_ASSERT_EQUAL_INT(0, parseIcsFeed("total garbage no structure", 0, out, 4, TZ));
    TEST_ASSERT_EQUAL_INT(0, parseIcsFeed("", 0, out, 4, TZ));
}

void test_ics_truncated_buffer(void) {
    // Cut off mid-DTSTART: no complete event -> 0, no crash.
    const char *ics =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART:2026";
    CalendarEvent out[4];
    TEST_ASSERT_EQUAL_INT(0, parseIcsFeed(ics, 0, out, 4, TZ));
}

void test_ics_unterminated_vevent(void) {
    // BEGIN without END:VEVENT -> discarded.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T010000Z\r\n"
        "SUMMARY:Never finished\r\n";
    CalendarEvent out[4];
    TEST_ASSERT_EQUAL_INT(0, parseIcsFeed(ics, 0, out, 4, TZ));
}

void test_ics_over_capacity_is_capped_not_crashed(void) {
    // NEGATIVE TEST: 3 well-formed events but room for only 2 -> returns 2.
    // The guard must cap loudly (return the cap), never overflow out[].
    const char *ics =
        "BEGIN:VEVENT\r\nDTSTART:20260803T010000Z\r\nSUMMARY:One\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nDTSTART:20260804T010000Z\r\nSUMMARY:Two\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nDTSTART:20260805T010000Z\r\nSUMMARY:Three\r\nEND:VEVENT\r\n";
    CalendarEvent out[2];
    int n = parseIcsFeed(ics, 0, out, 2, TZ);
    TEST_ASSERT_EQUAL_INT(2, n);                       // capped at capacity
    TEST_ASSERT_EQUAL_STRING("One", out[0].title);
    TEST_ASSERT_EQUAL_STRING("Two", out[1].title);     // third dropped, not crashed
}

void test_ics_zero_capacity_guard(void) {
    CalendarEvent out[1];
    TEST_ASSERT_EQUAL_INT(0, parseIcsFeed(GOOGLE_FIXTURE, 0, out, 0, TZ));
}

// ===========================================================================
//  IcsParser — recurrence parsing + expansion
// ===========================================================================
void test_rrule_daily_across_window(void) {
    // Daily 09:00 local standup starting Mon Aug 3, no COUNT/UNTIL.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART;TZID=Asia/Kuala_Lumpur:20260803T090000\r\n"
        "DTEND;TZID=Asia/Kuala_Lumpur:20260803T093000\r\n"
        "RRULE:FREQ=DAILY\r\n"
        "SUMMARY:Standup\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[4];
    int n = parseIcsFeed(ics, 0, parsed, 4, TZ);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_TRUE(calIsRecurring(parsed[0]));
    TEST_ASSERT_EQUAL(CalFreq::Daily, parsed[0].freq);

    int64_t winStart = MON_AUG3_LOCAL_MIDNIGHT;
    int64_t winEnd   = winStart + 5 * 86400;   // 5 local days
    CalendarEvent occ[32];
    int c = expandAndCollect(parsed, n, winStart, winEnd, occ, 32, TZ);
    TEST_ASSERT_EQUAL_INT(5, c);               // one per day, Mon..Fri
    for (int i = 1; i < c; ++i) {
        TEST_ASSERT_EQUAL_INT64(occ[i - 1].startUtc + 86400, occ[i].startUtc);
    }
    TEST_ASSERT_EQUAL(CalFreq::None, occ[0].freq);   // occurrences are concrete singles
}

void test_rrule_daily_count_bounds(void) {
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T010000Z\r\n"
        "DTEND:20260803T020000Z\r\n"
        "RRULE:FREQ=DAILY;COUNT=3\r\n"
        "SUMMARY:Limited\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    parseIcsFeed(ics, 0, parsed, 2, 0);
    CalendarEvent occ[16];
    int c = expandAndCollect(parsed, 1, MON_AUG3_UTC, MON_AUG3_UTC + 30 * 86400, occ, 16, 0);
    TEST_ASSERT_EQUAL_INT(3, c);               // COUNT caps total occurrences
}

void test_rrule_daily_until_bounds(void) {
    // UNTIL is inclusive in RFC 5545: an occurrence exactly at UNTIL is kept.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T010000Z\r\n"
        "DTEND:20260803T020000Z\r\n"
        "RRULE:FREQ=DAILY;UNTIL=20260805T010000Z\r\n"
        "SUMMARY:Until\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    parseIcsFeed(ics, 0, parsed, 2, 0);
    CalendarEvent occ[16];
    int c = expandAndCollect(parsed, 1, MON_AUG3_UTC, MON_AUG3_UTC + 30 * 86400, occ, 16, 0);
    TEST_ASSERT_EQUAL_INT(3, c);               // Aug 3, 4, 5 (5th == UNTIL, inclusive)
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 2 * 86400 + 3600, occ[2].startUtc);  // 01:00 UTC
}

void test_rrule_weekly_byday_mo_we(void) {
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T020000Z\r\n"
        "DTEND:20260803T030000Z\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO,WE\r\n"
        "SUMMARY:Gym\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    parseIcsFeed(ics, 0, parsed, 2, 0);
    TEST_ASSERT_EQUAL(CalFreq::Weekly, parsed[0].freq);
    TEST_ASSERT_EQUAL_UINT8(calDayBit(0) | calDayBit(2), parsed[0].bydayMask);

    CalendarEvent occ[16];
    int c = expandAndCollect(parsed, 1, MON_AUG3_UTC, MON_AUG3_UTC + 10 * 86400, occ, 16, 0);
    // Mon Aug 3, Wed Aug 5, Mon Aug 10, Wed Aug 12 — each at DTSTART's 02:00Z
    // (R4: occurrences keep DTSTART's time-of-day, not local midnight).
    TEST_ASSERT_EQUAL_INT(4, c);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 0 * 86400 + 7200, occ[0].startUtc);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 2 * 86400 + 7200, occ[1].startUtc);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 7 * 86400 + 7200, occ[2].startUtc);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 9 * 86400 + 7200, occ[3].startUtc);
}

void test_rrule_weekly_count(void) {
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T020000Z\r\n"
        "DTEND:20260803T030000Z\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO,WE;COUNT=3\r\n"
        "SUMMARY:Counted\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    parseIcsFeed(ics, 0, parsed, 2, 0);
    CalendarEvent occ[16];
    int c = expandAndCollect(parsed, 1, MON_AUG3_UTC, MON_AUG3_UTC + 60 * 86400, occ, 16, 0);
    TEST_ASSERT_EQUAL_INT(3, c);               // Mon3, Wed5, Mon10
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 7 * 86400 + 7200, occ[2].startUtc);  // 02:00Z
}

void test_rrule_weekly_interval(void) {
    // Every 2nd week on MO,WE.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T020000Z\r\n"
        "DTEND:20260803T030000Z\r\n"
        "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE\r\n"
        "SUMMARY:Biweekly\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    parseIcsFeed(ics, 0, parsed, 2, 0);
    CalendarEvent occ[16];
    int c = expandAndCollect(parsed, 1, MON_AUG3_UTC, MON_AUG3_UTC + 21 * 86400, occ, 16, 0);
    // Week0: Aug 3, 5 ; Week2: Aug 17, 19 (week1 skipped) — all at 02:00Z
    TEST_ASSERT_EQUAL_INT(4, c);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 14 * 86400 + 7200, occ[2].startUtc);
}

void test_rrule_weekly_keeps_dtstart_time_of_day(void) {
    // REGRESSION (R4): a weekly 09:00 LOCAL event must recur at 09:00 local,
    // not at local midnight (the old anchor). DTSTART 09:00 local (01:00Z)
    // Mon Aug 3, BYDAY=MO, TZ = UTC+8.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART;TZID=Asia/Kuala_Lumpur:20260803T090000\r\n"
        "DTEND;TZID=Asia/Kuala_Lumpur:20260803T100000\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO\r\n"
        "SUMMARY:Weekly review\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    int n = parseIcsFeed(ics, 0, parsed, 2, TZ);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 3600, parsed[0].startUtc);  // 01:00Z

    CalendarEvent occ[8];
    int c = expandAndCollect(parsed, n, MON_AUG3_LOCAL_MIDNIGHT,
                             MON_AUG3_LOCAL_MIDNIGHT + 21 * 86400, occ, 8, TZ);
    TEST_ASSERT_EQUAL_INT(3, c);                 // Aug 3, 10, 17
    for (int i = 0; i < c; ++i) {
        // Every occurrence at local 09:00 = its day's local midnight + 9 h.
        int64_t day0 = core::todayStartUtc(occ[i].startUtc, TZ);
        TEST_ASSERT_EQUAL_INT64(9 * 3600, occ[i].startUtc - day0);
        TEST_ASSERT_EQUAL_INT64(3600, occ[i].endUtc - occ[i].startUtc);  // 1 h kept
    }
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 3600, occ[0].startUtc);      // == DTSTART
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 7 * 86400 + 3600, occ[1].startUtc);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 14 * 86400 + 3600, occ[2].startUtc);
}

void test_rrule_weekly_no_phantom_before_dtstart(void) {
    // REGRESSION (R4): BYDAY days in the anchor week that fall BEFORE DTSTART
    // are not occurrences (RFC 5545) and must not be emitted nor counted
    // toward COUNT. DTSTART = Wed Aug 5 00:00Z with BYDAY=MO,WE;COUNT=3:
    // Mon Aug 3 is a phantom; the real series is Wed5, Mon10, Wed12. Before
    // the fix the phantom displaced a real occurrence (Wed5, Mon10 only +
    // the phantom).
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260805T000000Z\r\n"
        "DTEND:20260805T010000Z\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO,WE;COUNT=3\r\n"
        "SUMMARY:Wed start\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    int n = parseIcsFeed(ics, 0, parsed, 2, 0);
    TEST_ASSERT_EQUAL_INT(1, n);
    CalendarEvent occ[8];
    int c = expandAndCollect(parsed, n, MON_AUG3_UTC, MON_AUG3_UTC + 30 * 86400, occ, 8, 0);
    TEST_ASSERT_EQUAL_INT(3, c);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 2 * 86400, occ[0].startUtc);   // Wed Aug 5
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 7 * 86400, occ[1].startUtc);   // Mon Aug 10
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_UTC + 9 * 86400, occ[2].startUtc);   // Wed Aug 12
}

void test_rrule_monthly_unsupported_skipped(void) {
    // MONTHLY is out of the Round-1 subset: parsed as Unsupported, skipped by
    // the expander (leaves a TODO(R2) marker in the seam).
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260803T010000Z\r\n"
        "DTEND:20260803T020000Z\r\n"
        "RRULE:FREQ=MONTHLY\r\n"
        "SUMMARY:Rent\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    int n = parseIcsFeed(ics, 0, parsed, 2, 0);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL(CalFreq::Unsupported, parsed[0].freq);
    TEST_ASSERT_FALSE(calIsRecurring(parsed[0]));
    CalendarEvent occ[8];
    int c = expandAndCollect(parsed, 1, MON_AUG3_UTC, MON_AUG3_UTC + 400 * 86400, occ, 8, 0);
    TEST_ASSERT_EQUAL_INT(0, c);               // skipped, not expanded
}

void test_expand_passthrough_and_sort(void) {
    // Non-recurring events overlap-filtered and sorted by startUtc.
    CalendarEvent evs[3];
    for (int i = 0; i < 3; ++i) calEventClear(evs[i]);
    evs[0].startUtc = MON_AUG3_UTC + 5000; evs[0].endUtc = MON_AUG3_UTC + 6000; strcpy(evs[0].title, "late");
    evs[1].startUtc = MON_AUG3_UTC + 100;  evs[1].endUtc = MON_AUG3_UTC + 200;  strcpy(evs[1].title, "early");
    evs[2].startUtc = MON_AUG3_UTC - 100000; evs[2].endUtc = MON_AUG3_UTC - 90000; strcpy(evs[2].title, "outside");
    CalendarEvent occ[8];
    int c = expandAndCollect(evs, 3, MON_AUG3_UTC, MON_AUG3_UTC + 86400, occ, 8, 0);
    TEST_ASSERT_EQUAL_INT(2, c);               // "outside" filtered out
    TEST_ASSERT_EQUAL_STRING("early", occ[0].title);
    TEST_ASSERT_EQUAL_STRING("late",  occ[1].title);
}

void test_expand_all_day_recurring_daily(void) {
    // An all-day event that recurs daily still steps by whole local days.
    const char *ics =
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20260803\r\n"
        "DTEND;VALUE=DATE:20260804\r\n"
        "RRULE:FREQ=DAILY;COUNT=3\r\n"
        "SUMMARY:Holiday\r\n"
        "END:VEVENT\r\n";
    CalendarEvent parsed[2];
    parseIcsFeed(ics, 0, parsed, 2, TZ);
    TEST_ASSERT_TRUE(parsed[0].allDay);
    CalendarEvent occ[8];
    int c = expandAndCollect(parsed, 1, MON_AUG3_LOCAL_MIDNIGHT,
                             MON_AUG3_LOCAL_MIDNIGHT + 7 * 86400, occ, 8, TZ);
    TEST_ASSERT_EQUAL_INT(3, c);
    TEST_ASSERT_EQUAL_INT64(MON_AUG3_LOCAL_MIDNIGHT, occ[0].startUtc);
    TEST_ASSERT_EQUAL_INT64(86400, occ[0].endUtc - occ[0].startUtc);
}

// ===========================================================================
//  Runner
// ===========================================================================
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // CalendarDate — anchors / round-trips
    RUN_TEST(test_date_anchor_monday);
    RUN_TEST(test_date_epoch_roundtrip_utc);
    RUN_TEST(test_date_epoch_roundtrip_offset);
    RUN_TEST(test_date_leap_year);
    RUN_TEST(test_date_civil_january_february_year);
    RUN_TEST(test_date_weekday_sequence);
    RUN_TEST(test_date_daystart_offset);

    // CalendarDate — ranges
    RUN_TEST(test_today_start);
    RUN_TEST(test_next_n_days_boundaries);
    RUN_TEST(test_event_2359_vs_0001_boundary);
    RUN_TEST(test_week_start_sunday_belongs_to_previous_week);
    RUN_TEST(test_week_range_monday_start);

    // CalendarDate — sort
    RUN_TEST(test_sort_by_start_stable);

    // IcsParser — happy path
    RUN_TEST(test_ics_parse_multi_event_fixture);
    RUN_TEST(test_ics_folded_long_title);
    RUN_TEST(test_ics_title_truncated_to_capacity);

    // IcsParser — malformed resilience (incl. negative guard tests)
    RUN_TEST(test_ics_null_and_garbage);
    RUN_TEST(test_ics_truncated_buffer);
    RUN_TEST(test_ics_unterminated_vevent);
    RUN_TEST(test_ics_over_capacity_is_capped_not_crashed);
    RUN_TEST(test_ics_zero_capacity_guard);
    RUN_TEST(test_ics_value_date_time_stays_timed);

    // IcsParser — recurrence
    RUN_TEST(test_rrule_daily_across_window);
    RUN_TEST(test_rrule_daily_count_bounds);
    RUN_TEST(test_rrule_daily_until_bounds);
    RUN_TEST(test_rrule_weekly_byday_mo_we);
    RUN_TEST(test_rrule_weekly_count);
    RUN_TEST(test_rrule_weekly_interval);
    RUN_TEST(test_rrule_weekly_keeps_dtstart_time_of_day);
    RUN_TEST(test_rrule_weekly_no_phantom_before_dtstart);
    RUN_TEST(test_rrule_monthly_unsupported_skipped);
    RUN_TEST(test_expand_passthrough_and_sort);
    RUN_TEST(test_expand_all_day_recurring_daily);

    return UNITY_END();
}
