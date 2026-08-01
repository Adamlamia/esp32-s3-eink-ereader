// ===========================================================================
//  test_weather  —  Unit tests for the Open-Meteo core seam (WTH·R1)
// ===========================================================================
//  Hardware-free tests for the pure seams in src/core/OpenMeteo.h:
//    parseOpenMeteo          — tolerant Open-Meteo response parsing
//    serializeWeatherCache   — /weather.json cache writer
//    deserializeWeatherCache — /weather.json cache reader (corrupt-file contract)
//    buildOpenMeteoUrl       — request-URL builder (fixed buffer, no overflow)
//    weatherCodeGlyph        — WMO code -> ASCII glyph bucketing
//    formatTenthsC           — tenths-of-°C formatting
//
//  The Open-Meteo fixture below was captured live from
//    https://api.open-meteo.com/v1/forecast?latitude=3.1390&longitude=101.6869
//    &current=temperature_2m,apparent_temperature,relative_humidity_2m,
//    wind_speed_10m,weather_code&daily=weather_code,temperature_2m_max,
//    temperature_2m_min&timezone=Asia%2FKuala_Lumpur&forecast_days=3
//  (Kuala Lumpur, 2026-08-01) and inlined so the tests never touch the network.
// ===========================================================================
#include <unity.h>
#include <cstring>
#include <string>
#include "config.h"
#include "core/OpenMeteo.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// --- Live-shape fixture (KL, 3-day) ------------------------------------------
static const char *FIXTURE_KL =
    "{\"latitude\":3.1282952,\"longitude\":101.68548,\"generationtime_ms\":0.23,"
    "\"utc_offset_seconds\":28800,\"timezone\":\"Asia/Kuala_Lumpur\","
    "\"timezone_abbreviation\":\"GMT+8\",\"elevation\":55.0,"
    "\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\","
    "\"temperature_2m\":\"°C\",\"apparent_temperature\":\"°C\","
    "\"relative_humidity_2m\":\"%\",\"wind_speed_10m\":\"km/h\","
    "\"weather_code\":\"wmo code\"},"
    "\"current\":{\"time\":\"2026-08-01T22:45\",\"interval\":900,"
    "\"temperature_2m\":26.9,\"apparent_temperature\":33.7,"
    "\"relative_humidity_2m\":92,\"wind_speed_10m\":1.1,\"weather_code\":2},"
    "\"daily_units\":{\"time\":\"iso8601\",\"weather_code\":\"wmo code\","
    "\"temperature_2m_max\":\"°C\",\"temperature_2m_min\":\"°C\"},"
    "\"daily\":{\"time\":[\"2026-08-01\",\"2026-08-02\",\"2026-08-03\"],"
    "\"weather_code\":[81,80,81],"
    "\"temperature_2m_max\":[31.6,32.9,31.4],"
    "\"temperature_2m_min\":[25.1,24.8,23.2]}}";

// ===========================================================================
//  parseOpenMeteo — happy path
// ===========================================================================
void test_parse_happy_path(void) {
    WeatherSnapshot s;
    TEST_ASSERT_TRUE(parseOpenMeteo(FIXTURE_KL, s));

    // current conditions
    TEST_ASSERT_TRUE(s.cur.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.9f, s.cur.tempC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.7f, s.cur.feelsC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.1f,  s.cur.windKph);
    TEST_ASSERT_EQUAL_INT(92, s.cur.humidityPct);
    TEST_ASSERT_EQUAL_INT(2,  s.cur.weatherCode);

    // daily forecast: 3 days, tenths-of-°C ints
    TEST_ASSERT_EQUAL_INT(3, s.dayCount);
    TEST_ASSERT_TRUE(s.days[0].valid);
    TEST_ASSERT_EQUAL_INT16(251, s.days[0].tMin);     // 25.1 °C
    TEST_ASSERT_EQUAL_INT16(316, s.days[0].tMax);     // 31.6 °C
    TEST_ASSERT_EQUAL_INT(81, s.days[0].weatherCode);
    TEST_ASSERT_EQUAL_INT16(248, s.days[1].tMin);
    TEST_ASSERT_EQUAL_INT16(329, s.days[1].tMax);
    TEST_ASSERT_EQUAL_INT(80, s.days[1].weatherCode);
    TEST_ASSERT_EQUAL_INT16(232, s.days[2].tMin);
    TEST_ASSERT_EQUAL_INT16(314, s.days[2].tMax);

    // parser leaves the sync-owned fields untouched
    TEST_ASSERT_EQUAL_INT64(0, s.fetchedUtc);
    TEST_ASSERT_EQUAL_STRING("", s.label);
}

