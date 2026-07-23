// ===========================================================================
//  test/test_pagination/test_pagination.cpp
// ---------------------------------------------------------------------------
//  Host (native) Unity tests for the page-offset math (core/Paginator.h) and
//  the end-offset clamp behavior of the layout core (core/PageLayout.h).
//  Hardware-free: the "where does this page end" decision is a deterministic
//  stub (a fixed page stride), so there is no display, FS or PSRAM dependency.
// ===========================================================================
#include <unity.h>
#include <string>
#include <vector>
#include "core/Paginator.h"
#include "core/PageLayout.h"

// Deterministic page model: every page is exactly K bytes wide, clamped to the
// file size. This stands in for TextReader::layoutPage(off,false) without a
// font -- the boundaries are predictable so index<->offset math is checkable.
static const uint32_t K = 100;
static const uint32_t N = 1000;

static uint32_t endOfPage(uint32_t off) {
    uint32_t next = off + K;
    return next > N ? N : next;
}

// Same 1px-per-char model used by the word-wrap suite, needed for the
// deferred-word / clamp test which drives layoutTextPage directly.
static int measureModel(const std::string &s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != ' ')                       w += 1;
        else if (i > 0 && i + 1 < s.size() &&
                 s[i - 1] != ' ' && s[i + 1] != ' ') w += 1;
    }
    return w;
}

void setUp(void) {}
void tearDown(void) {}

// --- next/prev index<->offset round-trip ----------------------------------
static void test_next_prev_roundtrip(void) {
    core::Paginator p;
    p.openAt(0, N, endOfPage);            // fresh open at the start
    TEST_ASSERT_EQUAL_UINT(0, p.start());
    TEST_ASSERT_EQUAL_INT(0, p.index());

    TEST_ASSERT_TRUE(p.nextPage(endOfPage(p.start())));   // -> page 1
    TEST_ASSERT_EQUAL_UINT(100, p.start());
    TEST_ASSERT_EQUAL_INT(1, p.index());

    TEST_ASSERT_TRUE(p.nextPage(endOfPage(p.start())));   // -> page 2
    TEST_ASSERT_EQUAL_UINT(200, p.start());
    TEST_ASSERT_EQUAL_INT(2, p.index());

    TEST_ASSERT_TRUE(p.prevPage());                       // back to page 1
    TEST_ASSERT_EQUAL_UINT(100, p.start());
    TEST_ASSERT_EQUAL_INT(1, p.index());

    TEST_ASSERT_TRUE(p.prevPage());                       // back to page 0
    TEST_ASSERT_EQUAL_UINT(0, p.start());
    TEST_ASSERT_EQUAL_INT(0, p.index());
}

// --- prevPage at index 0 is a no-op ---------------------------------------
static void test_prevpage_at_start_is_noop(void) {
    core::Paginator p;
    p.openAt(0, N, endOfPage);
    TEST_ASSERT_FALSE(p.prevPage());
    TEST_ASSERT_EQUAL_UINT(0, p.start());
    TEST_ASSERT_EQUAL_INT(0, p.index());
}

// --- nextPage at EOF is a no-op -------------------------------------------
static void test_nextpage_at_eof_is_noop(void) {
    // Short file: exactly N/K == 10 pages? Use a smaller file to hit EOF fast.
    const uint32_t shortN = 250;
    auto endShort = [](uint32_t off) { uint32_t nx = off + K; return nx > 250u ? 250u : nx; };
    core::Paginator p;
    p.openAt(0, shortN, endShort);
    TEST_ASSERT_TRUE(p.nextPage(endShort(p.start())));    // 0   -> 100
    TEST_ASSERT_TRUE(p.nextPage(endShort(p.start())));    // 100 -> 200
    // page starting at 200 ends at 250 == EOF; advancing must be refused.
    TEST_ASSERT_FALSE(p.nextPage(endShort(p.start())));
    TEST_ASSERT_EQUAL_UINT(200, p.start());
}

// --- open() resume-rebuild reproduces sequential boundaries ---------------
// Guards the "stuck page on resume" bug: resuming mid-book must land on the
// same page (and rebuild the same back-history) as paging there from the top.
static void test_resume_rebuild_matches_sequential(void) {
    const uint32_t resume = 350;

    // (a) sequential: open at the top and page forward until the current page
    //     contains the resume offset.
    core::Paginator seq;
    seq.openAt(0, N, endOfPage);
    while (endOfPage(seq.start()) <= resume) seq.nextPage(endOfPage(seq.start()));

    // (b) resume: rebuild straight to the resume offset.
    core::Paginator res;
    res.openAt(resume, N, endOfPage);

    TEST_ASSERT_EQUAL_UINT(seq.start(), res.start());
    TEST_ASSERT_EQUAL_INT(seq.index(), res.index());
    TEST_ASSERT_EQUAL_UINT(300, res.start());             // page [300,400) holds 350
    TEST_ASSERT_EQUAL_INT(3, res.index());

    // The rebuilt back-history is the full sequence of prior boundaries.
    TEST_ASSERT_EQUAL_size_t(4, res.pageCount());
    TEST_ASSERT_EQUAL_UINT(0,   res.offsetAt(0));
    TEST_ASSERT_EQUAL_UINT(100, res.offsetAt(1));
    TEST_ASSERT_EQUAL_UINT(200, res.offsetAt(2));
    TEST_ASSERT_EQUAL_UINT(300, res.offsetAt(3));
}

// --- resume past EOF resets to the first page -----------------------------
static void test_resume_beyond_eof_resets(void) {
    core::Paginator p;
    p.openAt(N + 500, N, endOfPage);      // bogus resume -> clamp to 0
    TEST_ASSERT_EQUAL_UINT(0, p.start());
    TEST_ASSERT_EQUAL_INT(0, p.index());
}

// --- endOffset clamps with no double-counted deferred word ----------------
// A word that does not fit is deferred to the next page. The page end must
// point *at* that word (not past it), and laying the next page out from there
// must consume it exactly once so the two pages tile the input with no gap and
// no overlap.
static void test_end_offset_no_deferred_double_count(void) {
    const std::string raw = "aa bb cc";   // 8 bytes
    auto measure = [](const std::string &s) { return measureModel(s); };
    auto emit    = [](const std::string &) {};

    // Page 1: one line, width budget fits "aa bb" (==5) but not "...cc".
    core::LayoutResult p1 = core::layoutTextPage<std::string>(
        raw.c_str(), (int)raw.size(), /*maxLines=*/1, /*maxW=*/5, measure, emit);
    TEST_ASSERT_EQUAL_UINT(6, p1.consumed);               // ends at the start of "cc"

    // Page 2: lay out from the reported end; it must consume the rest exactly.
    core::LayoutResult p2 = core::layoutTextPage<std::string>(
        raw.c_str() + p1.consumed, (int)raw.size() - (int)p1.consumed,
        /*maxLines=*/1, /*maxW=*/5, measure, emit);
    TEST_ASSERT_EQUAL_UINT(2, p2.consumed);               // "cc"

    TEST_ASSERT_EQUAL_UINT(raw.size(), p1.consumed + p2.consumed);  // tiles exactly
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_next_prev_roundtrip);
    RUN_TEST(test_prevpage_at_start_is_noop);
    RUN_TEST(test_nextpage_at_eof_is_noop);
    RUN_TEST(test_resume_rebuild_matches_sequential);
    RUN_TEST(test_resume_beyond_eof_resets);
    RUN_TEST(test_end_offset_no_deferred_double_count);
    return UNITY_END();
}
