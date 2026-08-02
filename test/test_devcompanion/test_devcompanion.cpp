// ===========================================================================
//  test_devcompanion  —  Unit tests for the Dev Companion core seams (DEV·R1)
// ===========================================================================
//  Hardware-free tests for the pure seams in src/core/:
//    RefsIndex.h   : parseRefsIndex        — tolerant refs_index.txt parser
//    GithubModel.h : parseGithubCount      — search/issues total_count
//                    parseGithubCi         — check_runs / workflow_runs -> CiState
//                    serializeGithubCache  — /github.json writer
//                    deserializeGithubCache— /github.json reader (corrupt contract)
//
//  All fixtures are inlined; the tests never touch the network or filesystem.
// ===========================================================================
#include <unity.h>
#include <cstring>
#include <string>
#include "config.h"
#include "core/RefsIndex.h"
#include "core/GithubModel.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

// ===========================================================================
//  parseRefsIndex — happy path
// ===========================================================================
void test_refs_normal(void) {
    const char *txt =
        "pinout.raw|ESP32-S3 Pinout\n"
        "schematic.raw|Board Schematic\n"
        "wiring.raw|Wiring Diagram\n";
    RefsEntry e[REFS_MAX_ENTRIES];
    int n = parseRefsIndex(txt, e, REFS_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("pinout.raw", e[0].file);
    TEST_ASSERT_EQUAL_STRING("ESP32-S3 Pinout", e[0].label);
    TEST_ASSERT_EQUAL_STRING("schematic.raw", e[1].file);
    TEST_ASSERT_EQUAL_STRING("Board Schematic", e[1].label);
    TEST_ASSERT_EQUAL_STRING("wiring.raw", e[2].file);
    TEST_ASSERT_EQUAL_STRING("Wiring Diagram", e[2].label);
}

// ===========================================================================
//  parseRefsIndex — blank lines + whitespace tolerance
// ===========================================================================
void test_refs_blank_lines(void) {
    const char *txt =
        "\n"
        "pinout.raw|Pinout\n"
        "   \n"                 // whitespace-only line -> skipped
        "\n"
        "schematic.raw|Schematic\n"
        "\n";
    RefsEntry e[REFS_MAX_ENTRIES];
    int n = parseRefsIndex(txt, e, REFS_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("pinout.raw", e[0].file);
    TEST_ASSERT_EQUAL_STRING("schematic.raw", e[1].file);
}

void test_refs_crlf_and_field_whitespace(void) {
    // CRLF line endings + padding spaces around both fields must trim cleanly.
    const char *txt = "  pinout.raw  |  ESP32 Pinout  \r\n"
                      "wiring.raw|Wiring\r\n";
    RefsEntry e[REFS_MAX_ENTRIES];
    int n = parseRefsIndex(txt, e, REFS_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("pinout.raw", e[0].file);
    TEST_ASSERT_EQUAL_STRING("ESP32 Pinout", e[0].label);
    TEST_ASSERT_EQUAL_STRING("wiring.raw", e[1].file);
    TEST_ASSERT_EQUAL_STRING("Wiring", e[1].label);
}

// ===========================================================================
//  parseRefsIndex — missing '|' falls back to the filename sans extension
// ===========================================================================
void test_refs_missing_pipe_fallback(void) {
    const char *txt =
        "pinout.raw\n"              // -> label "pinout"
        "board.v2.raw\n"            // -> label "board.v2" (last dot only)
        "readme\n"                  // no extension -> label "readme"
        "labeled.raw|Has Label\n";  // normal line still works alongside
    RefsEntry e[REFS_MAX_ENTRIES];
    int n = parseRefsIndex(txt, e, REFS_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_STRING("pinout.raw", e[0].file);
    TEST_ASSERT_EQUAL_STRING("pinout", e[0].label);
    TEST_ASSERT_EQUAL_STRING("board.v2.raw", e[1].file);
    TEST_ASSERT_EQUAL_STRING("board.v2", e[1].label);
    TEST_ASSERT_EQUAL_STRING("readme", e[2].file);
    TEST_ASSERT_EQUAL_STRING("readme", e[2].label);
    TEST_ASSERT_EQUAL_STRING("Has Label", e[3].label);
}

void test_refs_pipe_with_empty_label_falls_back(void) {
    // "file|" (empty label) must not leave a blank overlay label: stem fallback.
    const char *txt = "pinout.raw|\n";
    RefsEntry e[REFS_MAX_ENTRIES];
    int n = parseRefsIndex(txt, e, REFS_MAX_ENTRIES);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("pinout.raw", e[0].file);
    TEST_ASSERT_EQUAL_STRING("pinout", e[0].label);
}

// ===========================================================================
//  parseRefsIndex — oversized fields are truncated, never overflowed
// ===========================================================================
void test_refs_oversized_truncation(void) {
    // Build a filename longer than REFS_FILE_MAX and a label longer than
    // REFS_LABEL_MAX; both must clamp to cap-1 chars and stay NUL-terminated.
    std::string bigFile(REFS_FILE_MAX + 20, 'a');   // way over the buffer
    bigFile += ".raw";
    std::string bigLabel(REFS_LABEL_MAX + 20, 'L');
    std::string line = bigFile + "|" + bigLabel + "\n";

    RefsEntry e[REFS_MAX_ENTRIES];
    int n = parseRefsIndex(line.c_str(), e, REFS_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(REFS_FILE_MAX - 1, (int)strlen(e[0].file));
    TEST_ASSERT_EQUAL_INT(REFS_LABEL_MAX - 1, (int)strlen(e[0].label));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)e[0].file[REFS_FILE_MAX - 1]);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)e[0].label[REFS_LABEL_MAX - 1]);
}

// ===========================================================================
//  parseRefsIndex — empty input + capacity + null safety
// ===========================================================================
void test_refs_empty_input(void) {
    RefsEntry e[REFS_MAX_ENTRIES];
    TEST_ASSERT_EQUAL_INT(0, parseRefsIndex("", e, REFS_MAX_ENTRIES));
    TEST_ASSERT_EQUAL_INT(0, parseRefsIndex("\n\n\n", e, REFS_MAX_ENTRIES));  // only blanks
}

void test_refs_max_capacity(void) {
    // 20 valid lines but capacity 16 (REFS_MAX_ENTRIES): clamp, no overflow.
    std::string txt;
    for (int i = 0; i < 20; i++) {
        txt += "img" + std::to_string(i) + ".raw|Label " + std::to_string(i) + "\n";
    }
    RefsEntry e[REFS_MAX_ENTRIES];
    int n = parseRefsIndex(txt.c_str(), e, REFS_MAX_ENTRIES);
    TEST_ASSERT_EQUAL_INT(REFS_MAX_ENTRIES, n);
    // the FIRST 16 win (parser stops once full)
    TEST_ASSERT_EQUAL_STRING("img0.raw", e[0].file);
    TEST_ASSERT_EQUAL_STRING("img15.raw", e[REFS_MAX_ENTRIES - 1].file);

    // a smaller caller cap is honoured too
    RefsEntry small[3];
    TEST_ASSERT_EQUAL_INT(3, parseRefsIndex(txt.c_str(), small, 3));
}

// NEGATIVE TEST: null / zero-capacity arguments must yield 0, never crash.
void test_refs_null_and_zero_safe(void) {
    RefsEntry e[REFS_MAX_ENTRIES];
    TEST_ASSERT_EQUAL_INT(0, parseRefsIndex(nullptr, e, REFS_MAX_ENTRIES));
    TEST_ASSERT_EQUAL_INT(0, parseRefsIndex("a.raw|A", nullptr, REFS_MAX_ENTRIES));
    TEST_ASSERT_EQUAL_INT(0, parseRefsIndex("a.raw|A", e, 0));
    TEST_ASSERT_EQUAL_INT(0, parseRefsIndex("a.raw|A", e, -5));
}

// ===========================================================================
//  parseGithubCount — search/issues total_count
// ===========================================================================
void test_count_valid(void) {
    TEST_ASSERT_EQUAL_INT(7, parseGithubCount(
        "{\"total_count\":7,\"incomplete_results\":false,\"items\":[]}"));
    // zero is a valid count (no open PRs / issues)
    TEST_ASSERT_EQUAL_INT(0, parseGithubCount("{\"total_count\":0,\"items\":[]}"));
    // a large count with a populated items array still reads total_count
    TEST_ASSERT_EQUAL_INT(123, parseGithubCount(
        "{\"total_count\":123,\"items\":[{\"id\":1},{\"id\":2}]}"));
}

void test_count_missing_total(void) {
    // structurally valid object but no total_count -> -1
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount("{\"items\":[]}"));
    // non-numeric total_count -> -1
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount("{\"total_count\":\"many\"}"));
    // negative total_count -> -1 (nonsensical)
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount("{\"total_count\":-3}"));
}

