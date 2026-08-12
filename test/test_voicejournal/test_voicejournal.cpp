// ===========================================================================
//  test_voicejournal  —  Unit tests for the Voice Journal pure seam (VJ·R1)
// ===========================================================================
//  Hardware-free tests for the pure seams in src/core/VoiceModel.h:
//    readVoiceQueue          — tolerant voice queue parsing
//    writeVoiceQueue         — queue management (append, truncate oldest)
//    serializeJournalCache   — /journal.json cache writer
//    deserializeJournalCache — /journal.json cache reader (corrupt-file contract)
// ===========================================================================
#include <unity.h>
#include <cstring>
#include <string>
#include "config.h"
#include "core/VoiceModel.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// ===========================================================================
//  readVoiceQueue — happy path
// ===========================================================================
void test_read_empty_input(void) {
    VoiceEntry entries[16];
    int n = readVoiceQueue("", entries, 16);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_read_one_valid_line(void) {
    std::string input = "/voice/20260802-142345.wav\n";
    VoiceEntry entries[16];
    int n = readVoiceQueue(input, entries, 16);
    
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("/voice/20260802-142345.wav", entries[0].path);
    TEST_ASSERT_EQUAL_INT64(0, entries[0].timestampUtc); // default timestamp
    TEST_ASSERT_EQUAL_STRING("", entries[0].title);
}

void test_read_two_lines(void) {
    std::string input = "/voice/20260802-142345.wav\n/voice/20260802-142346.wav\n";
    VoiceEntry entries[16];
    int n = readVoiceQueue(input, entries, 16);
    
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("/voice/20260802-142345.wav", entries[0].path);
    TEST_ASSERT_EQUAL_STRING("/voice/20260802-142346.wav", entries[1].path);
}

void test_read_malformed_no_newline(void) {
    std::string input = "/voice/20260802-142345.wav"; // no newline
    VoiceEntry entries[16];
    int n = readVoiceQueue(input, entries, 16);
    
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("/voice/20260802-142345.wav", entries[0].path);
}

void test_read_malformed_missing_path(void) {
    std::string input = "\n\n"; // empty lines
    VoiceEntry entries[16];
    int n = readVoiceQueue(input, entries, 16);
    
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_read_oversized_path(void) {
    // Create oversized path
    char oversizedPath[128];
    for (int i = 0; i < 127; i++) {
        oversizedPath[i] = 'a';
    }
    oversizedPath[127] = '\0';
    
    std::string input = std::string(oversizedPath) + "\n";
    VoiceEntry entries[16];
    int n = readVoiceQueue(input, entries, 16);
    
    // Should be truncated to WAV_PATH_MAX
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(WAV_PATH_MAX - 1, (int)strlen(entries[0].path));
}

// ===========================================================================
//  writeVoiceQueue — queue management
// ===========================================================================
void test_write_append(void) {
    std::string queue = "";
    bool result = writeVoiceQueue(queue, "/voice/20260802-142345.wav", 1785715200LL);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("/voice/20260802-142345.wav\n", queue.c_str());
}

void test_write_truncate_oldest(void) {
    // Start with full queue
    std::string queue = "";
    for (int i = 0; i < VOICE_QUEUE_MAX_LINES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/voice/20260802-14234%d.wav", i);
        if (i > 0) queue += "\n";
        queue += path;
    }
    queue += "\n";
    
    // Add new entry
    bool result = writeVoiceQueue(queue, "/voice/20260802-142350.wav", 1785715200LL);
    
    TEST_ASSERT_TRUE(result);
    
    // Should have VOICE_QUEUE_MAX_LINES entries, first one removed
    VoiceEntry entries[16];
    int n = readVoiceQueue(queue, entries, 16);
    TEST_ASSERT_EQUAL_INT(VOICE_QUEUE_MAX_LINES, n);
    
    // First entry should be the second one from original
    char expected[64];
    snprintf(expected, sizeof(expected), "/voice/20260802-142341.wav");
    TEST_ASSERT_EQUAL_STRING(expected, entries[0].path);
}

void test_write_full_queue(void) {
    std::string queue = "";
    for (int i = 0; i < VOICE_QUEUE_MAX_LINES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/voice/20260802-14234%d.wav", i);
        if (i > 0) queue += "\n";
        queue += path;
    }
    queue += "\n";
    
    // Add new entry
    bool result = writeVoiceQueue(queue, "/voice/20260802-142350.wav", 1785715200LL);
    
    TEST_ASSERT_TRUE(result);
    
    // Parse and verify
    VoiceEntry entries[16];
    int n = readVoiceQueue(queue, entries, 16);
    TEST_ASSERT_EQUAL_INT(VOICE_QUEUE_MAX_LINES, n);
}

// ===========================================================================
//  serializeJournalCache / deserializeJournalCache — round-trip
// ===========================================================================
void test_cache_roundtrip_full(void) {
    VoiceEntry entries[16];
    
    // Create test entries
    for (int i = 0; i < 3; i++) {
        memset(&entries[i], 0, sizeof(VoiceEntry));
        snprintf(entries[i].title, VOICE_TITLE_MAX, "Entry %d", i);
        entries[i].timestampUtc = 1785715200LL + i * 3600;
        snprintf(entries[i].path, WAV_PATH_MAX, "/voice/20260802-%02d2345.wav", i + 10);
    }
    
    std::string json;
    serializeJournalCache(json, entries, 3);
    
    // Verify JSON structure
    TEST_ASSERT_TRUE(json.find("{\"v\":1") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"entries\":[") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"title\":\"Entry 0\"") != std::string::npos);
    
    // Deserialize
    VoiceEntry out[16];
    int n = deserializeJournalCache(json, out, 16);
    
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("Entry 0", out[0].title);
    TEST_ASSERT_EQUAL_INT64(1785715200LL, out[0].timestampUtc);
    TEST_ASSERT_EQUAL_STRING("/voice/20260802-102345.wav", out[0].path);
}

