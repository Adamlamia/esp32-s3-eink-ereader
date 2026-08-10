// ===========================================================================
//  test/test_cacerts/test_cacerts.cpp
// ---------------------------------------------------------------------------
//  Host (native) Unity tests for the CA certificate lookup seam (core/CaCerts.h).
//  Pure logic: maps hostnames to their root CA certificates for TLS validation.
//  No hardware, no HAL dependencies.
//
//  Tests verify:
//    - Known hosts (calendar.google.com, api.open-meteo.com, api.github.com)
//      return non-null CA certificates
//    - Unknown hosts return nullptr (caller decides fallback behavior)
//    - Null host returns nullptr (defensive)
// ===========================================================================
#include <unity.h>
#include "core/CaCerts.h"

void setUp(void) {}
void tearDown(void) {}

// --- Known host: calendar.google.com returns GTS Root R1 -------------------
static void test_calendar_google_com_returns_cert(void) {
    const char *ca = core::caCertForHost("calendar.google.com");
    TEST_ASSERT_NOT_NULL(ca);
    // Verify it's the GTS Root R1 cert (starts with the expected PEM header)
    TEST_ASSERT_EQUAL_STRING_LEN("-----BEGIN CERTIFICATE-----", ca, 27);
}

// --- Known host: api.open-meteo.com returns ISRG Root X1 -------------------
static void test_api_open_meteo_com_returns_cert(void) {
    const char *ca = core::caCertForHost("api.open-meteo.com");
    TEST_ASSERT_NOT_NULL(ca);
    // Verify it's the ISRG Root X1 cert (starts with the expected PEM header)
    TEST_ASSERT_EQUAL_STRING_LEN("-----BEGIN CERTIFICATE-----", ca, 27);
}

// --- Known host: api.github.com returns ISRG Root X1 -----------------------
static void test_api_github_com_returns_cert(void) {
    const char *ca = core::caCertForHost("api.github.com");
    TEST_ASSERT_NOT_NULL(ca);
    // Verify it's the ISRG Root X1 cert (starts with the expected PEM header)
    TEST_ASSERT_EQUAL_STRING_LEN("-----BEGIN CERTIFICATE-----", ca, 27);
}

// --- Unknown host returns nullptr ------------------------------------------
static void test_unknown_host_returns_null(void) {
    const char *ca = core::caCertForHost("unknown.example.com");
    TEST_ASSERT_NULL(ca);
}

// --- Null host returns nullptr (defensive) ---------------------------------
static void test_null_host_returns_null(void) {
    const char *ca = core::caCertForHost(nullptr);
    TEST_ASSERT_NULL(ca);
}

// --- Verify ISRG Root X1 and GTS Root R1 are distinct certificates ---------
static void test_certs_are_distinct(void) {
    const char *isrg = core::caCertForHost("api.github.com");
    const char *gts  = core::caCertForHost("calendar.google.com");
    TEST_ASSERT_NOT_NULL(isrg);
    TEST_ASSERT_NOT_NULL(gts);
    // They should be different pointers (different cert data)
    TEST_ASSERT_NOT_EQUAL(isrg, gts);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_calendar_google_com_returns_cert);
    RUN_TEST(test_api_open_meteo_com_returns_cert);
    RUN_TEST(test_api_github_com_returns_cert);
    RUN_TEST(test_unknown_host_returns_null);
    RUN_TEST(test_null_host_returns_null);
    RUN_TEST(test_certs_are_distinct);
    return UNITY_END();
}
