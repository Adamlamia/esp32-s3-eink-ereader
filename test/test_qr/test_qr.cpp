// ===========================================================================
//  test_qr  —  Unit tests for the QR Toolkit core seams (QR·R1)
// ===========================================================================
//  Hardware-free tests for the pure seams in src/core/Emvco.h and
//  src/core/QrPayload.h:
//    emvcoCrc16     — CRC16-CCITT (0x1021 / init 0xFFFF) byte-exactness
//    emvcoBuild     — structured fields -> QRPS string with valid tag-63 CRC
//    emvcoParse     — QRPS string -> fields (structure + CRC validated)
//    emvcoSubTag    — template-tag (tag 26) sub-field extraction
//    wifiQrEscape   — zxing WIFI: escaping of \ ; , : "
//    wifiQrBuild    — WIFI:T:WPA / T:nopass payload builder
//    QrEntryList    — bounds, truncation safety, empty-state detection
//
//  All credentials / merchant data below are SYNTHETIC test fixtures — never
//  real WiFi creds or real merchant IDs (secrets live only in src/secrets.h,
//  which is git-ignored and never referenced here).
//
//  The DuitNow-shaped fixture's CRC was cross-computed with an independent
//  Python CRC16-CCITT implementation; the algorithm itself is pinned by the
//  canonical CRC-16/CCITT-FALSE check value (0x29B1 for "123456789").
// ===========================================================================
#include <unity.h>
#include <cstring>
#include "config.h"
#include "core/Emvco.h"
#include "core/QrPayload.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// --- Synthetic DuitNow-shaped fixtures (CRCs cross-checked with Python) --------
// Fields: 00 "01", 01 "12" (dynamic), 26 merchant template (sub-tags 00 GUI /
// 01 proxy ID / 02 reference / 03 purpose), 52 MCC "4812", 53 "458" (MYR),
// 54 amount "15.50", 58 "MY", 59 name, 60 city, 63 CRC.
static const char *FIXTURE_DUITNOW =
    "00020101021226590014MY.GOV.BNM.RPP0111601234567890215MY2026080212345"
    "0303UMM520448125303458540515.505802MY5915KEDAI BUKU ALAM6012KUALA "
    "LUMPUR6304432B";

// Same merchant, static initiation (01 "11"), no amount — the shape built by
// test_build_duitnow_shape; expected output pinned byte-for-byte.
static const char *FIXTURE_BUILD_EXPECTED =
    "00020101021126590014MY.GOV.BNM.RPP0111601234567890215MY2026080212345"
    "0303UMM5204481253034585802MY5915KEDAI BUKU ALAM6012KUALA LUMPUR63045C13";

static const char *TAG26_VALUE =
    "0014MY.GOV.BNM.RPP0111601234567890215MY20260802123450303UMM";

// ===========================================================================
//  emvcoCrc16 — byte-exactness
// ===========================================================================
void test_crc16_canonical_check_value(void) {
    // The defining check value of CRC-16/CCITT-FALSE (poly 0x1021, init
    // 0xFFFF, no reflection, no final XOR). If this passes, the algorithm is
    // byte-exact against the published standard the EMVCo spec references.
    TEST_ASSERT_EQUAL_HEX16(0x29B1, emvcoCrc16("123456789", 9));
}

void test_crc16_empty_and_length_sensitive(void) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, emvcoCrc16("", 0));   // init value, no data
    // CRC must depend on the LENGTH argument, not just the C string: hashing
    // one fewer char of the check string must differ.
    TEST_ASSERT_TRUE(emvcoCrc16("123456789", 9) != emvcoCrc16("123456789", 8));
}

// ===========================================================================
//  emvcoBuild — structured fields -> QRPS string
// ===========================================================================
static void fillDuitNowStatic(EmvcoPayload &p) {
    emvcoPayloadClear(p);
    TEST_ASSERT_TRUE(emvcoAddField(p, "00", "01"));
    TEST_ASSERT_TRUE(emvcoAddField(p, "01", "11"));
    TEST_ASSERT_TRUE(emvcoAddField(p, "26", TAG26_VALUE));
    TEST_ASSERT_TRUE(emvcoAddField(p, "52", "4812"));
    TEST_ASSERT_TRUE(emvcoAddField(p, "53", "458"));
    TEST_ASSERT_TRUE(emvcoAddField(p, "58", "MY"));
    TEST_ASSERT_TRUE(emvcoAddField(p, "59", "KEDAI BUKU ALAM"));
    TEST_ASSERT_TRUE(emvcoAddField(p, "60", "KUALA LUMPUR"));
}

