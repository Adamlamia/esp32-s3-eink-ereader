// ===========================================================================
//  test/test_validation/test_validation.cpp
// ---------------------------------------------------------------------------
//  Host (native) Unity tests for the security / robustness seams the review
//  called out: the web path guard (core/PathValidation.h, review C1/S5) and
//  the battery-voltage math (core/BatteryMath.h, review S1). Both suites are
//  written to FAIL on the pre-fix firmware and PASS once the review's diffs
//  are applied -- see the per-suite notes below. Hardware-free: pure functions,
//  no ADC, no network, no FS.
// ===========================================================================
#include <unity.h>
#include "core/PathValidation.h"
#include "core/BatteryMath.h"

void setUp(void) {}
void tearDown(void) {}

// --- isBookPath: accept legitimate book files ------------------------------
// Pre-fix (C1/S5): the routes did NO validation, so any path was accepted and
// the reject cases below would all "pass through" to a real file operation.
static void test_isbookpath_accepts_valid(void) {
    TEST_ASSERT_TRUE(core::isBookPath("/books/a.txt"));
    TEST_ASSERT_TRUE(core::isBookPath("/books/A.TXT"));       // extension case-insensitive
    TEST_ASSERT_TRUE(core::isBookPath("/books/pocket-haiku.txt"));
}

// --- isBookPath: reject traversal, escape and non-.txt ---------------------
static void test_isbookpath_rejects_traversal_and_escape(void) {
    TEST_ASSERT_FALSE(core::isBookPath("/bookmarks.json"));   // sibling, not under /books/
    TEST_ASSERT_FALSE(core::isBookPath("/books/../secret"));  // parent traversal
    TEST_ASSERT_FALSE(core::isBookPath("/books/sub/a.txt"));  // nested subdirectory
    TEST_ASSERT_FALSE(core::isBookPath("/books/a.bin"));      // wrong extension
    TEST_ASSERT_FALSE(core::isBookPath("/books/"));           // empty file name
    TEST_ASSERT_FALSE(core::isBookPath(""));                  // empty path
}

// --- isBookPath: guard edges of the extension check ------------------------
static void test_isbookpath_extension_edges(void) {
    TEST_ASSERT_FALSE(core::isBookPath("/books/.txt"));       // name is just ".txt" -> nl<5
    TEST_ASSERT_TRUE(core::isBookPath("/books/x.txt"));       // shortest valid name
    TEST_ASSERT_FALSE(core::isBookPath("/books/txt"));        // no extension
}

// --- battery: post-fix signed math clamps a low pack to 0 ------------------
// Pre-fix (S1): (mv - 3300) was computed in uint32_t, so mv<3300 wrapped to a
// huge value and clamped to 100. This asserts the signed post-fix contract, so
// it FAILS on the pre-fix formula (which would return 100 for 3000 mV).
static void test_battery_below_min_is_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, core::batteryPercentFromMv(3300));
    TEST_ASSERT_EQUAL_INT(0, core::batteryPercentFromMv(3000));   // would be 100 pre-fix
    TEST_ASSERT_EQUAL_INT(0, core::batteryPercentFromMv(0));
}

static void test_battery_above_max_is_hundred(void) {
    TEST_ASSERT_EQUAL_INT(100, core::batteryPercentFromMv(4200));
    TEST_ASSERT_EQUAL_INT(100, core::batteryPercentFromMv(4500));
}

static void test_battery_midpoint_is_about_fifty(void) {
    // Midpoint of 3300..4200 is 3750 mV -> exactly 50%.
    TEST_ASSERT_EQUAL_INT(50, core::batteryPercentFromMv(3750));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_isbookpath_accepts_valid);
    RUN_TEST(test_isbookpath_rejects_traversal_and_escape);
    RUN_TEST(test_isbookpath_extension_edges);
    RUN_TEST(test_battery_below_min_is_zero);
    RUN_TEST(test_battery_above_max_is_hundred);
    RUN_TEST(test_battery_midpoint_is_about_fifty);
    return UNITY_END();
}
