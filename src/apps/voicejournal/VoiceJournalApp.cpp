#include "VoiceJournalApp.h"
#include "app/AppManager.h"
#include "core/VoiceModel.h"
#include "storage/BookStorage.h"
#include "display/DisplayManager.h"
#include "app/WifiSession.h"
#include "core/IcsParser.h"
#include <driver/i2s.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace apps {

VoiceJournalApp::VoiceJournalApp(SystemContext &ctx)
    : _ctx(ctx),
      _display(ctx.display),
      _storage(ctx.storage) {
}

void VoiceJournalApp::onLaunch() {
    _isRecording = false;
    _isSyncing = false;
    _currentPage = 0;
    _entryCount = 0;
    
    // Initialize SD card if not already done
    if (!_storage.fs().begin()) {
        drawStatus("SD init failed");
        return;
    }
    
    // Check if voice directory exists, create if not
    if (!_storage.fs().exists(VOICE_WAV_DIR)) {
        _storage.fs().mkdir(VOICE_WAV_DIR);
    }
    
    // Auto-sync if queue has entries and journal is stale
    struct stat st;
    if (_storage.fs().stat(VOICE_QUEUE_FILE, &st) == 0 && st.st_size > 0) {
        if (_storage.fs().stat(VOICE_CACHE_FILE, &st) == 0) {
            // Check if journal is stale
            time_t now = time(nullptr);
            if (now - st.st_mtime > VOICE_STALE_SEC) {
                syncVoiceQueue();
            }
        } else {
            // No journal cache, sync immediately
            syncVoiceQueue();
        }
    }
    
    drawStatus("Voice Journal");
}

void VoiceJournalApp::onLoop() {
    // Nothing to do in loop for this app
}