void test_build_duitnow_shape(void) {
    EmvcoPayload p;
    fillDuitNowStatic(p);

    char out[QR_EMVCO_PAYLOAD_MAX];
    TEST_ASSERT_TRUE(emvcoBuild(p, out, sizeof(out)));

    // Byte-exact against the independently CRC-computed fixture: proves the
    // TLV framing ("TTLLVV"), field ordering and the tag-63 CRC trailer.
    TEST_ASSERT_EQUAL_STRING(FIXTURE_BUILD_EXPECTED, out);
    // The trailer is "6304" + 4 uppercase hex digits.
    const size_t len = std::strlen(out);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(out + len - 8, "6304", 4));
    // And the whole thing validates under the parser (CRC self-consistent).
    TEST_ASSERT_TRUE(emvcoIsValid(out));
}

void test_build_appends_valid_crc(void) {
    // Minimal payload: payload-format-indicator only.
    EmvcoPayload p;
    emvcoPayloadClear(p);
    TEST_ASSERT_TRUE(emvcoAddField(p, "00", "01"));

    char out[QR_EMVCO_PAYLOAD_MAX];
    TEST_ASSERT_TRUE(emvcoBuild(p, out, sizeof(out)));

    // "000201" + "6304" + CRC over "0002016304".
    const uint16_t want = emvcoCrc16("0002016304", 10);
    char expect[16];
    std::snprintf(expect, sizeof(expect), "0002016304%04X", (unsigned)want);
    TEST_ASSERT_EQUAL_STRING(expect, out);
}

// ===========================================================================
//  emvcoParse — string -> fields (+ validation)
// ===========================================================================
void test_parse_duitnow_fixture(void) {
    EmvcoPayload p;
    TEST_ASSERT_TRUE(emvcoParse(FIXTURE_DUITNOW, p));

    // Nine top-level fields; the tag-63 trailer is validated, not stored.
    TEST_ASSERT_EQUAL_INT(9, p.count);
    TEST_ASSERT_EQUAL_STRING("00", p.fields[0].tag);
    TEST_ASSERT_EQUAL_STRING("01", p.fields[0].value);
    TEST_ASSERT_EQUAL_STRING("01", p.fields[1].tag);
    TEST_ASSERT_EQUAL_STRING("12", p.fields[1].value);      // dynamic
    TEST_ASSERT_EQUAL_STRING("26", p.fields[2].tag);
    TEST_ASSERT_EQUAL_STRING(TAG26_VALUE, p.fields[2].value);   // verbatim
    TEST_ASSERT_EQUAL_STRING("52", p.fields[3].tag);
    TEST_ASSERT_EQUAL_STRING("4812", p.fields[3].value);
    TEST_ASSERT_EQUAL_STRING("53", p.fields[4].tag);
    TEST_ASSERT_EQUAL_STRING("458", p.fields[4].value);
    TEST_ASSERT_EQUAL_STRING("54", p.fields[5].tag);
    TEST_ASSERT_EQUAL_STRING("15.50", p.fields[5].value);
    TEST_ASSERT_EQUAL_STRING("58", p.fields[6].tag);
    TEST_ASSERT_EQUAL_STRING("MY", p.fields[6].value);
    TEST_ASSERT_EQUAL_STRING("59", p.fields[7].tag);
    TEST_ASSERT_EQUAL_STRING("KEDAI BUKU ALAM", p.fields[7].value);
    TEST_ASSERT_EQUAL_STRING("60", p.fields[8].tag);
    TEST_ASSERT_EQUAL_STRING("KUALA LUMPUR", p.fields[8].value);
}

void test_roundtrip_lossless(void) {
    // parse(fixture) -> build -> byte-identical string.
    EmvcoPayload p;
    TEST_ASSERT_TRUE(emvcoParse(FIXTURE_DUITNOW, p));

    char rebuilt[QR_EMVCO_PAYLOAD_MAX];
    TEST_ASSERT_TRUE(emvcoBuild(p, rebuilt, sizeof(rebuilt)));
    TEST_ASSERT_EQUAL_STRING(FIXTURE_DUITNOW, rebuilt);

    // And the other direction: build -> parse -> build is stable too.
    EmvcoPayload p2;
    TEST_ASSERT_TRUE(emvcoParse(rebuilt, p2));
    char rebuilt2[QR_EMVCO_PAYLOAD_MAX];
    TEST_ASSERT_TRUE(emvcoBuild(p2, rebuilt2, sizeof(rebuilt2)));
    TEST_ASSERT_EQUAL_STRING(rebuilt, rebuilt2);
}

