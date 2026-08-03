// ===========================================================================
//  core/VoiceModel.h  —  Voice Journal pure seam (VJ·R1)
// ===========================================================================
//  Header-only, heap-free model for the Voice Journal feature:
//    - WAV queue management (/voice_queue.txt)
//    - Journal entry serialization (/journal.json)
//
//  All buffers sized by config.h constants (no heap, no HAL, host-testable).
//  Tolerant parsing: empty input → 0, malformed lines → skip, oversized paths → truncate.
//  Queue management: append-only; if queue exceeds VOICE_QUEUE_MAX_LINES, overwrite oldest.
//  Serialization: compact JSON schema { v: 1, entries: [ { title: "...", ts: ..., path: "..." } ] }
// ===========================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include "config.h"

namespace core {

// --- Data types -------------------------------------------------------------
struct VoiceEntry {
    char     title[VOICE_TITLE_MAX];      // "Meeting notes", truncated from transcription
    int64_t  timestampUtc;                // UTC epoch seconds when recorded
    char     path[VOICE_PATH_MAX];        // "/voice/20260802-142345.wav"
};

// --- Queue management ---------------------------------------------------------
// One line per queued WAV file ("/voice/20260802-142345.wav\n") in /voice_queue.txt.
// Returns count written (0..maxOut).
int readVoiceQueue(const std::string &in, VoiceEntry *out, int maxOut);

// Write a new entry to the queue (append), or truncate if full. Returns true on success.
bool writeVoiceQueue(const std::string &in, const char *wavPath, int64_t timestampUtc);

// --- Journal entry serialization ----------------------------------------------
// Serialize a VoiceEntry[] into a compact JSON document for /journal.json.
// Schema: { v: 1, entries: [ { title: "...", ts: 1785715200, path: "/voice/..." } ] }
void serializeJournalCache(std::string &out, const VoiceEntry *entries, int n);

// Deserialize /journal.json into entries[]. Returns count written (0..maxOut).
int deserializeJournalCache(const std::string &in, VoiceEntry *entries, int maxOut);

} // namespace core
