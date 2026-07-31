// ===========================================================================
//  test_app_framework  —  Unit tests for the multi-app framework seams
// ===========================================================================
//  Tests the pure-logic seams extracted into src/core/ButtonClassify.h:
//    - classifyRelease(): hold-band → gesture mapping (tap/medium/bounce)
//    - isLongPress(): long-press threshold detection
//    - wrapSelection(): list navigation wrap-around math
// ===========================================================================
#include <unity.h>
#include "core/ButtonClassify.h"

using core::Gesture;
using core::classifyRelease;
using core::isLongPress;
using core::wrapSelection;

void setUp(void) {}
void tearDown(void) {}

// ===========================================================================
//  classifyRelease() — hold-band gesture classification on button release
// ===========================================================================

// --- Bounce rejection (< 30ms) ---
void test_release_0ms_is_bounce(void) {
    TEST_ASSERT_EQUAL(Gesture::None, classifyRelease(0));
}
void test_release_15ms_is_bounce(void) {
    TEST_ASSERT_EQUAL(Gesture::None, classifyRelease(15));
}
void test_release_29ms_is_bounce(void) {
    TEST_ASSERT_EQUAL(Gesture::None, classifyRelease(29));
}

// --- Boundary: exactly 30ms is the first valid tap ---
void test_release_30ms_is_tap(void) {
    TEST_ASSERT_EQUAL(Gesture::Tap, classifyRelease(30));
}

// --- Tap band (30ms .. 349ms) ---
void test_release_100ms_is_tap(void) {
    TEST_ASSERT_EQUAL(Gesture::Tap, classifyRelease(100));
}
void test_release_200ms_is_tap(void) {
    TEST_ASSERT_EQUAL(Gesture::Tap, classifyRelease(200));
}
void test_release_349ms_is_tap(void) {
    TEST_ASSERT_EQUAL(Gesture::Tap, classifyRelease(349));
}

// --- Boundary: exactly 350ms crosses into MediumHold ---
void test_release_350ms_is_mediumhold(void) {
    TEST_ASSERT_EQUAL(Gesture::MediumHold, classifyRelease(350));
}

// --- MediumHold band (350ms .. 749ms) ---
void test_release_500ms_is_mediumhold(void) {
    TEST_ASSERT_EQUAL(Gesture::MediumHold, classifyRelease(500));
}
void test_release_749ms_is_mediumhold(void) {
    TEST_ASSERT_EQUAL(Gesture::MediumHold, classifyRelease(749));
}

// --- Beyond long-press threshold: still MediumHold on release ---
// (LongHold fires WHILE held, not on release — if the user releases after
//  the long-press threshold without the longFired flag having been set,
//  classifyRelease still returns MediumHold.)
void test_release_750ms_is_mediumhold(void) {
    TEST_ASSERT_EQUAL(Gesture::MediumHold, classifyRelease(750));
}
void test_release_2000ms_is_mediumhold(void) {
    TEST_ASSERT_EQUAL(Gesture::MediumHold, classifyRelease(2000));
}

// --- Custom thresholds ---
void test_custom_thresholds_tap(void) {
    // debounce=50, prevHold=400, longPress=900
    TEST_ASSERT_EQUAL(Gesture::None,       classifyRelease(49, 50, 400, 900));
    TEST_ASSERT_EQUAL(Gesture::Tap,        classifyRelease(50, 50, 400, 900));
    TEST_ASSERT_EQUAL(Gesture::Tap,        classifyRelease(399, 50, 400, 900));
    TEST_ASSERT_EQUAL(Gesture::MediumHold, classifyRelease(400, 50, 400, 900));
}