// ===========================================================================
//  parseOpenMeteo — rejection contract (never crash, return false)
// ===========================================================================
void test_parse_empty_input(void) {
    WeatherSnapshot s;
    TEST_ASSERT_FALSE(parseOpenMeteo("", s));
    TEST_ASSERT_EQUAL_INT(0, s.dayCount);
    TEST_ASSERT_FALSE(s.cur.valid);
}

void test_parse_garbage_input(void) {
    WeatherSnapshot s;
    TEST_ASSERT_FALSE(parseOpenMeteo("this is not json {{{", s));
    TEST_ASSERT_EQUAL_INT(0, s.dayCount);
}

void test_parse_non_object(void) {
    WeatherSnapshot s;
    TEST_ASSERT_FALSE(parseOpenMeteo("[1,2,3]", s));
    TEST_ASSERT_FALSE(parseOpenMeteo("\"hello\"", s));
    TEST_ASSERT_FALSE(parseOpenMeteo("null", s));
}

void test_parse_missing_current(void) {
    WeatherSnapshot s;
    TEST_ASSERT_FALSE(parseOpenMeteo(
        "{\"daily\":{\"weather_code\":[1],\"temperature_2m_max\":[30.0],"
        "\"temperature_2m_min\":[24.0]}}", s));
}

void test_parse_missing_daily(void) {
    WeatherSnapshot s;
    TEST_ASSERT_FALSE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":26.9}}", s));
}

// NEGATIVE TEST: a truncated/garbage body (mid-token cut of a real response)
// must be rejected LOUDLY — parseOpenMeteo returns false, never half-fills.
void test_parse_truncated_body_rejected(void) {
    std::string full(FIXTURE_KL);
    std::string cut = full.substr(0, full.size() / 2);   // mid-token chop
    WeatherSnapshot s;
    TEST_ASSERT_FALSE(parseOpenMeteo(cut, s));
    TEST_ASSERT_FALSE(s.cur.valid);
    TEST_ASSERT_EQUAL_INT(0, s.dayCount);
}

// ===========================================================================
//  parseOpenMeteo — tolerance + sanitisation
// ===========================================================================
void test_parse_short_daily_arrays(void) {
    // Only one daily element: dayCount follows the data, no out-of-bounds.
    WeatherSnapshot s;
    TEST_ASSERT_TRUE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":26.0},"
        "\"daily\":{\"weather_code\":[3],\"temperature_2m_max\":[30.5],"
        "\"temperature_2m_min\":[24.5]}}", s));
    TEST_ASSERT_EQUAL_INT(1, s.dayCount);
    TEST_ASSERT_EQUAL_INT16(245, s.days[0].tMin);
    TEST_ASSERT_EQUAL_INT16(305, s.days[0].tMax);
}

void test_parse_long_daily_arrays_clamped(void) {
    // 5 daily entries but capacity is WEATHER_FORECAST_DAYS (3): clamp, no crash.
    WeatherSnapshot s;
    TEST_ASSERT_TRUE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":26.0},"
        "\"daily\":{\"weather_code\":[0,1,2,3,45],"
        "\"temperature_2m_max\":[30.0,30.1,30.2,30.3,30.4],"
        "\"temperature_2m_min\":[24.0,24.1,24.2,24.3,24.4]}}", s));
    TEST_ASSERT_EQUAL_INT(WEATHER_FORECAST_DAYS, s.dayCount);
    TEST_ASSERT_EQUAL_INT(2, s.days[2].weatherCode);   // third entry, not fifth
}