void test_parse_accepts_lowercase_hex_crc(void) {
    // Scanners/issuers occasionally emit lowercase CRC hex; accept it.
    char lower[QR_EMVCO_PAYLOAD_MAX];
    std::strcpy(lower, FIXTURE_DUITNOW);
    const size_t len = std::strlen(lower);
    lower[len - 4] = '4';   // "432B" -> "432b"
    lower[len - 3] = '3';
    lower[len - 2] = '2';
    lower[len - 1] = 'b';
    TEST_ASSERT_TRUE(emvcoIsValid(lower));
}

// ===========================================================================
//  emvcoParse — NEGATIVE tests (guards must fail loudly)
// ===========================================================================
void test_parse_rejects_corrupt_crc(void) {
    // Flip the final CRC digit: 432B -> 432C. A corrupt payment payload must
    // be REJECTED, never half-decoded.
    char bad[QR_EMVCO_PAYLOAD_MAX];
    std::strcpy(bad, FIXTURE_DUITNOW);
    bad[std::strlen(bad) - 1] = 'C';

    EmvcoPayload p;
    TEST_ASSERT_FALSE(emvcoParse(bad, p));
    TEST_ASSERT_FALSE(emvcoIsValid(bad));
    TEST_ASSERT_EQUAL_INT(0, p.count);      // left empty on failure
}

void test_parse_rejects_truncated_value_real(void) {
    EmvcoPayload p;
    char bad[QR_EMVCO_PAYLOAD_MAX];
    std::strcpy(bad, FIXTURE_DUITNOW);
    bad[std::strlen(bad) - 10] = '\0';
    TEST_ASSERT_FALSE(emvcoParse(bad, p));
    TEST_ASSERT_EQUAL_INT(0, p.count);
}

void test_parse_rejects_overlong_declared_length(void) {
    // "5915" (name length 15) corrupted to "5999": claims 99 chars that are
    // not present -> structural reject.
    char bad[QR_EMVCO_PAYLOAD_MAX];
    std::strcpy(bad, FIXTURE_DUITNOW);
    char *needle = std::strstr(bad, "5915KEDAI");
    TEST_ASSERT_NOT_NULL(needle);
    needle[2] = '9';
    needle[3] = '9';
    TEST_ASSERT_FALSE(emvcoIsValid(bad));
}

void test_parse_rejects_missing_crc(void) {
    // A well-formed TLV stream with NO tag 63 is invalid.
    TEST_ASSERT_FALSE(emvcoIsValid("00020101021158 02MY"));
    TEST_ASSERT_FALSE(emvcoIsValid("0002015802MY"));
}

void test_parse_rejects_non_hex_crc(void) {
    char bad[QR_EMVCO_PAYLOAD_MAX];
    std::strcpy(bad, FIXTURE_DUITNOW);
    bad[std::strlen(bad) - 1] = 'Z';        // not a hex digit
    TEST_ASSERT_FALSE(emvcoIsValid(bad));
}

void test_parse_rejects_garbage_and_short(void) {
    EmvcoPayload p;
    TEST_ASSERT_FALSE(emvcoParse(nullptr, p));
    TEST_ASSERT_FALSE(emvcoParse("", p));
    TEST_ASSERT_FALSE(emvcoParse("6304ABCD", p));           // too short
    TEST_ASSERT_FALSE(emvcoParse("00XX0163041234", p));      // non-digit length
    TEST_ASSERT_FALSE(emvcoParse("XX020163041234", p));      // non-digit tag
    TEST_ASSERT_EQUAL_INT(0, p.count);
}

void test_parse_rejects_field_after_crc(void) {
    // Append a stray TLV AFTER the tag-63 trailer -> invalid ordering.
    char bad[QR_EMVCO_PAYLOAD_MAX];
    std::strcpy(bad, FIXTURE_DUITNOW);
    std::strcat(bad, "9902XX");
    TEST_ASSERT_FALSE(emvcoIsValid(bad));
}

