// ===========================================================================
//  test_calendar_store  —  Unit tests for the CalendarStore seam (Round 2)
// ===========================================================================
//  Hardware-free tests for the pure serialize / deserialize seam in
//  src/apps/calendar/CalendarStore.h (namespace core). The std::string stands
//  in for the on-disk file, exactly like test_bookmark does for BookmarkStore:
//  ArduinoJson is header-only and identical on the host, so abstracting the
//  storage boundary is just passing the in-memory string.
//
//  Covers the robustness contract: round-trips (timed, all-day, recurring),
//  tolerance of empty / garbage / truncated documents, field sanitisation,
//  capacity bounding, and the INT64_MIN "no UNTIL" sentinel handling.
// ===========================================================================
#include <unity.h>
#include <cstring>
#include <string>
#include "config.h"
#include "core/CalendarEvent.h"
#include "apps/calendar/CalendarStore.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// --- Fixture helper ---------------------------------------------------------
static CalendarEvent mkEvent(int64_t s, int64_t e, const char *title,
                             uint8_t cat, bool allDay) {
    CalendarEvent ev;
    calEventClear(ev);
    ev.startUtc = s;
    ev.endUtc   = e;
    strncpy(ev.title, title, CAL_TITLE_MAX - 1);
    ev.title[CAL_TITLE_MAX - 1] = '\0';
    ev.category = cat;
    ev.allDay   = allDay;
    return ev;
}

// ===========================================================================
//  Round-trips
// ===========================================================================
void test_store_roundtrip_basic(void) {
    CalendarEvent in[2];
    in[0] = mkEvent(INT64_C(1785715200), INT64_C(1785718800), "Standup", 0, false);
    in[1] = mkEvent(INT64_C(1785801600), INT64_C(1785888000), "Holiday", 2, true);

    std::string json;
    serializeCalendarCache(json, in, 2, INT64_C(1785700000));

    CalendarEvent out[4];
    int64_t sync = -1;
    int n = deserializeCalendarCache(json, out, 4, sync);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1785700000), sync);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1785715200), out[0].startUtc);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1785718800), out[0].endUtc);
    TEST_ASSERT_EQUAL_STRING("Standup", out[0].title);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].category);
    TEST_ASSERT_FALSE(out[0].allDay);
    TEST_ASSERT_EQUAL_STRING("Holiday", out[1].title);
    TEST_ASSERT_EQUAL_UINT8(2, out[1].category);
    TEST_ASSERT_TRUE(out[1].allDay);
}

void test_store_roundtrip_recurrence_fields(void) {
    CalendarEvent in[1];
    in[0] = mkEvent(INT64_C(1785715200), INT64_C(1785717000), "Weekly sync", 1, false);
    in[0].freq      = CalFreq::Weekly;
    in[0].interval  = 2;
    in[0].count     = 10;
    in[0].untilUtc  = INT64_C(1790000000);
    in[0].bydayMask = calDayBit(0) | calDayBit(4);   // Mon + Fri

    std::string json;
    serializeCalendarCache(json, in, 1, INT64_C(123));

    CalendarEvent out[1];
    int64_t sync = 0;
    int n = deserializeCalendarCache(json, out, 1, sync);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(123, (int)sync);
    TEST_ASSERT_EQUAL_INT((int)CalFreq::Weekly, (int)out[0].freq);
    TEST_ASSERT_EQUAL_INT32(2, out[0].interval);
    TEST_ASSERT_EQUAL_INT32(10, out[0].count);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1790000000), out[0].untilUtc);
    TEST_ASSERT_EQUAL_UINT8(calDayBit(0) | calDayBit(4), out[0].bydayMask);
}

void test_store_until_sentinel_omitted(void) {
    // untilUtc == INT64_MIN means "no UNTIL" and must not be serialised as a
    // number; the round-trip restores the sentinel exactly.
    CalendarEvent in[1] = { mkEvent(1000, 2000, "No until", 0, false) };
    TEST_ASSERT_EQUAL_INT64(INT64_MIN, in[0].untilUtc);   // calEventClear default

    std::string json;
    serializeCalendarCache(json, in, 1, 0);
    TEST_ASSERT_TRUE(json.find("\"u\":") == std::string::npos);   // omitted

    CalendarEvent out[1];
    int64_t sync = 0;
    int n = deserializeCalendarCache(json, out, 1, sync);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT64(INT64_MIN, out[0].untilUtc);
}