// NEGATIVE TEST: malformed / non-object input must fail LOUDLY (-1), never crash.
void test_count_malformed(void) {
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount("this is not json {{{"));
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount("[1,2,3]"));      // array, not object
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount("\"hello\""));    // scalar
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount("null"));
    // truncated mid-token chop of a real response
    std::string full = "{\"total_count\":42,\"items\":[{\"id\":1}]}";
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount(full.substr(0, full.size() / 2)));
}

void test_count_empty(void) {
    TEST_ASSERT_EQUAL_INT(-1, parseGithubCount(""));
}

// ===========================================================================
//  parseGithubCi — check-runs / workflow-runs -> CiState
// ===========================================================================
void test_ci_success(void) {
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"check_runs\":[{\"status\":\"completed\",\"conclusion\":\"success\"}]}")
        == CiState::Success);
    // first (most recent) wins even when later runs differ
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"check_runs\":[{\"conclusion\":\"success\"},{\"conclusion\":\"failure\"}]}")
        == CiState::Success);
}

void test_ci_failure(void) {
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"check_runs\":[{\"status\":\"completed\",\"conclusion\":\"failure\"}]}")
        == CiState::Failure);
    // an unrecognised conclusion (cancelled) maps to Unknown, NOT Failure
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"check_runs\":[{\"status\":\"completed\",\"conclusion\":\"cancelled\"}]}")
        == CiState::Unknown);
}