// ===========================================================================
//  emvcoBuild / emvcoAddField — NEGATIVE bounds tests
// ===========================================================================
void test_build_rejects_oversize_value(void) {
    // A 100-char value cannot be represented in a 2-digit EMVCo length field:
    // add must refuse it (never truncate a payment payload), while the
    // 99-char boundary value is the largest accepted.
    char huge[101];
    std::memset(huge, 'A', 100);
    huge[100] = '\0';

    EmvcoPayload p;
    emvcoPayloadClear(p);
    TEST_ASSERT_FALSE(emvcoAddField(p, "59", huge));      // 100 chars: refuse
    TEST_ASSERT_EQUAL_INT(0, p.count);

    huge[99] = '\0';                                       // now 99 chars
    TEST_ASSERT_TRUE(emvcoAddField(p, "59", huge));       // boundary: accept
    TEST_ASSERT_EQUAL_INT(1, p.count);

    char out[QR_EMVCO_PAYLOAD_MAX];
    TEST_ASSERT_TRUE(emvcoBuild(p, out, sizeof(out)));
    TEST_ASSERT_TRUE(emvcoIsValid(out));
}

void test_addfield_rejects_bad_tag_and_full_list(void) {
    EmvcoPayload p;
    emvcoPayloadClear(p);
    TEST_ASSERT_FALSE(emvcoAddField(p, "A0", "x"));       // non-digit tag
    TEST_ASSERT_FALSE(emvcoAddField(p, "0", "x"));        // one digit
    TEST_ASSERT_FALSE(emvcoAddField(p, "000", "x"));      // three digits
    TEST_ASSERT_FALSE(emvcoAddField(p, "00", nullptr));   // null value

    // Fill to capacity, then one more must fail loudly.
    for (int i = 0; i < QR_EMVCO_MAX_FIELDS; ++i)
        TEST_ASSERT_TRUE(emvcoAddField(p, "01", "11"));
    TEST_ASSERT_EQUAL_INT(QR_EMVCO_MAX_FIELDS, p.count);
    TEST_ASSERT_FALSE(emvcoAddField(p, "01", "11"));
    TEST_ASSERT_EQUAL_INT(QR_EMVCO_MAX_FIELDS, p.count);  // unchanged
}

