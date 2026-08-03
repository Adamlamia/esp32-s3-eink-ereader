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
#include "core/VoiceModel.h"
#include "storage/BookStorage.h"
#include "display/DisplayManager.h"
#include "app/WifiSession.h"
#include "core/IcsParser.h" // for time helpers

namespace apps {

class VoiceJournalApp : public App {
public:
    explicit VoiceJournalApp(SystemContext &ctx);

    void onLaunch() override;
    void onLoop() override;
    void onButtonEvent(ButtonEvent event) override;
    void onSleepWakeup() override;

    int64_t sleepWakeupSec() override { return -1; } // no background schedule

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

    // Helper methods
    void drawStatus(const char *status);
    void drawJournalPage(int page);
    void showRecordingStatus();
    void showSavedStatus();
    void showQueueStatus();
    void showJournalList();
    void syncVoiceQueue();
};

} // namespace apps
