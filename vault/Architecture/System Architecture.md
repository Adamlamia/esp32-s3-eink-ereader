---
title: System Architecture
tags: [architecture, overview]
status: reference
---

# System Architecture

> High-level module map and design patterns for the ESP32-S3 E-Ink E-Reader firmware.

## Layer Diagram

```
┌─────────────────────────────────────────────────┐
│                   Apps Layer                     │
│  Reader · Calendar · Weather · QR · Todo ·      │
│  Agenda · DevCompanion · VoiceJournal            │
├─────────────────────────────────────────────────┤
│              App Framework (app/)                │
│  App (base) · AppManager · SystemContext ·       │
│  AppRegistry · WifiSession                       │
├─────────────────────────────────────────────────┤
│            Core Logic (core/) — HAL-free         │
│  IcsParser · CalendarDate · CalendarEvent ·      │
│  OpenMeteo · Emvco · QrPayload · TodoModel ·     │
│  VoiceModel · AgendaMerge · Paginator ·          │
│  PageLayout · TextTransform · Format ·           │
│  SyncSchedule · BatteryMath · ButtonClassify ·   │
│  BookmarkStore · RefsIndex · GithubModel ·       │
│  PathValidation · CalendarDate                   │
├─────────────────────────────────────────────────┤
│            Platform Services                     │
│  DisplayManager · BookStorage · WebPortal ·      │
│  UsbMassStorage · TextReader                     │
├─────────────────────────────────────────────────┤
│            Hardware (ESP32-S3)                   │
│  LilyGo-EPD47 · SPI SD · I2S Mic · ADC · WiFi  │
└─────────────────────────────────────────────────┘
```

## Key Design Patterns

### Header-Only Pure Logic (core/)
All testable logic lives in `src/core/*.h` as header-only files in `namespace core`. No HAL, no heap, no Arduino dependency. Every capacity bounds a fixed static buffer. This enables native Unity testing on the host.

### Delegation-Based Seams
Apps delegate complex logic to core seams. The app handles display and lifecycle; the core seam handles parsing, formatting, and data transformation. This keeps testable code separate from hardware-dependent code.

### App Lifecycle
```
AppManager::loop()
  → decode button (GPIO → ButtonEvent)
  → route to active App::onButton()
  → App::onLoop() tick
  → check sleep conditions
```

### Dependency Injection
`SystemContext` holds shared services (display, storage, wifi). Apps receive it via constructor. No global singletons.

### RAII WiFi Pattern
WiFi is turned on for sync, then off. Apps use `WifiSession` to guard the radio. Multiple apps share the same WiFi window via portal-guard.

## Source Layout

```
src/
├── app/          App framework (App, AppManager, SystemContext, AppRegistry)
├── apps/         One folder per app (reader/, calendar/, weather/, etc.)
├── core/         Header-only pure logic (testable on native)
├── display/      DisplayManager + font data
├── reader/       TextReader (pagination, rendering)
├── storage/      BookStorage (SD + LittleFS abstraction)
├── usb/          UsbMassStorage (TinyUSB MSC)
├── web/          WebPortal (ESPAsyncWebServer)
├── bookmark/     BookmarkManager
├── config.h      Central configuration
├── secrets.h     WiFi creds, API keys (git-ignored)
└── main.cpp      Entry point
```

## See Also
- [[Architecture/App Framework]]
- [[Architecture/Core Logic Seams]]
- [[Hardware/Pin Map]]
