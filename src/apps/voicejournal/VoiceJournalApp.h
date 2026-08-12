#pragma once
// ===========================================================================
//  VoiceJournalApp.h  —  Voice Journal app (VJ·R1)
// ===========================================================================
//  App that implements the Voice Journal feature:
//    - Tap = record (hold to start, release to stop)
//    - MediumHold = view journal entries (chronological list)
//    - LongHold = menu → "Back to Home"
//
//  Uses I2S recording with INMP441 mic and SD storage.
// ===========================================================================
#include "app/App.h"
#include "app/SystemContext.h"
#include "core/VoiceModel.h"
#include "storage/BookStorage.h"
#include "display/DisplayManager.h"
#include "app/WifiSession.h"
#include "core/IcsParser.h" // for time helpers
#include <Arduino.h>
#include <FS.h>

namespace apps {

// --- Result of one voice sync session ----------------------------------------
struct VoiceSyncResult {
    bool ok;               // true iff >= 1 entry succeeded AND the cache was written
    int  entriesFetched;    // entries processed
    int  entriesOk;         // entries that were successfully synced
    int  entriesFailed;     // entries that failed
    char message[64];      // short human-readable status/error, always NUL-terminated
};

class VoiceJournalApp : public App {
public:
    explicit VoiceJournalApp(SystemContext &ctx);

    // --- Identity ---
    const char* name() const override { return "Voice Journal"; }
    const char* icon() const override { return "[🎤]"; }   // microphone glyph

    // --- Lifecycle ---
    void onEnter() override;
    void onExit()  override;

    // --- Input ---
    void onButton(ButtonEvent ev) override;

    // --- Optional hooks ---
    void onLoop(uint32_t nowMs) override; // periodic tick
    bool wantsSleep() override { return !_isRecording && !_isSyncing; } // stay awake during recording/sync
    int32_t sleepWakeupSec() override { return -1; } // no background schedule

private:
    SystemContext &_ctx;
    DisplayManager &_display;
    BookStorage &_storage;

    // State
    bool _isRecording;
    bool _isSyncing;
    int _currentPage;
    int _entryCount;
    core::VoiceEntry _entries[16]; // max entries to display

    // I2S recording state
    bool     _i2sRunning;          // I2S driver installed and clocking
    File     _recordingFile;       // open WAV file on SD (valid while recording)
    char     _recordingPath[64];   // path of the file being recorded
    uint32_t _recordingStartMs;    // millis() snapshot when recording began
    uint32_t _recordingBytes;      // raw PCM bytes written so far
    uint32_t _lastDurationSec;     // last value shown on screen (avoid flicker)
    String   _wavDir;              // actual WAV directory (may be "/" if mkdir failed)

    // I2S helpers
    bool     startI2S();
    void     stopI2S();
    bool     startRecording();     // install I2S + open WAV file
    void     stopRecording();      // finalize WAV header + close + enqueue
    void     updateRecordingDisplay(); // show "Recording... Ns" on e-ink

    // Helper methods
    void drawStatus(const char *status);
    void drawJournalPage(int page);
    void showRecordingStatus();
    void showSavedStatus();
    void showQueueStatus();
    void showJournalList();
    VoiceSyncResult syncVoiceQueue();
};

} // namespace apps