void test_parse_humidity_clamped(void) {
    WeatherSnapshot s;
    TEST_ASSERT_TRUE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":26.0,\"relative_humidity_2m\":147},"
        "\"daily\":{}}", s));
    TEST_ASSERT_EQUAL_INT(100, s.cur.humidityPct);     // >100 clamps down

    TEST_ASSERT_TRUE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":26.0,\"relative_humidity_2m\":-9},"
        "\"daily\":{}}", s));
    TEST_ASSERT_EQUAL_INT(0, s.cur.humidityPct);       // <0 clamps up
}

void test_parse_nonfinite_rejected(void) {
    // 1e999 overflows float to inf: the parser must reject it (cur invalid)
    // while still accepting the structurally valid document.
    WeatherSnapshot s;
    TEST_ASSERT_TRUE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":1e999},"
        "\"daily\":{\"temperature_2m_max\":[1e999],\"temperature_2m_min\":[24.0]}}", s));
    TEST_ASSERT_FALSE(s.cur.valid);                    // inf temperature rejected
    TEST_ASSERT_EQUAL_INT(1, s.dayCount);
    TEST_ASSERT_TRUE(s.days[0].valid);                 // finite min still usable
    TEST_ASSERT_EQUAL_INT16(240, s.days[0].tMin);
    TEST_ASSERT_EQUAL_INT16(240, s.days[0].tMax);      // inf max mirrored from min

    // A literal NaN token is not valid JSON at all -> whole parse fails.
    TEST_ASSERT_FALSE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":NaN},\"daily\":{}}", s));
}

void test_parse_negative_temps_and_wind_clamp(void) {
    WeatherSnapshot s;
    TEST_ASSERT_TRUE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":-5.2,\"wind_speed_10m\":-3.0},"
        "\"daily\":{\"temperature_2m_max\":[-1.0],\"temperature_2m_min\":[-9.4]}}", s));
    TEST_ASSERT_TRUE(s.cur.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.2f, s.cur.tempC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.2f, s.cur.feelsC);  // missing -> mirrors temp
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f,  s.cur.windKph); // negative -> 0
    TEST_ASSERT_EQUAL_INT16(-94, s.days[0].tMin);
    TEST_ASSERT_EQUAL_INT16(-10, s.days[0].tMax);
}

void test_parse_inverted_min_max_swapped(void) {
    // Hostile API: min > max. The seam swaps them rather than rendering nonsense.
    WeatherSnapshot s;
    TEST_ASSERT_TRUE(parseOpenMeteo(
        "{\"current\":{\"temperature_2m\":26.0},"
        "\"daily\":{\"temperature_2m_max\":[20.0],\"temperature_2m_min\":[30.0]}}", s));
    TEST_ASSERT_EQUAL_INT16(200, s.days[0].tMin);
    TEST_ASSERT_EQUAL_INT16(300, s.days[0].tMax);
}

// ===========================================================================
//  weatherCodeGlyph — one code per bucket + the unknown fallback
// ===========================================================================
void test_glyph_buckets(void) {
    TEST_ASSERT_EQUAL_STRING("SUN", weatherCodeGlyph(0));    // clear sky
    TEST_ASSERT_EQUAL_STRING("PRT", weatherCodeGlyph(1));    // mainly clear
    TEST_ASSERT_EQUAL_STRING("PRT", weatherCodeGlyph(2));    // partly cloudy
    TEST_ASSERT_EQUAL_STRING("CLD", weatherCodeGlyph(3));    // overcast
    TEST_ASSERT_EQUAL_STRING("FOG", weatherCodeGlyph(45));   // fog
    TEST_ASSERT_EQUAL_STRING("FOG", weatherCodeGlyph(48));   // rime fog
    TEST_ASSERT_EQUAL_STRING("RAN", weatherCodeGlyph(51));   // drizzle
    TEST_ASSERT_EQUAL_STRING("RAN", weatherCodeGlyph(65));   // heavy rain
    TEST_ASSERT_EQUAL_STRING("RAN", weatherCodeGlyph(81));   // rain shower
    TEST_ASSERT_EQUAL_STRING("SNW", weatherCodeGlyph(71));   // light snow
    TEST_ASSERT_EQUAL_STRING("SNW", weatherCodeGlyph(86));   // heavy snow shower
    TEST_ASSERT_EQUAL_STRING("STM", weatherCodeGlyph(95));   // thunderstorm
    TEST_ASSERT_EQUAL_STRING("STM", weatherCodeGlyph(99));   // storm + heavy hail
    TEST_ASSERT_EQUAL_STRING("?",   weatherCodeGlyph(-1));   // unknown
    TEST_ASSERT_EQUAL_STRING("?",   weatherCodeGlyph(4));    // gap in the table
    TEST_ASSERT_EQUAL_STRING("?",   weatherCodeGlyph(100));  // out of WMO range
}

