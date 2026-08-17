// ===========================================================================
//  core/VoiceModel.cpp  —  Voice Journal pure seam implementation (VJ·R1)
// ===========================================================================
//  Implementation of the pure seam functions declared in VoiceModel.h.
//  All buffers sized by config.h constants (no heap, no HAL, host-testable).
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include "config.h"
#include "VoiceModel.h"

namespace core {

// --- Queue management ---------------------------------------------------------

// Helper to trim whitespace from end of string
static void trimTrailingWhitespace(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

// Helper to extract path from line (trimming whitespace and newlines)
static bool extractPath(const char *line, char *out, size_t outSize) {
    // Find first non-whitespace character
    const char *start = line;
    while (*start == ' ' || *start == '\t') start++;
    
    // Find end of line (before newline)
    const char *end = start;
    while (*end && *end != '\n' && *end != '\r') end++;
    
    size_t len = end - start;
    if (len == 0) return false;
    
    // Copy and null-terminate
    if (len >= outSize) len = outSize - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    
    return true;
}

// Read voice queue file content and parse into VoiceEntry array
int readVoiceQueue(const std::string &in, VoiceEntry *out, int maxOut) {
    if (!out || maxOut <= 0) return 0;
    
    int count = 0;
    const char *p = in.c_str();
    
    while (*p && count < maxOut) {
        // Skip leading whitespace
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        
        // Find end of line
        const char *lineStart = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        
        // Extract path from this line
        char path[VOICE_PATH_MAX];
        if (extractPath(lineStart, path, sizeof(path))) {
            // Create entry with empty title and current timestamp
            VoiceEntry entry;
            memset(&entry, 0, sizeof(entry));
            strncpy(entry.path, path, VOICE_PATH_MAX - 1);
            entry.path[VOICE_PATH_MAX - 1] = '\0';
            
            // Set default timestamp (0 means not set)
            entry.timestampUtc = 0;
            
            // Title is empty for now (will be filled from backend response)
            entry.title[0] = '\0';
            
            out[count++] = entry;
        }
        
        // Skip to next line
        if (*p) p++;
        if (*p == '\r') p++;
    }
    
    return count;
}

// Write a new entry to the queue (append), or truncate if full
bool writeVoiceQueue(std::string &in, const char *wavPath, int64_t timestampUtc) {
    if (!wavPath) return false;
    
    // Parse existing queue
    VoiceEntry entries[VOICE_QUEUE_MAX_LINES];
    int n = readVoiceQueue(in, entries, VOICE_QUEUE_MAX_LINES);
    
    // If queue is full, shift all entries left (remove oldest)
    if (n >= VOICE_QUEUE_MAX_LINES) {
        for (int i = 0; i < n - 1; i++) {
            entries[i] = entries[i + 1];
        }
        n--;
    }
    
    // Add new entry at the end
    VoiceEntry newEntry;
    memset(&newEntry, 0, sizeof(newEntry));
    strncpy(newEntry.path, wavPath, VOICE_PATH_MAX - 1);
    newEntry.path[VOICE_PATH_MAX - 1] = '\0';
    newEntry.timestampUtc = timestampUtc;
    newEntry.title[0] = '\0';
    
    entries[n] = newEntry;
    n++;
    
    // Build new queue content
    std::string result;
    for (int i = 0; i < n; i++) {
        result += entries[i].path;
        result += "\n";
    }
    
    // Write the new queue content back in place (caller persists it).
    in = result;
    return true;
}

// --- Journal entry serialization ----------------------------------------------

void serializeJournalCache(std::string &out, const VoiceEntry *entries, int n) {
    if (!entries || n < 0) n = 0;
    if (n > VOICE_QUEUE_MAX_LINES) n = VOICE_QUEUE_MAX_LINES;
    
    // Start building JSON
    out.clear();
    out += "{\"v\":1,\"entries\":[";
    
    for (int i = 0; i < n; i++) {
        if (i > 0) out += ",";
        
        out += "{\"title\":\"";
        // Escape quotes in title
        const char *title = entries[i].title;
        while (*title) {
            if (*title == '\'') out += "\\'";
            else if (*title == '\"') out += "\\\"";
            else if (*title == '\\') out += "\\\\";
            else if (*title == '\n') out += "\\n";
            else if (*title == '\r') out += "\\r";
            else out += *title;
            title++;
        }
        out += "\",\"ts\":\"";
        
        // Format timestamp as string
        char tsStr[32];
        snprintf(tsStr, sizeof(tsStr), "%lld", (long long)entries[i].timestampUtc);
        out += tsStr;
        
        out += "\",\"path\":\"";
        // Escape quotes in path
        const char *path = entries[i].path;
        while (*path) {
            if (*path == '\'') out += "\\'";
            else if (*path == '\"') out += "\\\"";
            else if (*path == '\\') out += "\\\\";
            else if (*path == '\n') out += "\\n";
            else if (*path == '\r') out += "\\r";
            else out += *path;
            path++;
        }
        out += "\"}";
    }
    
    out += "]}";
}

// Deserialize journal cache into VoiceEntry array
int deserializeJournalCache(const std::string &in, VoiceEntry *entries, int maxOut) {
    if (!entries || maxOut <= 0) return 0;
    
    // Simple JSON parsing for the compact schema
    // Look for "entries":[{...},{...}] pattern
    const char *p = in.c_str();
    
    // Find "entries":[
    const char *entriesStart = strstr(p, "\"entries\":[");
    if (!entriesStart) return 0;

    // Corrupt-file contract: a truncated document (missing closing ']') must
    // be rejected wholesale rather than half-parsed.
    if (!strchr(entriesStart, ']')) return 0;
    
    p = entriesStart + 11; // Skip "entries":[ (11 chars)
    
    int count = 0;
    while (*p && count < maxOut) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != '{') break;
        
        // Parse one entry object
        VoiceEntry entry;
        memset(&entry, 0, sizeof(entry));
        
        // Find title (content starts 9 chars after the key's opening quote)
        const char *titleStart = strstr(p, "\"title\":\"");
        if (titleStart) {
            const char *titleEnd = strstr(titleStart + 9, "\"");
            if (titleEnd) {
                size_t len = titleEnd - (titleStart + 9);
                if (len >= VOICE_TITLE_MAX) len = VOICE_TITLE_MAX - 1;
                memcpy(entry.title, titleStart + 9, len);
                entry.title[len] = '\0';
            }
        }
        
        // Find timestamp (content starts 6 chars after the key's opening quote)
        const char *tsStart = strstr(p, "\"ts\":\"");
        if (tsStart) {
            const char *tsEnd = strstr(tsStart + 6, "\"");
            if (tsEnd) {
                char tsStr[32];
                size_t len = tsEnd - (tsStart + 6);
                if (len >= sizeof(tsStr) - 1) len = sizeof(tsStr) - 2;
                memcpy(tsStr, tsStart + 6, len);
                tsStr[len] = '\0';
                entry.timestampUtc = atoll(tsStr);
            }
        }
        
        // Find path (content starts 8 chars after the key's opening quote)
        const char *pathStart = strstr(p, "\"path\":\"");
        if (pathStart) {
            const char *pathEnd = strstr(pathStart + 8, "\"");
            if (pathEnd) {
                size_t len = pathEnd - (pathStart + 8);
                if (len >= VOICE_PATH_MAX) len = VOICE_PATH_MAX - 1;
                memcpy(entry.path, pathStart + 8, len);
                entry.path[len] = '\0';
            }
        }
        
        entries[count++] = entry;
        
        // Find end of this object
        const char *objEnd = strchr(p, '}');
        if (objEnd) {
            p = objEnd + 1;
        } else {
            break;
        }
        
        // Skip comma and whitespace
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    }
    
    return count;
}

} // namespace core
