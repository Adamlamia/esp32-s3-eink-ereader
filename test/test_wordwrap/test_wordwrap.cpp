// ===========================================================================
//  test/test_wordwrap/test_wordwrap.cpp
// ---------------------------------------------------------------------------
//  Host (native) Unity tests for the word-wrap / layout core (core/PageLayout.h)
//  and the UTF-8 -> ASCII mapping (core/TextTransform.h). Hardware-free: the
//  pixel measurement is a deterministic stub, so there is no font, display or
//  PSRAM dependency and no time/RNG to freeze.
// ===========================================================================
#include <unity.h>
#include <string>
#include <cstring>
#include <vector>
#include "core/PageLayout.h"
#include "core/TextTransform.h"

// Deterministic width model (no real font): 1 px per visible (non-space) char,
// plus 1 px for each space that sits *between* two non-space chars. A leading,
// trailing or standalone space contributes 0 -- this mirrors the real font,
// where a lone space measures ~0 ink but an inter-word gap still advances the
// cursor. Because that gap only exists in the *composed* string, a naive
// "sum each word + a space" estimate under-counts; these tests therefore pass
// only if the algorithm measures the composed line (the documented rationale).
static int measureModel(const std::string &s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != ' ')                       w += 1;
        else if (i > 0 && i + 1 < s.size() &&
                 s[i - 1] != ' ' && s[i + 1] != ' ') w += 1;   // interior space
    }
    return w;
}

struct Layout {
    std::vector<std::string> lines;   // committed lines (blank ones included)
    core::LayoutResult       res;
};

static Layout run(const std::string &raw, int maxLines, int maxW) {
    Layout out;
    auto measure = [](const std::string &s) { return measureModel(s); };
    auto emit    = [&](const std::string &line) { out.lines.push_back(line); };
    out.res = core::layoutTextPage<std::string>(
        raw.c_str(), (int)raw.size(), maxLines, maxW, measure, emit);
    return out;
}

void setUp(void) {}
void tearDown(void) {}

