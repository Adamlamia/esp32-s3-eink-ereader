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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char *TAG = "VoiceJournal";

namespace apps {

// ===========================================================================
//  Construction / lifecycle
// ===========================================================================

VoiceJournalApp::VoiceJournalApp(SystemContext &ctx)
    : _ctx(ctx),
      _display(ctx.display),
      _storage(ctx.storage),
      _isRecording(false),
      _isSyncing(false),
      _currentPage(0),
      _entryCount(0),
      _i2sRunning(false),
      _recordingStartMs(0),
      _recordingBytes(0),
      _lastDurationSec(0) {
    memset(_recordingPath, 0, sizeof(_recordingPath));
}

void VoiceJournalApp::onEnter() {
    ESP_LOGI(TAG, "onEnter() starting");
    _isRecording  = false;
    _isSyncing    = false;
    _currentPage  = 0;
    _entryCount   = 0;
    _i2sRunning   = false;
    _recordingBytes = 0;
    _lastDurationSec = 0;

    if (!_storage.begin()) {
        ESP_LOGE(TAG, "Storage init failed");
        drawStatus("Storage error\nNo SD / LittleFS");
        return;
    }
    ESP_LOGI(TAG, "Storage initialized, using %s", _storage.usingSD() ? "SD" : "LittleFS");

    // Try to create voice directory; if it fails, use root directory
    bool canWrite = true;
    _wavDir = VOICE_WAV_DIR;
    if (!_storage.fs().exists(VOICE_WAV_DIR)) {
        ESP_LOGI(TAG, "Creating %s", VOICE_WAV_DIR);
        if (!_storage.fs().mkdir(VOICE_WAV_DIR)) {
            ESP_LOGW(TAG, "Cannot create %s, using root directory", VOICE_WAV_DIR);
            _wavDir = "/";
            canWrite = true;  // Still writable, just no subdirectory
        } else {
            ESP_LOGI(TAG, "Created %s successfully", VOICE_WAV_DIR);
        }
    } else {
        ESP_LOGI(TAG, "%s already exists", VOICE_WAV_DIR);
    }

    // Auto-sync if queue has entries
    if (_storage.fs().exists(VOICE_QUEUE_FILE)) {
        File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
        if (f) {
            size_t queueSize = f.size();
            f.close();
            if (queueSize > 0) {
                syncVoiceQueue();
            }
        }
    }

    ESP_LOGI(TAG, "onEnter() complete, drawing status");
    if (!canWrite) {
        drawStatus("Read-only storage\nInsert SD card");
    } else if (_wavDir != VOICE_WAV_DIR) {
        drawStatus("Voice Journal\n(root dir) Tap=Record");
    } else {
        drawStatus("Voice Journal\nTap=Record Hold=List");
    }
}

void VoiceJournalApp::onExit() {
    // Guarantee I2S is torn down if the user leaves mid-recording
    if (_i2sRunning || _recordingFile) {
        stopRecording();
    }
}

// ===========================================================================
//  I2S driver helpers
// ===========================================================================

bool VoiceJournalApp::startI2S() {
    if (_i2sRunning) return true;

    i2s_config_t i2sConfig = {};
    i2sConfig.mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    i2sConfig.sample_rate      = I2S_MIC_SAMPLE_RATE;
    i2sConfig.bits_per_sample  = I2S_BITS_PER_SAMPLE_32BIT;
    i2sConfig.channel_format   = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.dma_buf_count    = 4;
    i2sConfig.dma_buf_len      = 256;   // 256 samples × 4 B = 1 KB per buf
    i2sConfig.use_apll         = false;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install failed: %d", (int)err);
        return false;
    }

    i2s_pin_config_t pinConfig = {};
    pinConfig.bck_io_num   = I2S_MIC_BCLK_PIN;   // GPIO 48
    pinConfig.ws_io_num    = I2S_MIC_WS_PIN;      // GPIO 45
    pinConfig.data_out_num = I2S_PIN_NO_CHANGE;
    pinConfig.data_in_num  = I2S_MIC_DATA_PIN;    // GPIO 39

    err = i2s_set_pin(I2S_NUM_0, &pinConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin failed: %d", (int)err);
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }

    _i2sRunning = true;
    ESP_LOGI(TAG, "I2S started: BCLK=%d WS=%d DATA=%d @ %d Hz",
             I2S_MIC_BCLK_PIN, I2S_MIC_WS_PIN, I2S_MIC_DATA_PIN,
             I2S_MIC_SAMPLE_RATE);
    return true;
}

void VoiceJournalApp::stopI2S() {
    if (!_i2sRunning) return;
    i2s_stop(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_0);
    _i2sRunning = false;
    ESP_LOGI(TAG, "I2S stopped");
}