// ===========================================================================
//  Cache round-trips (serialize / deserialize)
// ===========================================================================
static WeatherSnapshot mkFullSnapshot(void) {
    WeatherSnapshot s;
    weatherSnapshotClear(s);
    s.cur.tempC       = 26.9f;
    s.cur.feelsC      = 33.7f;
    s.cur.windKph     = 1.1f;
    s.cur.humidityPct = 92;
    s.cur.weatherCode = 2;
    s.cur.valid       = true;
    s.dayCount        = 3;
    s.days[0] = { 251, 316, 81, true };
    s.days[1] = { 248, 329, 80, true };
    s.days[2] = { 232, 314, 81, true };
    s.fetchedUtc = INT64_C(1785715200);
    strncpy(s.label, "Kuala Lumpur", WEATHER_LABEL_MAX - 1);
    s.label[WEATHER_LABEL_MAX - 1] = '\0';
    return s;
}

void test_cache_roundtrip_full(void) {
    WeatherSnapshot in = mkFullSnapshot();
    std::string json;
    serializeWeatherCache(json, in);

    // compact schema v1 keys are present
    TEST_ASSERT_TRUE(json.find("\"v\":1")        != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"sync\":1785715200") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"lbl\":\"Kuala Lumpur\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"cur\":{")      != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"days\":[")     != std::string::npos);

    WeatherSnapshot out;
    TEST_ASSERT_TRUE(deserializeWeatherCache(json, out));
    TEST_ASSERT_EQUAL_INT64(in.fetchedUtc, out.fetchedUtc);
    TEST_ASSERT_EQUAL_STRING(in.label, out.label);
    TEST_ASSERT_TRUE(out.cur.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.9f, out.cur.tempC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.7f, out.cur.feelsC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.1f,  out.cur.windKph);
    TEST_ASSERT_EQUAL_INT(92, out.cur.humidityPct);
    TEST_ASSERT_EQUAL_INT(2,  out.cur.weatherCode);
    TEST_ASSERT_EQUAL_INT(3,  out.dayCount);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(out.days[i].valid);
        TEST_ASSERT_EQUAL_INT16(in.days[i].tMin, out.days[i].tMin);
        TEST_ASSERT_EQUAL_INT16(in.days[i].tMax, out.days[i].tMax);
        TEST_ASSERT_EQUAL_INT(in.days[i].weatherCode, out.days[i].weatherCode);
    }
}

void test_cache_roundtrip_empty_snapshot(void) {
    // A never-synced snapshot round-trips its invalid flags (stable round-trip).
    WeatherSnapshot in;
    weatherSnapshotClear(in);
    std::string json;
    serializeWeatherCache(json, in);

    WeatherSnapshot out;
    TEST_ASSERT_TRUE(deserializeWeatherCache(json, out));
    TEST_ASSERT_EQUAL_INT64(0, out.fetchedUtc);
    TEST_ASSERT_EQUAL_STRING("", out.label);
    TEST_ASSERT_FALSE(out.cur.valid);
    TEST_ASSERT_EQUAL_INT(0, out.dayCount);
}