void VoiceJournalApp::onButtonEvent(ButtonEvent event) {
    switch (event.type) {
        case ButtonEvent::Tap:
            // Start recording on tap
            if (!_isRecording && !_isSyncing) {
                showRecordingStatus();
                // Start I2S recording
                // TODO: Implement I2S recording logic
                _isRecording = true;
                // For now, just simulate recording
                delay(1000);
                _isRecording = false;
                showSavedStatus();
                // Add to queue
                char wavPath[64];
                time_t now = time(nullptr);
                struct tm *tm_info = gmtime(&now);
                snprintf(wavPath, sizeof(wavPath), "%s/%04d%02d%02d-%02d%02d%02d.wav", 
                         VOICE_WAV_DIR, 
                         tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                         tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
                
                // Write to queue
                String queueContent;
                if (_storage.fs().exists(VOICE_QUEUE_FILE)) {
                    File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
                    if (f) {
                        queueContent = f.readString();
                        f.close();
                    }
                }
                
                // Convert String to std::string for core functions
                std::string queueStr = queueContent.c_str();
                core::writeVoiceQueue(queueStr, wavPath, now);
                
                // Write back to file
                File f = _storage.fs().open(VOICE_QUEUE_FILE, "w");
                if (f) {
                    f.print(queueStr.c_str());
                    f.close();
                }
                
                // Update journal cache
                // Read current entries
                int n = core::readVoiceQueue(queueStr, _entries, 16);
                std::string json;
                core::serializeJournalCache(json, _entries, n);
                
                f = _storage.fs().open(VOICE_CACHE_FILE, "w");
                if (f) {
                    f.print(json.c_str());
                    f.close();
                }
                
                showQueueStatus();
            }
            break;
            
        case ButtonEvent::MediumHold:
            // View journal entries
            if (!_isRecording && !_isSyncing) {
                // Load journal entries
                String journalContent;
                File f = _storage.fs().open(VOICE_CACHE_FILE, "r");
                if (f) {
                    journalContent = f.readString();
                    f.close();
                    // Convert String to std::string for core functions
                    std::string journalStr = journalContent.c_str();
                    _entryCount = core::deserializeJournalCache(journalStr, _entries, 16);
                }
                
                if (_entryCount > 0) {
                    showJournalList();
                } else {
                    drawStatus("No entries yet");
                }
            }
            break;
            
        case ButtonEvent::LongHold:
            // Back to home
            _ctx.appManager.returnToLauncher();
            break;
            
        default:
            break;
    }
}

void VoiceJournalApp::onSleepWakeup() {
    // Nothing to do
}

void VoiceJournalApp::drawStatus(const char *status) {
    _display.clear();
    _display.drawBookText(status, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    _display.flush();
}

void VoiceJournalApp::drawJournalPage(int page) {
    _display.clear();
    
    int startIdx = page * 10;
    int endIdx = startIdx + 10;
    if (endIdx > _entryCount) endIdx = _entryCount;
    
    int y = 20;
    for (int i = startIdx; i < endIdx; i++) {
        if (i >= _entryCount) break;
        
        // Format timestamp as YYYY-MM-DD HH:MM
        char timeStr[32];
        struct tm *tm_info = gmtime(&_entries[i].timestampUtc);
        snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d", 
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min);
        
        char line[128];
        snprintf(line, sizeof(line), "%s  %s", timeStr, _entries[i].title);
        
        _display.drawBookText(line, 0, y, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
        y += 30;
    }
    
    // Page indicator
    char pageInfo[32];
    snprintf(pageInfo, sizeof(pageInfo), "Page %d of %d", page + 1, (_entryCount + 9) / 10);
    _display.drawBookText(pageInfo, 0, DISPLAY_HEIGHT - 40, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    
    _display.flush();
}

void VoiceJournalApp::showRecordingStatus() {
    drawStatus("Recording...");
}

void VoiceJournalApp::showSavedStatus() {
    drawStatus("Saved!");
}

void VoiceJournalApp::showQueueStatus() {
    // Count lines in queue file
    struct stat st;
    int count = 0;
    if (_storage.fs().stat(VOICE_QUEUE_FILE, &st) == 0) {
        count = st.st_size;
        // Simple line count approximation
        File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
        if (f) {
            String content = f.readString();
            f.close();
            count = 0;
            for (int i = 0; i < content.length(); i++) {
                if (content[i] == '\n') count++;
            }
            // Add one for last line if no trailing newline
            if (content.length() > 0 && content.charAt(content.length()-1) != '\n') count++;
        }
    }
    
    char status[64];
    snprintf(status, sizeof(status), "Queue: %d", count);
    drawStatus(status);
}

void VoiceJournalApp::showJournalList() {
    _currentPage = 0;
    drawJournalPage(_currentPage);
}

void VoiceJournalApp::syncVoiceQueue() {
    _isSyncing = true;
    
    // Use WifiSession for network operations
    WifiSession session(_ctx);
    
    // Read queue file
    String queueContent;
    File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
    if (f) {
        queueContent = f.readString();
        f.close();
    }
    
    // Parse queue
    core::VoiceEntry entries[16];
    int n = core::readVoiceQueue(queueContent.c_str(), entries, 16);
    
    if (n <= 0) {
        _isSyncing = false;
        return;
    }
    
    // Try to get backend URL from secrets.h
    #ifdef VOICE_BACKEND_URL
        const char *backendUrl = VOICE_BACKEND_URL;
    #else
        const char *backendUrl = "http://localhost:8000/voice";
    #endif
    
    // For each entry, send to backend
    for (int i = 0; i < n; i++) {
        // Read WAV file
        File wavFile = _storage.fs().open(entries[i].path, "r");
        if (!wavFile) {
            continue;
        }
        
        // Get file size
        size_t fileSize = wavFile.size();
        if (fileSize == 0) {
            wavFile.close();
            continue;
        }
        
        // Read WAV data
        uint8_t *wavData = new uint8_t[fileSize];
        if (!wavData) {
            wavFile.close();
            continue;
        }
        
        wavFile.read(wavData, fileSize);
        wavFile.close();
        
        // Send to backend
        // TODO: Implement HTTP POST with WAV data
        
        // For now, simulate success
        // Update journal cache
        std::string journalContent;
        File journalFile = _storage.fs().open(VOICE_CACHE_FILE, "r");
        if (journalFile) {
            journalContent = journalFile.readString();
            journalFile.close();
        }
        
        // Add entry to journal
        int journalN = core::deserializeJournalCache(journalContent, _entries, 16);
        
        // Add new entry
        if (journalN < 16) {
            _entries[journalN] = entries[i];
            // Set title from simulated response
            strncpy(_entries[journalN].title, "Auto-generated title", VOICE_TITLE_MAX - 1);
            _entries[journalN].title[VOICE_TITLE_MAX - 1] = '\0';
            
            // Serialize updated journal
            std::string newJournal;
            core::serializeJournalCache(newJournal, _entries, journalN + 1);
            
            // Write back to file
            journalFile = _storage.fs().open(VOICE_CACHE_FILE, "w");
            if (journalFile) {
                journalFile.print(newJournal.c_str());
                journalFile.close();
            }
        }
        
        // Remove WAV file and queue entry
        _storage.fs().remove(entries[i].path);
        
        // Update queue
        std::string newQueue;
        core::writeVoiceQueue(queueContent.c_str(), "", 0); // This is a placeholder - actual implementation needed
        
        delete[] wavData;
    }
    
    _isSyncing = false;
}

} // namespace apps