// ===========================================================================
//  isLongPress() — threshold detection while button is held
// ===========================================================================
void test_longpress_below_threshold(void) {
    TEST_ASSERT_FALSE(isLongPress(0));
    TEST_ASSERT_FALSE(isLongPress(749));
}
void test_longpress_at_threshold(void) {
    TEST_ASSERT_TRUE(isLongPress(750));
}
void test_longpress_above_threshold(void) {
    TEST_ASSERT_TRUE(isLongPress(1000));
    TEST_ASSERT_TRUE(isLongPress(5000));
}
void test_longpress_custom_threshold(void) {
    TEST_ASSERT_FALSE(isLongPress(899, 900));
    TEST_ASSERT_TRUE(isLongPress(900, 900));
}

// ===========================================================================
//  wrapSelection() — list navigation wrap-around
// ===========================================================================
void test_wrap_forward_normal(void) {
    TEST_ASSERT_EQUAL(1, wrapSelection(0, 1, 5));
    TEST_ASSERT_EQUAL(2, wrapSelection(1, 1, 5));
    TEST_ASSERT_EQUAL(4, wrapSelection(3, 1, 5));
}
void test_wrap_forward_wraps_to_zero(void) {
    TEST_ASSERT_EQUAL(0, wrapSelection(4, 1, 5));
    TEST_ASSERT_EQUAL(0, wrapSelection(7, 1, 8));
}
void test_wrap_backward_normal(void) {
    TEST_ASSERT_EQUAL(3, wrapSelection(4, -1, 5));
    TEST_ASSERT_EQUAL(0, wrapSelection(1, -1, 5));
}
void test_wrap_backward_wraps_to_end(void) {
    TEST_ASSERT_EQUAL(4, wrapSelection(0, -1, 5));
    TEST_ASSERT_EQUAL(2, wrapSelection(0, -1, 3));
}
void test_wrap_single_item(void) {
    TEST_ASSERT_EQUAL(0, wrapSelection(0, 1, 1));
    TEST_ASSERT_EQUAL(0, wrapSelection(0, -1, 1));
}
void test_wrap_zero_count_guards(void) {
    TEST_ASSERT_EQUAL(0, wrapSelection(0, 1, 0));
    TEST_ASSERT_EQUAL(0, wrapSelection(3, -1, 0));
    TEST_ASSERT_EQUAL(0, wrapSelection(0, 1, -1));
}
void test_wrap_large_delta(void) {
    // delta > count: wraps once (not modular — matches the UI's single-step use)
    TEST_ASSERT_EQUAL(0, wrapSelection(0, 5, 5));
}

// ===========================================================================
//  Runner
// ===========================================================================
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // classifyRelease
    RUN_TEST(test_release_0ms_is_bounce);
    RUN_TEST(test_release_15ms_is_bounce);
    RUN_TEST(test_release_29ms_is_bounce);
    RUN_TEST(test_release_30ms_is_tap);
    RUN_TEST(test_release_100ms_is_tap);
    RUN_TEST(test_release_200ms_is_tap);
    RUN_TEST(test_release_349ms_is_tap);
    RUN_TEST(test_release_350ms_is_mediumhold);
    RUN_TEST(test_release_500ms_is_mediumhold);
    RUN_TEST(test_release_749ms_is_mediumhold);
    RUN_TEST(test_release_750ms_is_mediumhold);
    RUN_TEST(test_release_2000ms_is_mediumhold);
    RUN_TEST(test_custom_thresholds_tap);

    // isLongPress
    RUN_TEST(test_longpress_below_threshold);
    RUN_TEST(test_longpress_at_threshold);
    RUN_TEST(test_longpress_above_threshold);
    RUN_TEST(test_longpress_custom_threshold);

    // wrapSelection
    RUN_TEST(test_wrap_forward_normal);
    RUN_TEST(test_wrap_forward_wraps_to_zero);
    RUN_TEST(test_wrap_backward_normal);
    RUN_TEST(test_wrap_backward_wraps_to_end);
    RUN_TEST(test_wrap_single_item);
    RUN_TEST(test_wrap_zero_count_guards);
    RUN_TEST(test_wrap_large_delta);

    return UNITY_END();
}