// NEGATIVE TEST (WeatherStore::load corrupt-file contract, host-side): a
// corrupt cache document must yield false + a fully cleared snapshot, never a
// crash and never stale half-data.
void test_cache_corrupt_input_yields_empty(void) {
    WeatherSnapshot out;
    out.fetchedUtc = 12345;                       // pre-dirty to prove the reset
    out.cur.valid  = true;
    TEST_ASSERT_FALSE(deserializeWeatherCache("{{{ not json", out));
    TEST_ASSERT_EQUAL_INT64(0, out.fetchedUtc);
    TEST_ASSERT_FALSE(out.cur.valid);
    TEST_ASSERT_EQUAL_INT(0, out.dayCount);
    TEST_ASSERT_EQUAL_STRING("", out.label);

    // truncated mid-document (half of a real cache) is corrupt too
    WeatherSnapshot in = mkFullSnapshot();
    std::string json;
    serializeWeatherCache(json, in);
    std::string cut = json.substr(0, json.size() / 2);
    TEST_ASSERT_FALSE(deserializeWeatherCache(cut, out));
    TEST_ASSERT_EQUAL_INT64(0, out.fetchedUtc);
}

void test_cache_missing_fields_tolerated(void) {
    // A structurally valid document with no data: accepted, everything default.
    WeatherSnapshot out;
    TEST_ASSERT_TRUE(deserializeWeatherCache("{\"v\":1}", out));
    TEST_ASSERT_EQUAL_INT64(0, out.fetchedUtc);
    TEST_ASSERT_FALSE(out.cur.valid);
    TEST_ASSERT_EQUAL_INT(0, out.dayCount);

    // Missing "lbl" + partial cur: label empty, fields sanitised.
    TEST_ASSERT_TRUE(deserializeWeatherCache(
        "{\"v\":1,\"sync\":777,\"cur\":{\"ok\":true,\"t\":25.5,\"h\":250}}", out));
    TEST_ASSERT_EQUAL_INT64(777, out.fetchedUtc);
    TEST_ASSERT_EQUAL_STRING("", out.label);
    TEST_ASSERT_TRUE(out.cur.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.5f, out.cur.tempC);
    TEST_ASSERT_EQUAL_INT(100, out.cur.humidityPct);   // 250 clamps to 100
}

void test_cache_label_truncated(void) {
    // Oversized label in the document is truncated, always NUL-terminated.
    std::string doc = "{\"v\":1,\"lbl\":\"";
    for (int i = 0; i < 60; i++) doc += "X";
    doc += "\"}";
    WeatherSnapshot out;
    TEST_ASSERT_TRUE(deserializeWeatherCache(doc, out));
    TEST_ASSERT_EQUAL_INT(WEATHER_LABEL_MAX - 1, (int)strlen(out.label));
    TEST_ASSERT_EQUAL_UINT8('\0', out.label[WEATHER_LABEL_MAX - 1]);
}

// ===========================================================================
//  WTH·R2 regression tests: cache-read sanitisation (defence in depth)
// ===========================================================================
//  The live parser (parseOpenMeteo) clamps wind/humidity/code and rejects
//  non-finite floats; the cache READER must enforce the same envelope so a
//  corrupt / hand-edited / future-schema /weather.json can never surface an
//  out-of-physical-range value on screen. Each test below FAILS without the
//  R2 hardening of deserializeWeatherCache and passes with it.
void test_cache_cur_wind_clamped(void) {
    WeatherSnapshot out;
    // Out-of-range cached wind is clamped to the parser's 0..500 km/h envelope.
    TEST_ASSERT_TRUE(deserializeWeatherCache(
        "{\"v\":1,\"cur\":{\"ok\":true,\"t\":25.0,\"w\":9999.0}}", out));
    TEST_ASSERT_TRUE(out.cur.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, out.cur.windKph);   // >500 clamps down

    TEST_ASSERT_TRUE(deserializeWeatherCache(
        "{\"v\":1,\"cur\":{\"ok\":true,\"t\":25.0,\"w\":-7.0}}", out));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out.cur.windKph);     // <0 clamps up
}

void test_cache_cur_wind_large_clamped(void) {
    // A large out-of-range cached wind reading is clamped into the physical
    // 0..500 km/h envelope (never an absurd on-screen value). This is the R2
    // read-path hardening: the live parser clamps wind, and now the cache
    // reader enforces the same envelope so a corrupt / hand-edited cache cannot
    // surface e.g. "Wind 98765.0 km/h". (A JSON number that overflows double is
    // deliberately NOT used here: its double->float narrowing is host-defined,
    // so it would make the test non-portable.)
    WeatherSnapshot out;
    TEST_ASSERT_TRUE(deserializeWeatherCache(
        "{\"v\":1,\"cur\":{\"ok\":true,\"t\":25.0,\"w\":98765.0}}", out));
    TEST_ASSERT_TRUE(out.cur.valid);                             // 25.0 temp is fine
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, out.cur.windKph);    // >500 -> 500
}

