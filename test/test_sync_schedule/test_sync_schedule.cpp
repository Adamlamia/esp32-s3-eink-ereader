// ===========================================================================
//  test_sync_schedule  —  Unit tests for the SyncSchedule seam (Round 3)
// ===========================================================================
//  Hardware-free tests for the pure scheduling decisions in
//  src/core/SyncSchedule.h (namespace core). Deterministic: every case uses
//  fixed epoch inputs, no wall clock, no <ctime>.
//
//  Reference instants (all TRUE UTC epoch seconds; local = UTC + 8 h):
//    2026-08-03 is a MONDAY. 2026-08-03T00:00:00Z = 1785715200 (= 08:00 local).
//    local midnight  Aug 3 = 1785686400   (= 2026-08-02T16:00:00Z)
//    05:00 local     Aug 3 = 1785704400
//    05:59:59 local  Aug 3 = 1785707999
//    06:00 local     Aug 3 = 1785708000   (the daily sync boundary, syncHour=6)
//    06:00:01 local  Aug 3 = 1785708001
//    07:00 local     Aug 3 = 1785711600
//    08:00 local     Aug 3 = 1785715200
//    06:00 local     Aug 2 = 1785621600   (yesterday's boundary)
//    06:00 local     Aug 4 = 1785794400   (tomorrow's boundary)
// ===========================================================================
#include <unity.h>
#include <cstdint>
#include "core/SyncSchedule.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// --- Fixed fixtures ---------------------------------------------------------
static const int32_t TZ      = 8 * 3600;   // CAL_TZ_OFFSET_SEC (Malaysia, UTC+8)
static const int     HOUR    = 6;          // CAL_SYNC_HOUR
static const int64_t STALE   = 20 * 3600;  // CAL_SYNC_STALE_SEC (20 h)

// 2026-08-03 (Monday) instants, TRUE UTC seconds:
static const int64_t AUG3_MIDNIGHT_LOCAL = INT64_C(1785686400);  // 00:00 local
static const int64_t AUG3_0500           = INT64_C(1785704400);
static const int64_t AUG3_0559_59        = INT64_C(1785707999);
static const int64_t AUG3_0600           = INT64_C(1785708000);  // sync boundary
static const int64_t AUG3_0600_01        = INT64_C(1785708001);
static const int64_t AUG3_0700           = INT64_C(1785711600);
static const int64_t AUG3_0800           = INT64_C(1785715200);  // == 00:00Z Aug 3
static const int64_t AUG2_0600           = INT64_C(1785621600);  // yesterday boundary
static const int64_t AUG4_0600           = INT64_C(1785794400);  // tomorrow boundary

// ===========================================================================
//  shouldAutoSync — invalid clock
// ===========================================================================
void test_invalid_clock_zero_is_never_due(void) {
    // Even "never synced" loses to an invalid clock: we cannot schedule on a
    // bad time, so the caller must obtain NTP first.
    TEST_ASSERT_FALSE(shouldAutoSync(0, 0, HOUR, TZ, STALE));
}

void test_invalid_clock_negative_is_never_due(void) {
    TEST_ASSERT_FALSE(shouldAutoSync(-100, AUG3_0800, HOUR, TZ, STALE));
    TEST_ASSERT_FALSE(shouldAutoSync(-1, 0, HOUR, TZ, STALE));
}

// ===========================================================================
//  shouldAutoSync — never synced
// ===========================================================================
void test_never_synced_is_due(void) {
    TEST_ASSERT_TRUE(shouldAutoSync(AUG3_0800, 0, HOUR, TZ, STALE));
}

void test_never_synced_negative_last_is_due(void) {
    TEST_ASSERT_TRUE(shouldAutoSync(AUG3_0800, -1, HOUR, TZ, STALE));
}

// ===========================================================================
//  shouldAutoSync — crossed the daily sync hour vs not yet
// ===========================================================================
void test_crossed_the_hour_since_last_sync_is_due(void) {
    // now 08:00, last sync 05:00 (before today's 06:00 boundary) -> crossed.
    TEST_ASSERT_TRUE(shouldAutoSync(AUG3_0800, AUG3_0500, HOUR, TZ, STALE));
}