// ===========================================================================
//  Corruption tolerance (the load() contract: never crash, return empty)
// ===========================================================================
void test_store_empty_input(void) {
    CalendarEvent out[2];
    int64_t sync = 42;
    int n = deserializeCalendarCache("", out, 2, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT64(0, sync);
}

void test_store_garbage_input(void) {
    CalendarEvent out[2];
    int64_t sync = 42;
    int n = deserializeCalendarCache("this is not json {{{", out, 2, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT64(0, sync);
}

void test_store_truncated_input(void) {
    // Build a valid multi-event document, chop it in half (mid-token, inside
    // a long title) and confirm it is rejected as corrupt.
    CalendarEvent in[3];
    in[0] = mkEvent(1000, 2000, "A very long title that will certainly be cut in half by the truncation below", 0, false);
    in[1] = mkEvent(3000, 4000, "Second event with another reasonably long title for the test", 1, true);
    in[2] = mkEvent(5000, 6000, "Third", 2, false);

    std::string json;
    serializeCalendarCache(json, in, 3, 99);
    TEST_ASSERT_TRUE(json.size() > 60);
    std::string cut = json.substr(0, json.size() / 2);

    CalendarEvent out[3];
    int64_t sync = 0;
    int n = deserializeCalendarCache(cut, out, 3, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_store_missing_events_array_keeps_sync(void) {
    // A valid document with no "ev" array is an empty calendar, NOT corruption:
    // zero events, but the last-sync timestamp survives.
    CalendarEvent out[2];
    int64_t sync = 0;
    int n = deserializeCalendarCache("{\"v\":1,\"sync\":12345}", out, 2, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT64(12345, sync);
}

// ===========================================================================
//  Bounding + sanitisation
// ===========================================================================
void test_store_bounds_to_max(void) {
    CalendarEvent in[5];
    for (int i = 0; i < 5; i++)
        in[i] = mkEvent(1000 + i, 2000 + i, "Ev", 0, false);

    std::string json;
    serializeCalendarCache(json, in, 5, 0);

    CalendarEvent out[3];
    int64_t sync = 0;
    int n = deserializeCalendarCache(json, out, 3, sync);
    TEST_ASSERT_EQUAL_INT(3, n);                 // clamped to caller capacity
}

void test_store_sanitizes_bad_fields(void) {
    // Hostile document: bad freq tag, negative interval/count, end < start,
    // missing title, oversized byday mask. All must be sanitised, not crashed.
    const char *doc =
        "{\"v\":1,\"sync\":7,\"ev\":[{"
        "\"s\":5000,\"e\":4000,\"c\":9,\"a\":true,"
        "\"f\":9,\"i\":-3,\"n\":-1,\"b\":255}]}";

    CalendarEvent out[2];
    int64_t sync = 0;
    int n = deserializeCalendarCache(doc, out, 2, sync);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT64(7, sync);
    TEST_ASSERT_EQUAL_INT64(5000, out[0].startUtc);
    TEST_ASSERT_EQUAL_INT64(5000, out[0].endUtc);      // clamped up to start
    TEST_ASSERT_EQUAL_STRING("", out[0].title);        // missing -> empty
    TEST_ASSERT_EQUAL_UINT8(9, out[0].category);
    TEST_ASSERT_TRUE(out[0].allDay);
    TEST_ASSERT_EQUAL_INT((int)CalFreq::None, (int)out[0].freq);  // 9 -> None
    TEST_ASSERT_EQUAL_INT32(1, out[0].interval);                  // -3 -> 1
    TEST_ASSERT_EQUAL_INT32(0, out[0].count);                     // -1 -> 0
    TEST_ASSERT_EQUAL_UINT8(0x7F, out[0].bydayMask);              // masked to 7 bits
}

void test_store_skips_event_without_start(void) {
    const char *doc =
        "{\"v\":1,\"sync\":1,\"ev\":["
        "{\"e\":9999,\"t\":\"no start\"},"
        "{\"s\":777,\"e\":888,\"t\":\"ok\"}]}";

    CalendarEvent out[2];
    int64_t sync = 0;
    int n = deserializeCalendarCache(doc, out, 2, sync);
    TEST_ASSERT_EQUAL_INT(1, n);                 // first event skipped
    TEST_ASSERT_EQUAL_INT64(777, out[0].startUtc);
    TEST_ASSERT_EQUAL_STRING("ok", out[0].title);
}

void test_store_null_and_zero_args(void) {
    // Null / zero arguments are rejected cleanly, never crashed on.
    int64_t sync = 5;
    TEST_ASSERT_EQUAL_INT(0, deserializeCalendarCache("{}", nullptr, 4, sync));
    CalendarEvent out[1];
    TEST_ASSERT_EQUAL_INT(0, deserializeCalendarCache("{}", out, 0, sync));

    std::string json = "sentinel";
    serializeCalendarCache(json, nullptr, 3, 42);  // null events -> empty list
    TEST_ASSERT_TRUE(json.find("\"ev\":[]") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"sync\":42") != std::string::npos);
}

// ===========================================================================
int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_store_roundtrip_basic);
    RUN_TEST(test_store_roundtrip_recurrence_fields);
    RUN_TEST(test_store_until_sentinel_omitted);
    RUN_TEST(test_store_empty_input);
    RUN_TEST(test_store_garbage_input);
    RUN_TEST(test_store_truncated_input);
    RUN_TEST(test_store_missing_events_array_keeps_sync);
    RUN_TEST(test_store_bounds_to_max);
    RUN_TEST(test_store_sanitizes_bad_fields);
    RUN_TEST(test_store_skips_event_without_start);
    RUN_TEST(test_store_null_and_zero_args);
    return UNITY_END();
}