void test_build_rejects_small_buffer(void) {
    EmvcoPayload p;
    fillDuitNowStatic(p);
    char tiny[24];
    tiny[0] = 'z';
    TEST_ASSERT_FALSE(emvcoBuild(p, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);       // cleared, no partial payload
    TEST_ASSERT_FALSE(emvcoBuild(p, nullptr, 0));
}

// ===========================================================================
//  emvcoSubTag — template (tag 26) decoding
// ===========================================================================
void test_subtag_extracts_merchant_fields(void) {
    char val[64];
    TEST_ASSERT_TRUE(emvcoSubTag(TAG26_VALUE, "00", val, sizeof(val)));
    TEST_ASSERT_EQUAL_STRING("MY.GOV.BNM.RPP", val);      // DuitNow GUI
    TEST_ASSERT_TRUE(emvcoSubTag(TAG26_VALUE, "01", val, sizeof(val)));
    TEST_ASSERT_EQUAL_STRING("60123456789", val);         // proxy/merchant ID
    TEST_ASSERT_TRUE(emvcoSubTag(TAG26_VALUE, "02", val, sizeof(val)));
    TEST_ASSERT_EQUAL_STRING("MY2026080212345", val);
    TEST_ASSERT_TRUE(emvcoSubTag(TAG26_VALUE, "03", val, sizeof(val)));
    TEST_ASSERT_EQUAL_STRING("UMM", val);
}

void test_subtag_absent_and_malformed(void) {
    char val[64];
    TEST_ASSERT_FALSE(emvcoSubTag(TAG26_VALUE, "09", val, sizeof(val)));  // absent
    TEST_ASSERT_FALSE(emvcoSubTag("0014MY.GOV", "00", val, sizeof(val))); // truncated
    TEST_ASSERT_FALSE(emvcoSubTag(TAG26_VALUE, "00", val, 4));            // cap too small
    TEST_ASSERT_FALSE(emvcoSubTag(nullptr, "00", val, sizeof(val)));
    TEST_ASSERT_FALSE(emvcoSubTag(TAG26_VALUE, "zz", val, sizeof(val)));  // bad tag
}

// ===========================================================================
//  wifiQrBuild / wifiQrEscape — WiFi QR payload formatting
// ===========================================================================
void test_wifi_wpa_format(void) {
    char out[QR_WIFI_QR_MAX];
    TEST_ASSERT_TRUE(wifiQrBuild("TestNet", "hunter2abc", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:TestNet;P:hunter2abc;;", out);
}

void test_wifi_open_network_uses_nopass(void) {
    char out[QR_WIFI_QR_MAX];
    TEST_ASSERT_TRUE(wifiQrBuild("CoffeeShop", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:nopass;S:CoffeeShop;;", out);
    // null password == open network too
    TEST_ASSERT_TRUE(wifiQrBuild("CoffeeShop", nullptr, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:nopass;S:CoffeeShop;;", out);
}

void test_wifi_escapes_all_five_specials_in_ssid(void) {
    // SSID containing every special char: \ ; , : "
    char out[QR_WIFI_QR_MAX];
    TEST_ASSERT_TRUE(wifiQrBuild("a\\b;c,d:e\"f", "plainpass", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(
        "WIFI:T:WPA;S:a\\\\b\\;c\\,d\\:e\\\"f;P:plainpass;;", out);
}

void test_wifi_escapes_all_five_specials_in_password(void) {
    char out[QR_WIFI_QR_MAX];
    TEST_ASSERT_TRUE(wifiQrBuild("PlainSsid", "p\\a;s,w:d\"x", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(
        "WIFI:T:WPA;S:PlainSsid;P:p\\\\a\\;s\\,w\\:d\\\"x;;", out);
}

void test_wifi_escape_standalone(void) {
    char out[64];
    TEST_ASSERT_TRUE(wifiQrEscape("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_TRUE(wifiQrEscape("abc", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    // Exact-fit buffer: ";" needs 3 chars ("\\;" + NUL).
    TEST_ASSERT_TRUE(wifiQrEscape(";", out, 3));
    TEST_ASSERT_EQUAL_STRING("\\;", out);
    TEST_ASSERT_FALSE(wifiQrEscape(";", out, 2));           // one short: refuse
}

void test_wifi_build_rejects_small_buffer(void) {
    char tiny[8];
    tiny[0] = 'z';
    TEST_ASSERT_FALSE(wifiQrBuild("TestNet", "hunter2abc", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);         // cleared, never partial
}

void test_wifi_build_rejects_empty_ssid(void) {
    char out[QR_WIFI_QR_MAX];
    TEST_ASSERT_FALSE(wifiQrBuild("", "hunter2abc", out, sizeof(out)));
    TEST_ASSERT_FALSE(wifiQrBuild(nullptr, "hunter2abc", out, sizeof(out)));
}

// ===========================================================================
//  QrEntryList — bounds, truncation, empty state
// ===========================================================================
void test_list_empty_state_detection(void) {
    QrEntryList l;
    qrListClear(l);
    TEST_ASSERT_TRUE(qrListIsEmpty(l));
    TEST_ASSERT_EQUAL_INT(0, l.count);

    TEST_ASSERT_TRUE(qrListAdd(l, QrKind::Url, "Docs", "https://x.io"));
    TEST_ASSERT_FALSE(qrListIsEmpty(l));
    TEST_ASSERT_EQUAL_INT(1, l.count);

    qrListClear(l);
    TEST_ASSERT_TRUE(qrListIsEmpty(l));
}

void test_list_bounds_at_max_entries(void) {
    QrEntryList l;
    qrListClear(l);
    for (int i = 0; i < QR_MAX_ENTRIES; ++i) {
        char label[16];
        std::snprintf(label, sizeof(label), "QR %d", i);
        TEST_ASSERT_TRUE(qrListAdd(l, QrKind::Text, label, "payload"));
    }
    TEST_ASSERT_EQUAL_INT(QR_MAX_ENTRIES, l.count);
    // One past capacity: rejected loudly, count unchanged.
    TEST_ASSERT_FALSE(qrListAdd(l, QrKind::Text, "overflow", "payload"));
    TEST_ASSERT_EQUAL_INT(QR_MAX_ENTRIES, l.count);
    // Existing entries untouched by the rejected add.
    TEST_ASSERT_EQUAL_STRING("QR 0", l.items[0].label);
    TEST_ASSERT_EQUAL_STRING("QR 7", l.items[QR_MAX_ENTRIES - 1].label);
}

void test_list_truncation_is_safe(void) {
    QrEntryList l;
    qrListClear(l);

    char longLabel[QR_LABEL_MAX + 40];
    char longPayload[QR_PAYLOAD_MAX + 64];
    std::memset(longLabel, 'L', sizeof(longLabel) - 1);
    longLabel[sizeof(longLabel) - 1] = '\0';
    std::memset(longPayload, 'P', sizeof(longPayload) - 1);
    longPayload[sizeof(longPayload) - 1] = '\0';

    TEST_ASSERT_TRUE(qrListAdd(l, QrKind::Text, longLabel, longPayload));
    const QrEntry &e = l.items[0];
    // Truncated to buffer capacity minus the NUL, and NUL-terminated.
    TEST_ASSERT_EQUAL_INT((int)(QR_LABEL_MAX - 1), (int)std::strlen(e.label));
    TEST_ASSERT_EQUAL_INT((int)(QR_PAYLOAD_MAX - 1), (int)std::strlen(e.payload));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)e.label[QR_LABEL_MAX - 1]);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)e.payload[QR_PAYLOAD_MAX - 1]);
}

void test_list_add_wifi_entry(void) {
    QrEntryList l;
    qrListClear(l);
    TEST_ASSERT_TRUE(qrListAddWifi(l, "HomeNet", "syntheticPass1", "Wi-Fi"));
    TEST_ASSERT_EQUAL_INT(1, l.count);
    TEST_ASSERT_TRUE(l.items[0].kind == QrKind::Wifi);
    TEST_ASSERT_EQUAL_STRING("Wi-Fi", l.items[0].label);
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:HomeNet;P:syntheticPass1;;",
                             l.items[0].payload);

    // Empty creds -> no entry added (fails loudly, empty state preserved).
    TEST_ASSERT_FALSE(qrListAddWifi(l, "", "", "Wi-Fi"));
    TEST_ASSERT_EQUAL_INT(1, l.count);
}

void test_list_kind_captions(void) {
    TEST_ASSERT_EQUAL_STRING("URL",            qrKindCaption(QrKind::Url));
    TEST_ASSERT_EQUAL_STRING("Wi-Fi network",  qrKindCaption(QrKind::Wifi));
    TEST_ASSERT_EQUAL_STRING("DuitNow payment", qrKindCaption(QrKind::Payment));
    TEST_ASSERT_EQUAL_STRING("Text",           qrKindCaption(QrKind::Text));
}

// ===========================================================================
//  QR-R2 REGRESSION tests - each FAILS without its fix, PASSES with it
// ===========================================================================
// R2-1: emvcoBuild must REFUSE a caller-supplied tag-63 field. The CRC trailer
// is auto-generated; a duplicate 63 yields a payload emvcoParse rejects,
// breaking the lossless round-trip. (Fails without the QR-R2 guard: build
// would "succeed" but emit an unparseable duplicate-trailer string.)
void test_build_rejects_caller_supplied_crc_tag(void) {
    EmvcoPayload p;
    emvcoPayloadClear(p);
    TEST_ASSERT_TRUE(emvcoAddField(p, "00", "01"));
    TEST_ASSERT_TRUE(emvcoAddField(p, "63", "ABCD"));   // caller must NOT add 63

    char out[QR_EMVCO_PAYLOAD_MAX];
    out[0] = 'z';
    TEST_ASSERT_FALSE(emvcoBuild(p, out, sizeof(out)));  // refuse loudly
    TEST_ASSERT_EQUAL_STRING("", out);                   // cleared, no partial

    // Sanity: the SAME payload minus the rogue 63 builds + round-trips fine,
    // so the refusal is specifically about the duplicate CRC trailer.
    EmvcoPayload q;
    emvcoPayloadClear(q);
    TEST_ASSERT_TRUE(emvcoAddField(q, "00", "01"));
    TEST_ASSERT_TRUE(emvcoBuild(q, out, sizeof(out)));
    TEST_ASSERT_TRUE(emvcoIsValid(out));
}

// R2-2: a WiFi payload is NEVER silently truncated when added to the carousel.
// wifiQrBuild caps its output at QR_WIFI_QR_MAX-1 and qrListAddWifi relies on
// QR_WIFI_QR_MAX <= QR_PAYLOAD_MAX (compile-time pinned in QrPayload.h) so the
// entry buffer always holds the FULL string. This test drives the assembled
// payload to exactly QR_WIFI_QR_MAX-1 (191) chars - the largest wifiQrBuild can
// emit - and asserts the stored entry is byte-complete (len == 191, ";;"
// terminator intact, escaped password present). It would FAIL if a future
// change let qrListAdd truncate a WiFi credential (len < 191 / cut tail) - the
// silent-mangle hazard the QR-R2 review checked for. See QR-REVIEW-QR-R2.md M1.
void test_list_add_wifi_payload_never_truncated(void) {
    QrEntryList l;
    qrListClear(l);

    // Assembled = "WIFI:T:WPA;S:"(13) + escSsid + ";P:"(3) + escPass + ";;"(2).
    // SSID = 80 ';' -> 160 escaped; pass = 6 ';' + 'x' -> 13 escaped.
    // 13 + 160 + 3 + 13 + 2 = 191 == QR_WIFI_QR_MAX-1: the max emittable size.
    char ssid[81], pass[8];
    std::memset(ssid, ';', 80); ssid[80] = '\0';
    std::memset(pass, ';', 6);  pass[6] = 'x'; pass[7] = '\0';

    TEST_ASSERT_TRUE(qrListAddWifi(l, ssid, pass, "Wi-Fi"));
    TEST_ASSERT_EQUAL_INT(1, l.count);

    const char *stored = l.items[0].payload;
    const size_t len = std::strlen(stored);
    // Stored in FULL: exact assembled length, proper terminator, escaped
    // password present - i.e. nothing was cut by qrListAdd.
    TEST_ASSERT_EQUAL_INT(191, (int)len);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(stored, "WIFI:T:WPA;S:", 13));
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(stored + len - 2, ";;", 2));
    TEST_ASSERT_NOT_NULL(std::strstr(stored, ";P:"));
}

// ===========================================================================
int main(int, char **) {
    UNITY_BEGIN();
    // CRC
    RUN_TEST(test_crc16_canonical_check_value);
    RUN_TEST(test_crc16_empty_and_length_sensitive);
    // build
    RUN_TEST(test_build_duitnow_shape);
    RUN_TEST(test_build_appends_valid_crc);
    // parse (happy)
    RUN_TEST(test_parse_duitnow_fixture);
    RUN_TEST(test_roundtrip_lossless);
    RUN_TEST(test_parse_accepts_lowercase_hex_crc);
    // parse (NEGATIVE — guards fail loudly)
    RUN_TEST(test_parse_rejects_corrupt_crc);
    RUN_TEST(test_parse_rejects_truncated_value_real);
    RUN_TEST(test_parse_rejects_overlong_declared_length);
    RUN_TEST(test_parse_rejects_missing_crc);
    RUN_TEST(test_parse_rejects_non_hex_crc);
    RUN_TEST(test_parse_rejects_garbage_and_short);
    RUN_TEST(test_parse_rejects_field_after_crc);
    // build / add bounds (NEGATIVE)
    RUN_TEST(test_build_rejects_oversize_value);
    RUN_TEST(test_addfield_rejects_bad_tag_and_full_list);
    RUN_TEST(test_build_rejects_small_buffer);
    // sub-tags
    RUN_TEST(test_subtag_extracts_merchant_fields);
    RUN_TEST(test_subtag_absent_and_malformed);
    // WiFi payload
    RUN_TEST(test_wifi_wpa_format);
    RUN_TEST(test_wifi_open_network_uses_nopass);
    RUN_TEST(test_wifi_escapes_all_five_specials_in_ssid);
    RUN_TEST(test_wifi_escapes_all_five_specials_in_password);
    RUN_TEST(test_wifi_escape_standalone);
    RUN_TEST(test_wifi_build_rejects_small_buffer);
    RUN_TEST(test_wifi_build_rejects_empty_ssid);
    // entry model
    RUN_TEST(test_list_empty_state_detection);
    RUN_TEST(test_list_bounds_at_max_entries);
    RUN_TEST(test_list_truncation_is_safe);
    RUN_TEST(test_list_add_wifi_entry);
    RUN_TEST(test_list_kind_captions);
    // QR-R2 regressions
    RUN_TEST(test_build_rejects_caller_supplied_crc_tag);
    RUN_TEST(test_list_add_wifi_payload_never_truncated);
    return UNITY_END();
}
