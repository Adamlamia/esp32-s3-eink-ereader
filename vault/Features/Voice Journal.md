---
title: Voice Journal App
tags: [feature, app, sync]
status: active
batch: "2"
---

# Voice Journal

> Record WAV → SD queue → nightly batch sync to backend → transcribe → timestamped entries.

## Overview

Hold-to-record voice memos via INMP441 I2S MEMS mic. WAV files queued on SD. Backend (self-hosted Whisper + Ollama, or cloud) transcribes and reformats. Timestamped journal entries stored on SD.

## Hard Constraint

ESP32 **cannot** do on-device free-form STT. The device stays "dumb"; intelligence lives in the backend.

## Key Files

| File | Role |
|---|---|
| `src/apps/voicejournal/VoiceJournalApp.cpp` | App lifecycle + UI |
| `src/apps/voicejournal/VoiceSync.cpp` | Backend sync + RAII WiFi |
| `src/core/VoiceModel.h` / `.cpp` | Voice entry model, queue management |

## Configuration

| Define | Value | Description |
|---|---|---|
| `VOICE_CACHE_FILE` | `/journal.json` | Cache path |
| `VOICE_QUEUE_FILE` | `/voice_queue.txt` | Queue manifest |
| `VOICE_WAV_DIR` | `/voice` | WAV storage dir |
| `VOICE_TITLE_MAX` | 48 | Entry title buffer |
| `VOICE_PATH_MAX` | 64 | WAV path buffer |
| `VOICE_QUEUE_MAX_LINES` | 16 | Max queued recordings |
| `VOICE_STALE_SEC` | 6h | Auto-sync threshold |
| `VOICE_BODY_MAX` | 8192 | Backend response cap |

## I2S Microphone Pins

| Signal | GPIO | Header Pin |
|---|---|---|
| BCLK | 48 | SCL |
| WS | 45 | MISO |
| DATA | 39 | CS |
| L/R | GND | Left channel |

- Sample rate: 16000 Hz (speech-optimized)
- Bits per sample: 32 (INMP441 outputs 24-bit in 32-bit I2S words)

## Backend

Backend URL and token configured in `secrets.h`:
```
#define VOICE_BACKEND_URL   "http://192.168.1.100:8000/voice"
#define VOICE_BACKEND_TOKEN "secret-token"
```

Backend choice (self-hosted vs cloud) is **deferred**.

## Privacy

Not always-on. User opens it nightly (queue + sync at night). Backend must stay swappable.

## See Also
- [[Hardware/Pin Map]] — I2S pin assignments
- [[Architecture/Core Logic Seams]]