// --- Composed-line measurement --------------------------------------------
// "aa bb" composes to width 5 (4 letters + 1 interior space). A naive sum of
// the two word widths is only 4, which would NOT wrap at maxW=4; the composed
// measurement (5 > 4) does. Wrapping here proves the composed line is measured.
static void test_composed_measurement_catches_undercount(void) {
    Layout L = run("aa bb", 10, 4);
    TEST_ASSERT_EQUAL_INT(2, (int)L.lines.size());
    TEST_ASSERT_EQUAL_STRING("aa", L.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("bb", L.lines[1].c_str());
}

// --- Exact-fit boundary: == maxW does not wrap, one more unit does ----------
static void test_exact_fit_does_not_wrap_plus_one_does(void) {
    // width("aa bb") == 5: fits exactly at maxW=5 (strict '>' comparison).
    Layout fit = run("aa bb", 10, 5);
    TEST_ASSERT_EQUAL_INT(1, (int)fit.lines.size());
    TEST_ASSERT_EQUAL_STRING("aa bb", fit.lines[0].c_str());

    // One unit less budget (or one more char of content) forces the wrap.
    Layout tight = run("aa bb", 10, 4);
    TEST_ASSERT_EQUAL_INT(2, (int)tight.lines.size());

    // width("aa bbc") == 6: one over maxW=5 -> wraps.
    Layout over = run("aa bbc", 10, 5);
    TEST_ASSERT_EQUAL_INT(2, (int)over.lines.size());
    TEST_ASSERT_EQUAL_STRING("aa", over.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("bbc", over.lines[1].c_str());
}

// --- \n forces a break -----------------------------------------------------
static void test_newline_forces_break(void) {
    Layout L = run("a\nb", 10, 100);
    TEST_ASSERT_EQUAL_INT(2, (int)L.lines.size());
    TEST_ASSERT_EQUAL_STRING("a", L.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("b", L.lines[1].c_str());
}

// --- \r is ignored (CRLF behaves like LF, no extra blank line) -------------
static void test_carriage_return_ignored(void) {
    Layout crlf = run("a\r\nb", 10, 100);
    Layout lf   = run("a\nb",  10, 100);
    TEST_ASSERT_EQUAL_INT((int)lf.lines.size(), (int)crlf.lines.size());
    TEST_ASSERT_EQUAL_STRING(lf.lines[0].c_str(), crlf.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING(lf.lines[1].c_str(), crlf.lines[1].c_str());
}

// --- Runs of spaces collapse to a single advance ---------------------------
static void test_space_runs_collapse(void) {
    Layout many = run("a    b", 10, 100);
    Layout one  = run("a b",    10, 100);
    TEST_ASSERT_EQUAL_INT(1, (int)many.lines.size());
    TEST_ASSERT_EQUAL_STRING("a b", many.lines[0].c_str());   // exactly one space
    TEST_ASSERT_EQUAL_STRING(one.lines[0].c_str(), many.lines[0].c_str());
}

// --- Over-wide single token (review N2): placed as-is, not broken ----------
// The wrap check is guarded by line.length(), so a token wider than maxW that
// starts a fresh line is emitted unbroken (documented behavior, not a fix).
static void test_overwide_single_token_not_broken(void) {
    Layout L = run("aaaaaa", 10, 3);          // width 6, maxW 3
    TEST_ASSERT_EQUAL_INT(1, (int)L.lines.size());
    TEST_ASSERT_EQUAL_STRING("aaaaaa", L.lines[0].c_str());
    TEST_ASSERT_TRUE(measureModel(L.lines[0]) > 3);
}

// --- toAscii: smart punctuation -> ASCII, other multibyte dropped ----------
static void test_toascii_maps_smart_punctuation(void) {
    using core::toAscii;
    TEST_ASSERT_EQUAL_STRING("'", toAscii<std::string>("\xE2\x80\x98").c_str()); // '
    TEST_ASSERT_EQUAL_STRING("'", toAscii<std::string>("\xE2\x80\x99").c_str()); // '
    TEST_ASSERT_EQUAL_STRING("\"", toAscii<std::string>("\xE2\x80\x9C").c_str()); // "
    TEST_ASSERT_EQUAL_STRING("\"", toAscii<std::string>("\xE2\x80\x9D").c_str()); // "
    TEST_ASSERT_EQUAL_STRING("-", toAscii<std::string>("\xE2\x80\x93").c_str()); // en dash
    TEST_ASSERT_EQUAL_STRING("-", toAscii<std::string>("\xE2\x80\x94").c_str()); // em dash
    TEST_ASSERT_EQUAL_STRING("...", toAscii<std::string>("\xE2\x80\xA6").c_str()); // ellipsis
}

static void test_toascii_drops_other_multibyte(void) {
    using core::toAscii;
    TEST_ASSERT_EQUAL_STRING("", toAscii<std::string>("\xC3\xA9").c_str());       // é (2-byte)
    TEST_ASSERT_EQUAL_STRING("", toAscii<std::string>("\xE2\x82\xAC").c_str());   // € (3-byte)
    // Mixed: smart-quoted "Hi", em dash, dropped é, ellipsis -> "\"Hi\"-..."
    std::string in = "\xE2\x80\x9C" "Hi" "\xE2\x80\x9D" "\xE2\x80\x94" "\xC3\xA9" "\xE2\x80\xA6";
    TEST_ASSERT_EQUAL_STRING("\"Hi\"-...", toAscii<std::string>(in).c_str());
}

static void test_toascii_leaves_ascii_intact(void) {
    using core::toAscii;
    const char *ascii = "Hello, world! 123 (a-b)";
    std::string out = toAscii<std::string>(std::string(ascii));
    TEST_ASSERT_EQUAL_STRING(ascii, out.c_str());
    TEST_ASSERT_EQUAL_UINT(strlen(ascii), (unsigned)out.size());  // byte offsets intact
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_composed_measurement_catches_undercount);
    RUN_TEST(test_exact_fit_does_not_wrap_plus_one_does);
    RUN_TEST(test_newline_forces_break);
    RUN_TEST(test_carriage_return_ignored);
    RUN_TEST(test_space_runs_collapse);
    RUN_TEST(test_overwide_single_token_not_broken);
    RUN_TEST(test_toascii_maps_smart_punctuation);
    RUN_TEST(test_toascii_drops_other_multibyte);
    RUN_TEST(test_toascii_leaves_ascii_intact);
    return UNITY_END();
}