void test_cache_day_temps_sanitised(void) {
    // Out-of-int16 cached tenths clamp to range; an inverted min/max pair is
    // normalised (mirrors parseOpenMeteo) so the forecast row never shows
    // "300.0 .. -300.0" style nonsense from a corrupt cache.
    WeatherSnapshot out;
    TEST_ASSERT_TRUE(deserializeWeatherCache(
        "{\"v\":1,\"days\":[{\"ok\":true,\"n\":99999,\"x\":-99999}]}", out));
    TEST_ASSERT_EQUAL_INT(1, out.dayCount);
    TEST_ASSERT_EQUAL_INT16(-32760, out.days[0].tMin);           // clamped + swapped
    TEST_ASSERT_EQUAL_INT16( 32760, out.days[0].tMax);
}

void test_cache_day_code_out_of_range(void) {
    // A cached WMO code outside -1..99 normalises to -1 (unknown glyph).
    WeatherSnapshot out;
    TEST_ASSERT_TRUE(deserializeWeatherCache(
        "{\"v\":1,\"days\":[{\"ok\":true,\"n\":240,\"x\":300,\"c\":777}]}", out));
    TEST_ASSERT_EQUAL_INT(-1, out.days[0].weatherCode);
}

void test_cache_dataless_doc_is_empty_state(void) {
    // Empty-state / first-fetch contract at the seam: a structurally valid but
    // data-less cache (the shape a never-completed sync could leave) loads as a
    // CLEARED snapshot -> WeatherApp renders "No weather yet - Tap to refresh",
    // never garbage and never a crash.
    WeatherSnapshot out;
    out.fetchedUtc = 999;                                        // pre-dirty
    out.cur.valid  = true;
    TEST_ASSERT_TRUE(deserializeWeatherCache("{\"v\":1,\"cur\":{},\"days\":[]}", out));
    TEST_ASSERT_EQUAL_INT64(0, out.fetchedUtc);
    TEST_ASSERT_FALSE(out.cur.valid);
    TEST_ASSERT_EQUAL_INT(0, out.dayCount);
}

// ===========================================================================
//  buildOpenMeteoUrl
// ===========================================================================
void test_url_kl_defaults(void) {
    char url[WEATHER_URL_MAX];
    buildOpenMeteoUrl(url, sizeof(url), 3.1390f, 101.6869f, "Asia/Kuala_Lumpur");

    TEST_ASSERT_TRUE(strlen(url) > 0);
    TEST_ASSERT_TRUE(strlen(url) < WEATHER_URL_MAX);
    TEST_ASSERT_NOT_NULL(strstr(url, "https://api.open-meteo.com/v1/forecast?"));
    TEST_ASSERT_NOT_NULL(strstr(url, "latitude=3.1390"));
    TEST_ASSERT_NOT_NULL(strstr(url, "longitude=101.6869"));
    TEST_ASSERT_NOT_NULL(strstr(url, "timezone=Asia%2FKuala_Lumpur"));  // '/' encoded
    TEST_ASSERT_NOT_NULL(strstr(url, "forecast_days=3"));
    TEST_ASSERT_NOT_NULL(strstr(url, "current=temperature_2m"));
    TEST_ASSERT_NOT_NULL(strstr(url, "daily=weather_code"));
}