void test_ci_pending(void) {
    // null conclusion + in_progress -> Pending (the documented case)
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"check_runs\":[{\"status\":\"in_progress\",\"conclusion\":null}]}")
        == CiState::Pending);
    // queued (null conclusion) -> Pending too
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"check_runs\":[{\"status\":\"queued\",\"conclusion\":null}]}")
        == CiState::Pending);
    // a missing conclusion key behaves like null -> Pending
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"check_runs\":[{\"status\":\"in_progress\"}]}")
        == CiState::Pending);
}

void test_ci_empty_runs(void) {
    // parsed document, empty array -> None (no CI configured / no runs)
    TEST_ASSERT_TRUE(parseGithubCi("{\"check_runs\":[]}") == CiState::None);
    // missing array key entirely -> None as well
    TEST_ASSERT_TRUE(parseGithubCi("{\"total_count\":0}") == CiState::None);
}

void test_ci_workflow_runs_fallback(void) {
    // GithubSync fetches /actions/runs which returns "workflow_runs"; the same
    // parser must honour that key with the identical item shape.
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"workflow_runs\":[{\"status\":\"completed\",\"conclusion\":\"success\"}]}")
        == CiState::Success);
    TEST_ASSERT_TRUE(parseGithubCi(
        "{\"workflow_runs\":[{\"status\":\"completed\",\"conclusion\":\"failure\"}]}")
        == CiState::Failure);
    TEST_ASSERT_TRUE(parseGithubCi("{\"workflow_runs\":[]}") == CiState::None);
}