// ===========================================================================
//  WAV file helpers
// ===========================================================================

// Write a 44-byte RIFF/WAV header.  dataSize is the raw PCM byte count that
// will follow the header.  We write a placeholder first, then seek back and
// overwrite it when recording stops (so the file is always a valid WAV).
static bool writeWavHeader(File &f, uint32_t dataSize, uint32_t sampleRate) {
    uint32_t fileSize   = 36 + dataSize;
    uint16_t audioFormat = 1;          // PCM
    uint16_t numChannels = 1;          // mono
    uint16_t bitsPerSample = 32;
    uint32_t byteRate    = sampleRate * numChannels * bitsPerSample / 8;
    uint16_t blockAlign  = numChannels * bitsPerSample / 8;

    uint8_t hdr[44];
    memcpy(&hdr[0],  "RIFF", 4);
    memcpy(&hdr[4],  &fileSize,    4);
    memcpy(&hdr[8],  "WAVE", 4);
    memcpy(&hdr[12], "fmt ", 4);
    uint32_t subChunk1Size = 16;
    memcpy(&hdr[16], &subChunk1Size, 4);
    memcpy(&hdr[20], &audioFormat,   2);
    memcpy(&hdr[22], &numChannels,   2);
    memcpy(&hdr[24], &sampleRate,    4);
    memcpy(&hdr[28], &byteRate,      4);
    memcpy(&hdr[32], &blockAlign,    2);
    memcpy(&hdr[34], &bitsPerSample, 2);
    memcpy(&hdr[36], "data", 4);
    memcpy(&hdr[40], &dataSize,      4);

    return (f.write(hdr, 44) == 44);
}

// ===========================================================================
//  Recording start / stop
// ===========================================================================

