#include "VoiceSync.h"
#include "app/AppManager.h"
#include "core/VoiceModel.h"
#include "storage/BookStorage.h"
#include "app/WifiSession.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace apps {

VoiceSync::VoiceSync(SystemContext &ctx)
    : _ctx(ctx),
      _storage(ctx.storage) {
}

bool VoiceSync::syncAll() {
    // Read queue file
    String queueContent;
    File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
    if (!f) {
        return false;
    }
    queueContent = f.readString();
    f.close();
    
    // Parse queue
    core::VoiceEntry entries[16];
    int n = core::readVoiceQueue(queueContent.c_str(), entries, 16);
    
    if (n <= 0) {
        return true; // Nothing to sync
    }
    
    // Try to get backend URL from secrets.h
#ifdef VOICE_BACKEND_URL
    const char *backendUrl = VOICE_BACKEND_URL;
#else
    const char *backendUrl = "http://localhost:8000/voice";
#endif
    
    // For each entry, send to backend
    bool success = true;
    for (int i = 0; i < n; i++) {
        if (!sendWavToBackend(entries[i].path, entries[i])) {
            success = false;
            continue;
        }
        
        // Remove WAV file and update queue
        _storage.fs().remove(entries[i].path);
        
        // Update journal cache
        String journalContent;
        File journalFile = _storage.fs().open(VOICE_CACHE_FILE, "r");
        if (journalFile) {
            journalContent = journalFile.readString();
            journalFile.close();
        }
        
        // Convert String to std::string for core functions
        std::string journalStr = journalContent.c_str();
        int journalN = core::deserializeJournalCache(journalStr, entries, 16);
        
        // Add new entry
        if (journalN < 16) {
            // The entry already has title and timestamp from backend response
            std::string newJournal;
            core::serializeJournalCache(newJournal, entries, journalN + 1);
            
            journalFile = _storage.fs().open(VOICE_CACHE_FILE, "w");
            if (journalFile) {
                journalFile.print(newJournal.c_str());
                journalFile.close();
            }
        }
    }
    
    return success;
}

bool VoiceSync::sendWavToBackend(const char *wavPath, core::VoiceEntry &entry) {
    // Read WAV file
    File wavFile = _storage.fs().open(wavPath, "r");
    if (!wavFile) {
        return false;
    }
    
    size_t fileSize = wavFile.size();
    if (fileSize == 0) {
        wavFile.close();
        return false;
    }
    
    // Read WAV data
    uint8_t *wavData = new uint8_t[fileSize];
    if (!wavData) {
        wavFile.close();
        return false;
    }
    
    wavFile.read(wavData, fileSize);
    wavFile.close();
    
    // Create HTTP client
    HTTPClient http;
#ifdef VOICE_BACKEND_URL
    http.begin(VOICE_BACKEND_URL);
#else
    http.begin("http://localhost:8000/voice");
#endif
    
    // Set headers
    http.addHeader("Content-Type", "audio/wav");
    
    // Send POST request with WAV data
    int httpResponseCode = http.POST((uint8_t*)wavData, fileSize);
    
    bool result = false;
    if (httpResponseCode > 0) {
        String response = http.getString();
        if (response.length() > 0) {
            result = parseBackendResponse(response, entry);
        }
    }
    
    http.end();
    delete[] wavData;
    
    return result;
}

bool VoiceSync::parseBackendResponse(const String &response, core::VoiceEntry &entry) {
    // Simple JSON parsing for backend response
    // Expected format: {"status":"ok","text":"Transcribed text here","title":"Auto-generated title"}
    
    // Find status
    int statusStart = response.indexOf("\"status\":\"");
    if (statusStart == -1) return false;
    
    int statusEnd = response.indexOf("\"", statusStart + 11);
    if (statusEnd == -1) return false;
    
    String status = response.substring(statusStart + 11, statusEnd);
    if (status != "ok") return false;
    
    // Find title
    int titleStart = response.indexOf("\"title\":\"");
    if (titleStart == -1) return false;
    
    int titleEnd = response.indexOf("\"", titleStart + 10);
    if (titleEnd == -1) return false;
    
    String title = response.substring(titleStart + 10, titleEnd);
    
    // Copy title to entry
    strncpy(entry.title, title.c_str(), VOICE_TITLE_MAX - 1);
    entry.title[VOICE_TITLE_MAX - 1] = '\0';
    
    // Get current time as timestamp
    time_t now = time(nullptr);
    entry.timestampUtc = now;
    
    return true;
}

} // namespace apps