// NEGATIVE TEST: malformed / non-object input must yield Unknown, never crash.
void test_ci_malformed(void) {
    TEST_ASSERT_TRUE(parseGithubCi("") == CiState::Unknown);
    TEST_ASSERT_TRUE(parseGithubCi("{{{ not json") == CiState::Unknown);
    TEST_ASSERT_TRUE(parseGithubCi("[1,2,3]") == CiState::Unknown);
    // first element not an object -> Unknown
    TEST_ASSERT_TRUE(parseGithubCi("{\"check_runs\":[42]}") == CiState::Unknown);
}

// ===========================================================================
//  serialize / deserialize GithubCache round-trips
// ===========================================================================
static GithubRepoStatus mkRepo(const char *name, int prs, int iss,
                               CiState ci, int64_t fet) {
    GithubRepoStatus s;
    githubRepoClear(s);
    strncpy(s.name, name, GITHUB_NAME_MAX - 1);
    s.name[GITHUB_NAME_MAX - 1] = '\0';
    s.openPRs    = prs;
    s.openIssues = iss;
    s.lastCi     = ci;
    s.fetchedUtc = fet;
    return s;
}

void test_cache_roundtrip(void) {
    GithubRepoStatus in[GITHUB_MAX_REPOS] = {
        mkRepo("Adamlamia/esp32-s3-eink-ereader", 2, 5, CiState::Success, INT64_C(1785715200)),
        mkRepo("owner/another", 0, 1, CiState::Failure, INT64_C(1785715100)),
        mkRepo("owner/third", 3, 0, CiState::Pending, INT64_C(1785715000)),
    };
    std::string json;
    serializeGithubCache(json, in, 3, INT64_C(1785715200));

    // compact schema v1 keys are present
    TEST_ASSERT_TRUE(json.find("\"v\":1") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"sync\":1785715200") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"repos\":[") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"name\":\"owner/another\"") != std::string::npos);

    GithubRepoStatus out[GITHUB_MAX_REPOS];
    int64_t sync = 0;
    int n = deserializeGithubCache(json, out, GITHUB_MAX_REPOS, sync);

    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1785715200), sync);
    TEST_ASSERT_EQUAL_STRING("Adamlamia/esp32-s3-eink-ereader", out[0].name);
    TEST_ASSERT_EQUAL_INT(2, out[0].openPRs);
    TEST_ASSERT_EQUAL_INT(5, out[0].openIssues);
    TEST_ASSERT_TRUE(out[0].lastCi == CiState::Success);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1785715200), out[0].fetchedUtc);
    TEST_ASSERT_EQUAL_STRING("owner/another", out[1].name);
    TEST_ASSERT_TRUE(out[1].lastCi == CiState::Failure);
    TEST_ASSERT_TRUE(out[2].lastCi == CiState::Pending);
    TEST_ASSERT_EQUAL_INT(3, out[2].openPRs);
}

void test_cache_empty_array(void) {
    // Zero repos round-trips cleanly (a sync that found no configured repos).
    std::string json;
    serializeGithubCache(json, nullptr, 0, INT64_C(1700000000));

    GithubRepoStatus out[GITHUB_MAX_REPOS];
    int64_t sync = 0;
    int n = deserializeGithubCache(json, out, GITHUB_MAX_REPOS, sync);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_INT64(INT64_C(1700000000), sync);
}

// NEGATIVE TEST (GithubStore::load corrupt-file contract, host-side): a corrupt
// cache must yield 0 repos + sync 0, never a crash and never stale half-data.
void test_cache_corrupt_input(void) {
    GithubRepoStatus out[GITHUB_MAX_REPOS];
    int64_t sync = 12345;                        // pre-dirty to prove the reset
    TEST_ASSERT_EQUAL_INT(0, deserializeGithubCache("{{{ not json", out, GITHUB_MAX_REPOS, sync));
    TEST_ASSERT_EQUAL_INT64(0, sync);

    TEST_ASSERT_EQUAL_INT(0, deserializeGithubCache("", out, GITHUB_MAX_REPOS, sync));
    TEST_ASSERT_EQUAL_INT(0, deserializeGithubCache("[1,2,3]", out, GITHUB_MAX_REPOS, sync));

    // truncated mid-document chop of a real cache is corrupt too
    GithubRepoStatus in[1] = { mkRepo("a/b", 1, 1, CiState::Success, 1700000000) };
    std::string json;
    serializeGithubCache(json, in, 1, 1700000000);
    sync = 999;
    TEST_ASSERT_EQUAL_INT(0, deserializeGithubCache(json.substr(0, json.size() / 2),
                                                    out, GITHUB_MAX_REPOS, sync));
    TEST_ASSERT_EQUAL_INT64(0, sync);
}