bool VoiceJournalApp::startRecording() {
    if (_i2sRunning) return false;

    // Build a timestamped file path using _wavDir (may be "/" if mkdir failed)
    time_t now = time(nullptr);
    struct tm *tm_info = gmtime(&now);
    String dirPrefix = (_wavDir == "/") ? "/" : String(VOICE_WAV_DIR) + "/";
    if (tm_info) {
        snprintf(_recordingPath, sizeof(_recordingPath),
                 "%s%04d%02d%02d-%02d%02d%02d.wav",
                 dirPrefix.c_str(),
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else {
        snprintf(_recordingPath, sizeof(_recordingPath),
                 "%sunknown.wav", dirPrefix.c_str());
    }

    _recordingFile = _storage.fs().open(_recordingPath, FILE_WRITE);
    if (!_recordingFile) {
        ESP_LOGE(TAG, "Cannot create %s", _recordingPath);
        drawStatus("SD write error");
        return false;
    }

    // Write placeholder WAV header (dataSize = 0, patched on stop)
    if (!writeWavHeader(_recordingFile, 0, I2S_MIC_SAMPLE_RATE)) {
        _recordingFile.close();
        drawStatus("WAV header error");
        return false;
    }

    if (!startI2S()) {
        _recordingFile.close();
        drawStatus("I2S init failed");
        return false;
    }

    _recordingStartMs = millis();
    _recordingBytes   = 0;
    _lastDurationSec  = 0;
    _isRecording      = true;

    ESP_LOGI(TAG, "Recording started → %s", _recordingPath);
    updateRecordingDisplay();
    return true;
}

void VoiceJournalApp::stopRecording() {
    if (!_isRecording && !_i2sRunning) return;

    // 1. Stop I2S clocking
    stopI2S();

    // 2. Patch the WAV header with the real data size
    if (_recordingFile) {
        _recordingFile.seek(40);                       // offset of data-size field
        _recordingFile.write((uint8_t *)&_recordingBytes, 4);
        _recordingFile.flush();
        _recordingFile.close();
    }

    uint32_t durationSec = (millis() - _recordingStartMs) / 1000;
    ESP_LOGI(TAG, "Recording stopped: %u s, %u bytes → %s",
             durationSec, _recordingBytes, _recordingPath);

    // 3. Enqueue the WAV file for later sync
    time_t now = time(nullptr);
    std::string queueStr;
    if (_storage.fs().exists(VOICE_QUEUE_FILE)) {
        File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
        if (f) {
            queueStr = f.readString().c_str();
            f.close();
        }
    }
    core::writeVoiceQueue(queueStr, _recordingPath, (int64_t)now);

    File f = _storage.fs().open(VOICE_QUEUE_FILE, "w");
    if (f) {
        f.print(queueStr.c_str());
        f.close();
    }

    // 4. Update the journal cache so the entry appears immediately
    int n = core::readVoiceQueue(queueStr, _entries, 16);
    std::string json;
    core::serializeJournalCache(json, _entries, n);
    f = _storage.fs().open(VOICE_CACHE_FILE, "w");
    if (f) {
        f.print(json.c_str());
        f.close();
    }
    _entryCount = n;

    // 5. Reset state
    _isRecording      = false;
    _recordingStartMs = 0;
    _recordingBytes   = 0;
    _lastDurationSec  = 0;
    memset(_recordingPath, 0, sizeof(_recordingPath));

    char status[48];
    snprintf(status, sizeof(status), "Saved! %u s", durationSec);
    drawStatus(status);
}

void VoiceJournalApp::updateRecordingDisplay() {
    if (!_isRecording) return;
    uint32_t sec = (millis() - _recordingStartMs) / 1000;
    if (sec != _lastDurationSec) {
        _lastDurationSec = sec;
        // Clear and draw a BIG recording indicator
        _display.clearBuffer();
        // Top: large "REC" with blinking dot
        char buf[64];
        snprintf(buf, sizeof(buf), "● REC  %02u:%02u", sec / 60, sec % 60);
        _display.drawBookText(0, 80, String(buf));
        // Bottom: hint
        _display.drawBookText(0, _display.usableHeight() - 60, String("Tap to stop"));
        _display.flush();
    }
}

// ===========================================================================
//  Button handling
// ===========================================================================

void VoiceJournalApp::onButton(ButtonEvent ev) {
    switch (ev) {
        case ButtonEvent::Tap:
            if (_isSyncing) break;

            if (_isRecording) {
                // Second tap → stop recording
                stopRecording();
            } else {
                // First tap → start recording
                if (!startRecording()) {
                    // startRecording already drew an error status
                }
            }
            break;

        case ButtonEvent::MediumHold:
            if (_isRecording || _isSyncing) break;
            // Load and show journal entries
            {
                String journalContent;
                File f = _storage.fs().open(VOICE_CACHE_FILE, "r");
                if (f) {
                    journalContent = f.readString();
                    f.close();
                    std::string journalStr = journalContent.c_str();
                    _entryCount = core::deserializeJournalCache(
                        journalStr, _entries, 16);
                }
            }
            if (_entryCount > 0) {
                showJournalList();
            } else {
                drawStatus("No entries yet");
            }
            break;

        case ButtonEvent::LongHold:
            if (_ctx.manager) {
                _ctx.manager->requestHome();
            }
            break;

        default:
            break;
    }
}

// ===========================================================================
//  Main loop — drains I2S while recording, stays responsive to buttons
// ===========================================================================

void VoiceJournalApp::onLoop(uint32_t /*nowMs*/) {
    if (!_isRecording || !_i2sRunning) return;

    // Non-blocking read: grab whatever the I2S DMA has buffered right now.
    // At 16 kHz × 32-bit mono ≈ 2 000 B/s, a 100 ms window yields ~200 B.
    // Using a small buffer keeps each iteration fast so button events are
    // processed with < 200 ms latency.
    int32_t samples[128];
    size_t  bytesRead = 0;
    esp_err_t err = i2s_read(I2S_NUM_0, samples, sizeof(samples),
                             &bytesRead, 100 /* ms timeout */);
    if (err == ESP_OK && bytesRead > 0) {
        size_t written = _recordingFile.write((uint8_t *)samples, bytesRead);
        if (written > 0) {
            _recordingBytes += (uint32_t)written;
        }

        // --- Diagnostic: print peak amplitude every ~1 second ---
        // INMP441 idle = 0x000000 (24-bit), so any deviation = audio detected.
        static uint32_t diagBytes = 0;
        static int32_t  diagPeak  = 0;
        diagBytes += (uint32_t)written;
        int nSamples = (int)(bytesRead / sizeof(int32_t));
        for (int i = 0; i < nSamples; i++) {
            int32_t v = samples[i] >> 8;  // shift 24-bit to signed range
            if (v < 0) v = -v;
            if (v > diagPeak) diagPeak = v;
        }
        if (diagBytes >= 2000) {  // ~1 second worth of data
            ESP_LOGI(TAG, "I2S diag: %u bytes, peak=%d (silence≈0, speech>500)",
                     diagBytes, (int)diagPeak);
            diagBytes = 0;
            diagPeak  = 0;
        }
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_read error: %d", (int)err);
    }

    // Update the e-ink counter once per second (cheap: only redraws on change)
    updateRecordingDisplay();
}

// ===========================================================================
//  Display helpers
// ===========================================================================

void VoiceJournalApp::drawStatus(const char *status) {
    _display.clearBuffer();
    _display.drawBookText(0, 0, String(status));
    _display.flush();
}

void VoiceJournalApp::drawJournalPage(int page) {
    _display.clearBuffer();

    int startIdx = page * 10;
    int endIdx   = startIdx + 10;
    if (endIdx > _entryCount) endIdx = _entryCount;

    int y = 20;
    for (int i = startIdx; i < endIdx; i++) {
        if (i >= _entryCount) break;

        char timeStr[32];
        time_t timestamp = (time_t)_entries[i].timestampUtc;
        struct tm *tm_info = gmtime(&timestamp);
        if (tm_info) {
            snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d",
                     tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                     tm_info->tm_hour, tm_info->tm_min);
        } else {
            strcpy(timeStr, "Unknown time");
        }

        char line[128];
        snprintf(line, sizeof(line), "%s  %s", timeStr, _entries[i].title);

        _display.drawBookText(0, y, String(line));
        y += 30;
    }

    char pageInfo[32];
    snprintf(pageInfo, sizeof(pageInfo), "Page %d of %d",
             page + 1, (_entryCount + 9) / 10);
    _display.drawBookText(0, _display.usableHeight() - 40, String(pageInfo));

    _display.flush();
}

void VoiceJournalApp::showJournalList() {
    _currentPage = 0;
    drawJournalPage(_currentPage);
}

void VoiceJournalApp::showQueueStatus() {
    int count = 0;
    if (_storage.fs().exists(VOICE_QUEUE_FILE)) {
        File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
        if (f) {
            String content = f.readString();
            f.close();
            for (int i = 0; i < content.length(); i++) {
                if (content[i] == '\n') count++;
            }
            if (content.length() > 0 &&
                content.charAt(content.length() - 1) != '\n') count++;
        }
    }
    char status[64];
    snprintf(status, sizeof(status), "Queue: %d", count);
    drawStatus(status);
}

// ===========================================================================
//  Wi-Fi sync (unchanged from previous implementation)
// ===========================================================================

apps::VoiceSyncResult VoiceJournalApp::syncVoiceQueue() {
    VoiceSyncResult r = {};
    _isSyncing = true;

    auto result = wifiSessionRunOnTask<VoiceSyncResult>("voiceSync",
        [this]() {
            VoiceSyncResult r = {};

            String queueContent;
            File f = _storage.fs().open(VOICE_QUEUE_FILE, "r");
            if (f) {
                queueContent = f.readString();
                f.close();
            }

            core::VoiceEntry entries[16];
            int n = core::readVoiceQueue(queueContent.c_str(), entries, 16);

            if (n <= 0) {
                snprintf(r.message, sizeof(r.message), "No voice entries in queue");
                return r;
            }

#ifdef VOICE_BACKEND_URL
            const char *backendUrl = VOICE_BACKEND_URL;
#else
            const char *backendUrl = "http://localhost:8000/voice";
#endif

            for (int i = 0; i < n; i++) {
                File wavFile = _storage.fs().open(entries[i].path, "r");
                if (!wavFile) { r.entriesFailed++; continue; }

                size_t fileSize = wavFile.size();
                if (fileSize == 0) { wavFile.close(); r.entriesFailed++; continue; }

                uint8_t *wavData = new uint8_t[fileSize];
                if (!wavData) { wavFile.close(); r.entriesFailed++; continue; }
                wavFile.read(wavData, fileSize);
                wavFile.close();

                // TODO: HTTP POST wavData to backendUrl

                String journalContent;
                File journalFile = _storage.fs().open(VOICE_CACHE_FILE, "r");
                if (journalFile) {
                    journalContent = journalFile.readString();
                    journalFile.close();
                }

                int journalN = core::deserializeJournalCache(
                    journalContent.c_str(), _entries, 16);

                if (journalN < 16) {
                    _entries[journalN] = entries[i];
                    strncpy(_entries[journalN].title,
                            "Auto-generated title", VOICE_TITLE_MAX - 1);
                    _entries[journalN].title[VOICE_TITLE_MAX - 1] = '\0';

                    std::string newJournal;
                    core::serializeJournalCache(
                        newJournal, _entries, journalN + 1);

                    journalFile = _storage.fs().open(VOICE_CACHE_FILE, "w");
                    if (journalFile) {
                        journalFile.print(newJournal.c_str());
                        journalFile.close();
                    }
                }

                _storage.fs().remove(entries[i].path);
                delete[] wavData;
                r.entriesOk++;
            }

            r.entriesFetched = n;
            r.ok = (r.entriesOk > 0);
            if (r.entriesOk == 0)
                snprintf(r.message, sizeof(r.message),
                         "All %d entry(s) failed", r.entriesFailed);
            else
                snprintf(r.message, sizeof(r.message),
                         "%d entries synced", r.entriesOk);
            return r;
        });

    _isSyncing = false;
    return result;
}

} // namespace apps
