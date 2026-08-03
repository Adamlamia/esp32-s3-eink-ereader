#pragma once
// ===========================================================================
//  VoiceSync.h  —  Voice Journal sync logic (VJ·R1)
// ===========================================================================
//  Handles the nightly sync of voice recordings to the backend.
//  Reuses WifiSession for network operations.
// ===========================================================================
#include "app/WifiSession.h"
#include "core/VoiceModel.h"
#include "storage/BookStorage.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace apps {

class VoiceSync {
public:
    explicit VoiceSync(SystemContext &ctx);

    // Sync all queued voice recordings to the backend
    bool syncAll();

private:
    SystemContext &_ctx;
    BookStorage &_storage;

    // Helper methods
    bool sendWavToBackend(const char *wavPath, core::VoiceEntry &entry);
    bool parseBackendResponse(const String &response, core::VoiceEntry &entry);
};

} // namespace apps
