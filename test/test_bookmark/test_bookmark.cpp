// ===========================================================================
//  test/test_bookmark/test_bookmark.cpp
// ---------------------------------------------------------------------------
//  Host (native) Unity tests for the bookmark / resume JSON logic
//  (core/BookmarkStore.h). The storage boundary is abstracted to a plain
//  std::string that stands in for the on-disk JSON file (the "fake in-memory
//  FS"), so there is no LittleFS/SD dependency. addBookmark writes createdAt=0
//  (device has no RTC/NTP), so there is no time/RNG to freeze.
// ===========================================================================
#include <unity.h>
#include <string>
#include <vector>
#include "core/BookmarkStore.h"

// A fresh, valid default document -- the state BookmarkManager::_load lands on
// for a first boot or a missing file.
static std::string freshDoc() {
    std::string j;
    core::normalize(j);      // empty -> default "{\"lastOpened\":\"\",\"books\":{}}"
    return j;
}

void setUp(void) {}
void tearDown(void) {}

// --- last position round-trip; unknown book reads 0 -----------------------
static void test_last_position_roundtrip(void) {
    std::string j = freshDoc();
    core::setLastPosition(j, "/books/a.txt", 12345);
    TEST_ASSERT_EQUAL_UINT(12345, core::getLastPosition(j, "/books/a.txt"));
}

static void test_last_position_unknown_book_is_zero(void) {
    std::string j = freshDoc();
    TEST_ASSERT_EQUAL_UINT(0, core::getLastPosition(j, "/books/never-seen.txt"));
}

// --- addBookmark -> listBookmarks matches offset/label --------------------
static void test_add_bookmark_lists_offset_and_label(void) {
    std::string j = freshDoc();
    core::addBookmark(j, "/books/a.txt", 42, "Chapter 1");
    core::addBookmark(j, "/books/a.txt", 999, "The End");

    std::vector<core::BookmarkRec> marks = core::listBookmarks(j, "/books/a.txt");
    TEST_ASSERT_EQUAL_size_t(2, marks.size());
    TEST_ASSERT_EQUAL_UINT(42, marks[0].offset);
    TEST_ASSERT_EQUAL_STRING("Chapter 1", marks[0].label.c_str());
    TEST_ASSERT_EQUAL_UINT(0, marks[0].createdAt);          // deterministic
    TEST_ASSERT_EQUAL_UINT(999, marks[1].offset);
    TEST_ASSERT_EQUAL_STRING("The End", marks[1].label.c_str());
}

// --- listBookmarks for a book with none -> empty --------------------------
static void test_list_bookmarks_empty_for_unknown(void) {
    std::string j = freshDoc();
    std::vector<core::BookmarkRec> marks = core::listBookmarks(j, "/books/none.txt");
    TEST_ASSERT_EQUAL_size_t(0, marks.size());
}

// --- removeBookmark respects bounds ---------------------------------------
static void test_remove_bookmark_bounds(void) {
    std::string j = freshDoc();
    core::addBookmark(j, "/books/a.txt", 10, "one");
    core::addBookmark(j, "/books/a.txt", 20, "two");

    core::removeBookmark(j, "/books/a.txt", 5);             // out of range -> no-op
    TEST_ASSERT_EQUAL_size_t(2, core::listBookmarks(j, "/books/a.txt").size());

    core::removeBookmark(j, "/books/a.txt", 0);             // remove first
    std::vector<core::BookmarkRec> marks = core::listBookmarks(j, "/books/a.txt");
    TEST_ASSERT_EQUAL_size_t(1, marks.size());
    TEST_ASSERT_EQUAL_UINT(20, marks[0].offset);            // "two" survives
    TEST_ASSERT_EQUAL_STRING("two", marks[0].label.c_str());
}

// --- last-opened book round-trip ------------------------------------------
static void test_last_opened_book_roundtrip(void) {
    std::string j = freshDoc();
    TEST_ASSERT_EQUAL_STRING("", core::getLastOpenedBook(j).c_str());   // default
    core::setLastOpenedBook(j, "/books/pocket-haiku.txt");
    TEST_ASSERT_EQUAL_STRING("/books/pocket-haiku.txt",
                             core::getLastOpenedBook(j).c_str());
}

// --- empty / corrupt JSON loads defaults without crashing -----------------
static void test_empty_json_normalizes_to_default(void) {
    std::string j;                          // empty "file"
    core::normalize(j);
    TEST_ASSERT_EQUAL_STRING(core::emptyBookmarkDoc(), j.c_str());
    TEST_ASSERT_EQUAL_UINT(0, core::getLastPosition(j, "/books/a.txt"));
}

static void test_corrupt_json_reads_defaults(void) {
    // A truncated / malformed buffer must not crash: deserialize fails, the doc
    // stays empty, and every accessor falls back to its documented default.
    std::string j = "{ this is not valid json ";
    TEST_ASSERT_EQUAL_UINT(0, core::getLastPosition(j, "/books/a.txt"));
    TEST_ASSERT_EQUAL_STRING("", core::getLastOpenedBook(j).c_str());
    TEST_ASSERT_EQUAL_size_t(0, core::listBookmarks(j, "/books/a.txt").size());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_last_position_roundtrip);
    RUN_TEST(test_last_position_unknown_book_is_zero);
    RUN_TEST(test_add_bookmark_lists_offset_and_label);
    RUN_TEST(test_list_bookmarks_empty_for_unknown);
    RUN_TEST(test_remove_bookmark_bounds);
    RUN_TEST(test_last_opened_book_roundtrip);
    RUN_TEST(test_empty_json_normalizes_to_default);
    RUN_TEST(test_corrupt_json_reads_defaults);
    return UNITY_END();
}