// NEGATIVE TEST: a too-small caller buffer must yield an EMPTY string (loud
// failure for the sync's url[0] check) — never a truncated URL, never an
// overflow.
void test_url_small_cap_yields_empty(void) {
    char buf[16];
    memset(buf, 0xAA, sizeof(buf));                // poison the buffer
    buildOpenMeteoUrl(buf, sizeof(buf), 3.1390f, 101.6869f, "Asia/Kuala_Lumpur");
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[0]);

    // exact-fit boundary: strlen+1 fits, strlen does not-as-cap
    char full[WEATHER_URL_MAX];
    buildOpenMeteoUrl(full, sizeof(full), 3.1390f, 101.6869f, "Asia/Kuala_Lumpur");
    size_t need = strlen(full) + 1;
    char tight[WEATHER_URL_MAX];
    buildOpenMeteoUrl(tight, need, 3.1390f, 101.6869f, "Asia/Kuala_Lumpur");
    TEST_ASSERT_EQUAL_STRING(full, tight);         // exact fit works
    buildOpenMeteoUrl(tight, need - 1, 3.1390f, 101.6869f, "Asia/Kuala_Lumpur");
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)tight[0]);  // one short -> empty
}

void test_url_null_and_zero_cap_safe(void) {
    buildOpenMeteoUrl(nullptr, 0, 3.0f, 101.0f, "Asia/Kuala_Lumpur");  // no crash
    char buf[8] = { 'x' };
    buildOpenMeteoUrl(buf, 0, 3.0f, 101.0f, "Asia/Kuala_Lumpur");
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[0]); // zero cap: untouched
    buildOpenMeteoUrl(buf, sizeof(buf), 3.0f, 101.0f, nullptr);  // null tz
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[0]);  // too small either way
}

void test_url_encodes_special_chars(void) {
    char url[WEATHER_URL_MAX];
    buildOpenMeteoUrl(url, sizeof(url), 0.0f, 0.0f, "Foo/Bar Baz");
    TEST_ASSERT_NOT_NULL(strstr(url, "timezone=Foo%2FBar%20Baz"));
    TEST_ASSERT_NOT_NULL(strstr(url, "latitude=0.0000"));
}

// ===========================================================================
//  formatTenthsC
// ===========================================================================
void test_format_tenths(void) {
    char buf[16];
    formatTenthsC(251, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("25.1", buf);
    formatTenthsC(-52, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("-5.2", buf);
    formatTenthsC(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0.0", buf);
    formatTenthsC(3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0.3", buf);

    // too-small buffer -> empty string, never an overflow
    char tiny[3];
    tiny[0] = 'z';
    formatTenthsC(251, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)tiny[0]);
}

// ===========================================================================
int main(int, char **) {
    UNITY_BEGIN();
    // parseOpenMeteo
    RUN_TEST(test_parse_happy_path);
    RUN_TEST(test_parse_empty_input);
    RUN_TEST(test_parse_garbage_input);
    RUN_TEST(test_parse_non_object);
    RUN_TEST(test_parse_missing_current);
    RUN_TEST(test_parse_missing_daily);
    RUN_TEST(test_parse_truncated_body_rejected);
    RUN_TEST(test_parse_short_daily_arrays);
    RUN_TEST(test_parse_long_daily_arrays_clamped);
    RUN_TEST(test_parse_humidity_clamped);
    RUN_TEST(test_parse_nonfinite_rejected);
    RUN_TEST(test_parse_negative_temps_and_wind_clamp);
    RUN_TEST(test_parse_inverted_min_max_swapped);
    // glyph bucketing
    RUN_TEST(test_glyph_buckets);
    // cache round-trips
    RUN_TEST(test_cache_roundtrip_full);
    RUN_TEST(test_cache_roundtrip_empty_snapshot);
    RUN_TEST(test_cache_corrupt_input_yields_empty);
    RUN_TEST(test_cache_missing_fields_tolerated);
    RUN_TEST(test_cache_label_truncated);
    RUN_TEST(test_cache_cur_wind_clamped);
    RUN_TEST(test_cache_cur_wind_large_clamped);
    RUN_TEST(test_cache_day_temps_sanitised);
    RUN_TEST(test_cache_day_code_out_of_range);
    RUN_TEST(test_cache_dataless_doc_is_empty_state);
    // URL builder
    RUN_TEST(test_url_kl_defaults);
    RUN_TEST(test_url_small_cap_yields_empty);
    RUN_TEST(test_url_null_and_zero_cap_safe);
    RUN_TEST(test_url_encodes_special_chars);
    // formatting helper
    RUN_TEST(test_format_tenths);
    return UNITY_END();
}
