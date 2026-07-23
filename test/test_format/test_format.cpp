// ===========================================================================
//  test/test_format/test_format.cpp
// ---------------------------------------------------------------------------
//  Host (native) Unity tests for the human-readable byte-size helper
//  (core/Format.h). Pure arithmetic + snprintf; no hardware. Pins the KiB/MiB
//  boundaries so the storage/USB size readouts stay stable.
// ===========================================================================
#include <unity.h>
#include "core/Format.h"

void setUp(void) {}
void tearDown(void) {}

// --- bytes below 1 KiB render as plain "B" ---------------------------------
static void test_bytes_below_kib(void) {
    TEST_ASSERT_EQUAL_STRING("0 B",    core::humanSize(0).c_str());
    TEST_ASSERT_EQUAL_STRING("1 B",    core::humanSize(1).c_str());
    TEST_ASSERT_EQUAL_STRING("1023 B", core::humanSize(1023).c_str());   // just under 1 KiB
}

// --- 1 KiB boundary: 1024 flips to "1.0 KB" --------------------------------
static void test_kib_boundary(void) {
    TEST_ASSERT_EQUAL_STRING("1.0 KB", core::humanSize(1024).c_str());
    TEST_ASSERT_EQUAL_STRING("1.5 KB", core::humanSize(1536).c_str());
    TEST_ASSERT_EQUAL_STRING("1024.0 KB",
                             core::humanSize(1024ULL * 1024 - 1).c_str()); // 1023.999.. rounds to 1024.0, still < 1 MiB
}

// --- 1 MiB boundary: 1048576 flips to "1.0 MB" -----------------------------
static void test_mib_boundary(void) {
    TEST_ASSERT_EQUAL_STRING("1.0 MB", core::humanSize(1024ULL * 1024).c_str());
    TEST_ASSERT_EQUAL_STRING("2.5 MB", core::humanSize(1024ULL * 1024 * 5 / 2).c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_bytes_below_kib);
    RUN_TEST(test_kib_boundary);
    RUN_TEST(test_mib_boundary);
    return UNITY_END();
}