void test_cache_roundtrip_empty(void) {
    std::string json;
    serializeJournalCache(json, nullptr, 0);
    
    VoiceEntry out[16];
    int n = deserializeJournalCache(json, out, 16);
    
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_cache_corrupt_input(void) {
    std::string corrupt = "{\"v\":1,\"entries\":[{\"title\":\"Test\"}";
    VoiceEntry out[16];
    int n = deserializeJournalCache(corrupt, out, 16);
    
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_cache_missing_fields(void) {
    std::string partial = "{\"v\":1}";
    VoiceEntry out[16];
    int n = deserializeJournalCache(partial, out, 16);
    
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_cache_title_truncation(void) {
    // Create oversized title
    char oversizedTitle[128];
    for (int i = 0; i < 127; i++) {
        oversizedTitle[i] = 'X';
    }
    oversizedTitle[127] = '\0';
    
    VoiceEntry entries[16];
    memset(&entries[0], 0, sizeof(VoiceEntry));
    strncpy(entries[0].title, oversizedTitle, VOICE_TITLE_MAX - 1);
    entries[0].title[VOICE_TITLE_MAX - 1] = '\0';
    entries[0].timestampUtc = 1785715200LL;
    snprintf(entries[0].path, WAV_PATH_MAX, "/voice/20260802-142345.wav");
    
    std::string json;
    serializeJournalCache(json, entries, 1);
    
    VoiceEntry out[16];
    int n = deserializeJournalCache(json, out, 16);
    
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(VOICE_TITLE_MAX - 1, (int)strlen(out[0].title));
}

// ===========================================================================
//  Negative tests
// ===========================================================================
void test_read_voice_queue_null_input(void) {
    VoiceEntry entries[16];
    // Test with null string reference
    std::string empty;
    int n = readVoiceQueue(empty, nullptr, 16);
    TEST_ASSERT_EQUAL_INT(0, n);
    
    n = readVoiceQueue(empty, entries, 0);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_serialize_journal_null_entries(void) {
    std::string json;
    serializeJournalCache(json, nullptr, 5);
    
    // Should produce valid JSON with empty entries array
    TEST_ASSERT_TRUE(json.find("{\"v\":1,\"entries\":[]}") != std::string::npos);
}

void test_deserialize_journal_null_input(void) {
    VoiceEntry entries[16];
    int n = deserializeJournalCache("", entries, 16);
    TEST_ASSERT_EQUAL_INT(0, n);
}

// ===========================================================================
int main(int, char **) {
    UNITY_BEGIN();
    
    // readVoiceQueue tests
    RUN_TEST(test_read_empty_input);
    RUN_TEST(test_read_one_valid_line);
    RUN_TEST(test_read_two_lines);
    RUN_TEST(test_read_malformed_no_newline);
    RUN_TEST(test_read_malformed_missing_path);
    RUN_TEST(test_read_oversized_path);
    
    // writeVoiceQueue tests
    RUN_TEST(test_write_append);
    RUN_TEST(test_write_truncate_oldest);
    RUN_TEST(test_write_full_queue);
    
    // Cache round-trip tests
    RUN_TEST(test_cache_roundtrip_full);
    RUN_TEST(test_cache_roundtrip_empty);
    RUN_TEST(test_cache_corrupt_input);
    RUN_TEST(test_cache_missing_fields);
    RUN_TEST(test_cache_title_truncation);
    
    // Negative tests
    RUN_TEST(test_read_voice_queue_null_input);
    RUN_TEST(test_serialize_journal_null_entries);
    RUN_TEST(test_deserialize_journal_null_input);
    
    return UNITY_END();
}