void test_not_yet_crossed_same_day_is_not_due(void) {
    // now 08:00, last sync 07:00 (after today's 06:00 boundary) and fresh
    // (1 h old << 20 h stale) -> not due.
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0800, AUG3_0700, HOUR, TZ, STALE));
}

void test_before_sync_hour_today_uses_yesterday_boundary(void) {
    // now 05:00 (before today's 06:00). Most recent boundary is YESTERDAY's
    // 06:00. Stale backstop disabled (0) to isolate the boundary logic (a last
    // sync at yesterday 06:00 is 23 h old and would otherwise trip stale).
    // A last sync exactly AT yesterday 06:00 is not before that boundary...
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0500, AUG2_0600, HOUR, TZ, 0));
    // ...but a last sync just BEFORE yesterday 06:00 crossed it -> due.
    TEST_ASSERT_TRUE(shouldAutoSync(AUG3_0500, AUG2_0600 - 1, HOUR, TZ, 0));
}

// ===========================================================================
//  shouldAutoSync — exactly on the hour boundary
// ===========================================================================
void test_exactly_on_hour_with_old_sync_is_due(void) {
    // now exactly 06:00 today, last sync yesterday 06:00 -> the boundary has
    // just been reached and the last sync predates it -> due.
    TEST_ASSERT_TRUE(shouldAutoSync(AUG3_0600, AUG2_0600, HOUR, TZ, STALE));
}

void test_exactly_on_hour_just_synced_is_not_due(void) {
    // now exactly 06:00 and the last sync IS that same instant -> not before
    // the boundary, and 0 s old -> not due (no immediate re-trigger).
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0600, AUG3_0600, HOUR, TZ, STALE));
}

void test_one_second_before_hour_not_crossed(void) {
    // now 05:59:59 (still before today's 06:00). Most recent boundary is
    // yesterday's; a fresh sync after it is not due.
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0559_59, AUG3_0500, HOUR, TZ, STALE));
}

// ===========================================================================
//  shouldAutoSync — stale backstop
// ===========================================================================
void test_stale_triggers_without_crossing(void) {
    // now 08:00, last sync 07:00 (after the boundary -> NOT crossed) but with a
    // 1 h stale threshold the 1 h-old cache is exactly stale -> due.
    TEST_ASSERT_TRUE(shouldAutoSync(AUG3_0800, AUG3_0700, HOUR, TZ, 3600));
}

void test_not_stale_below_threshold(void) {
    // Same instant, 2 h threshold: 1 h-old cache is fresh -> not due.
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0800, AUG3_0700, HOUR, TZ, 7200));
}

void test_stale_nonpositive_disables_backstop(void) {
    // staleSec <= 0 disables the stale backstop; only the daily boundary can
    // then trigger. Here nothing is crossed -> not due despite elapsed > 0.
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0800, AUG3_0700, HOUR, TZ, 0));
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0800, AUG3_0700, HOUR, TZ, -5));
}

void test_stale_realistic_twenty_hours(void) {
    // now 05:00 Aug 3 (before today's boundary). Last sync 06:30 Aug 2 is after
    // yesterday's boundary (not crossed) but 22.5 h old -> stale (>= 20 h).
    const int64_t AUG2_0630 = AUG2_0600 + 1800;
    TEST_ASSERT_TRUE(shouldAutoSync(AUG3_0500, AUG2_0630, HOUR, TZ, STALE));
    // A 19 h-old cache is not stale and not crossed -> not due.
    const int64_t AUG2_1000 = AUG2_0600 + 4 * 3600;   // 10:00 Aug 2 (19 h before 05:00)
    TEST_ASSERT_FALSE(shouldAutoSync(AUG3_0500, AUG2_1000, HOUR, TZ, STALE));
}

// ===========================================================================
//  secondsUntilNextSync
// ===========================================================================
void test_next_sync_later_today(void) {
    // now 05:00 -> 06:00 today is 1 h away.
    TEST_ASSERT_EQUAL_INT64(3600, secondsUntilNextSync(AUG3_0500, HOUR, TZ));
}