void test_cache_sanitises_fields(void) {
    // Oversized name truncated; negative counts clamped to 0; bad ci -> Unknown;
    // longer arrays clamped to GITHUB_MAX_REPOS.
    std::string doc = "{\"v\":1,\"sync\":555,\"repos\":[";
    // 6 repos but capacity is GITHUB_MAX_REPOS (4)
    for (int i = 0; i < 6; i++) {
        if (i) doc += ",";
        doc += "{\"name\":\"";
        doc += std::string(GITHUB_NAME_MAX + 10, 'x');   // oversized name
        doc += "\",\"prs\":-7,\"iss\":-1,\"ci\":99,\"fet\":12}";
    }
    doc += "]}";

    GithubRepoStatus out[GITHUB_MAX_REPOS + 2];
    int64_t sync = 0;
    int n = deserializeGithubCache(doc, out, GITHUB_MAX_REPOS + 2, sync);

    TEST_ASSERT_EQUAL_INT(GITHUB_MAX_REPOS, n);          // clamped to capacity
    TEST_ASSERT_EQUAL_INT64(555, sync);
    TEST_ASSERT_EQUAL_INT(GITHUB_NAME_MAX - 1, (int)strlen(out[0].name));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)out[0].name[GITHUB_NAME_MAX - 1]);
    TEST_ASSERT_EQUAL_INT(0, out[0].openPRs);            // -7 clamps to 0
    TEST_ASSERT_EQUAL_INT(0, out[0].openIssues);         // -1 clamps to 0
    TEST_ASSERT_TRUE(out[0].lastCi == CiState::Unknown); // ci 99 out of range
}

void test_cache_null_and_zero_safe(void) {
    GithubRepoStatus out[GITHUB_MAX_REPOS];
    int64_t sync = 7;
    TEST_ASSERT_EQUAL_INT(0, deserializeGithubCache("{\"v\":1}", nullptr, GITHUB_MAX_REPOS, sync));
    TEST_ASSERT_EQUAL_INT(0, deserializeGithubCache("{\"v\":1}", out, 0, sync));
}

// ===========================================================================
int main(int, char **) {
    UNITY_BEGIN();
    // parseRefsIndex
    RUN_TEST(test_refs_normal);
    RUN_TEST(test_refs_blank_lines);
    RUN_TEST(test_refs_crlf_and_field_whitespace);
    RUN_TEST(test_refs_missing_pipe_fallback);
    RUN_TEST(test_refs_pipe_with_empty_label_falls_back);
    RUN_TEST(test_refs_oversized_truncation);
    RUN_TEST(test_refs_empty_input);
    RUN_TEST(test_refs_max_capacity);
    RUN_TEST(test_refs_null_and_zero_safe);
    // parseGithubCount
    RUN_TEST(test_count_valid);
    RUN_TEST(test_count_missing_total);
    RUN_TEST(test_count_malformed);
    RUN_TEST(test_count_empty);
    // parseGithubCi
    RUN_TEST(test_ci_success);
    RUN_TEST(test_ci_failure);
    RUN_TEST(test_ci_pending);
    RUN_TEST(test_ci_empty_runs);
    RUN_TEST(test_ci_workflow_runs_fallback);
    RUN_TEST(test_ci_malformed);
    // cache round-trips
    RUN_TEST(test_cache_roundtrip);
    RUN_TEST(test_cache_empty_array);
    RUN_TEST(test_cache_corrupt_input);
    RUN_TEST(test_cache_sanitises_fields);
    RUN_TEST(test_cache_null_and_zero_safe);
    return UNITY_END();
}