void test_next_sync_from_local_midnight(void) {
    // now 00:00 local -> 06:00 today is 6 h away.
    TEST_ASSERT_EQUAL_INT64(6 * 3600, secondsUntilNextSync(AUG3_MIDNIGHT_LOCAL, HOUR, TZ));
}

void test_next_sync_target_passed_rolls_to_tomorrow(void) {
    // now 08:00 -> today's 06:00 passed; next is 06:00 tomorrow = 22 h away.
    TEST_ASSERT_EQUAL_INT64(AUG4_0600 - AUG3_0800, secondsUntilNextSync(AUG3_0800, HOUR, TZ));
    TEST_ASSERT_EQUAL_INT64(22 * 3600, secondsUntilNextSync(AUG3_0800, HOUR, TZ));
}

void test_next_sync_exactly_on_hour_targets_tomorrow(void) {
    // Exactly at 06:00 -> "at/just past" rule targets tomorrow: a full day.
    TEST_ASSERT_EQUAL_INT64(86400, secondsUntilNextSync(AUG3_0600, HOUR, TZ));
}

void test_next_sync_one_second_before(void) {
    TEST_ASSERT_EQUAL_INT64(1, secondsUntilNextSync(AUG3_0559_59, HOUR, TZ));
}

void test_next_sync_one_second_after(void) {
    // 06:00:01 -> tomorrow 06:00, i.e. 86400 - 1 s.
    TEST_ASSERT_EQUAL_INT64(86399, secondsUntilNextSync(AUG3_0600_01, HOUR, TZ));
}

void test_next_sync_utc_zero_offset(void) {
    // Sanity with no offset: 1785715200 == 2026-08-03T00:00:00Z == local 00:00,
    // so 06:00 (utc) is 6 h away.
    TEST_ASSERT_EQUAL_INT64(6 * 3600, secondsUntilNextSync(AUG3_0800, HOUR, 0));
}

void test_next_sync_always_positive_even_invalid_clock(void) {
    // Invalid clock -> defensive short positive interval (wake to retry NTP).
    TEST_ASSERT_TRUE(secondsUntilNextSync(0, HOUR, TZ) > 0);
    TEST_ASSERT_TRUE(secondsUntilNextSync(-42, HOUR, TZ) > 0);
    TEST_ASSERT_EQUAL_INT64(3600, secondsUntilNextSync(0, HOUR, TZ));
}

// ===========================================================================
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // shouldAutoSync — invalid clock
    RUN_TEST(test_invalid_clock_zero_is_never_due);
    RUN_TEST(test_invalid_clock_negative_is_never_due);
    // shouldAutoSync — never synced
    RUN_TEST(test_never_synced_is_due);
    RUN_TEST(test_never_synced_negative_last_is_due);
    // shouldAutoSync — crossed vs not yet
    RUN_TEST(test_crossed_the_hour_since_last_sync_is_due);
    RUN_TEST(test_not_yet_crossed_same_day_is_not_due);
    RUN_TEST(test_before_sync_hour_today_uses_yesterday_boundary);
    // shouldAutoSync — exactly on the hour
    RUN_TEST(test_exactly_on_hour_with_old_sync_is_due);
    RUN_TEST(test_exactly_on_hour_just_synced_is_not_due);
    RUN_TEST(test_one_second_before_hour_not_crossed);
    // shouldAutoSync — stale
    RUN_TEST(test_stale_triggers_without_crossing);
    RUN_TEST(test_not_stale_below_threshold);
    RUN_TEST(test_stale_nonpositive_disables_backstop);
    RUN_TEST(test_stale_realistic_twenty_hours);
    // secondsUntilNextSync
    RUN_TEST(test_next_sync_later_today);
    RUN_TEST(test_next_sync_from_local_midnight);
    RUN_TEST(test_next_sync_target_passed_rolls_to_tomorrow);
    RUN_TEST(test_next_sync_exactly_on_hour_targets_tomorrow);
    RUN_TEST(test_next_sync_one_second_before);
    RUN_TEST(test_next_sync_one_second_after);
    RUN_TEST(test_next_sync_utc_zero_offset);
    RUN_TEST(test_next_sync_always_positive_even_invalid_clock);

    return UNITY_END();
}
