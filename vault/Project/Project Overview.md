---
title: Project Overview
tags: [project, overview]
status: active
version: "0.2.0"
owner: Sufi Adam
github: Adamlamia/esp32-s3-eink-ereader
---

# ESP32-S3 E-Ink E-Reader

> A multi-app e-ink e-reader built on the LILYGO T5 4.7" S3 board.

## Quick Facts

| Item | Value |
|---|---|
| **MCU** | ESP32-S3 (dual-core, Wi-Fi + BLE) |
| **Display** | ED047TC1, 4.7", 960×540, 16-level grayscale |
| **Flash / PSRAM** | 16 MB / 8 MB (OPI) |
| **Framework** | Arduino + PlatformIO |
| **Firmware** | v0.2.0 |
| **Owner** | Adam (GitHub: [[Adamlamia]]) |

## Registered Apps

| App | Status | Branch |
|---|---|---|
| [[Features/Reader\|E-Reader]] | ✅ Active | main |
| [[Features/Calendar\|Calendar]] | ✅ Active | main |
| [[Features/Weather\|Weather]] | ✅ Active | main |
| [[Features/QR Toolkit\|QR Toolkit]] | ✅ Active | main |
| [[Features/Dev Companion\|Dev Companion]] | ✅ Active | main |
| [[Features/Voice Journal\|Voice Journal]] | ✅ Active | main |
| [[Features/Todo\|Todo]] | ⏸️ Deferred | main (disabled) |
| [[Features/Agenda\|Agenda]] | 🔲 Planned | — |

## Key Documents

- [[Project/Roadmap & Ideas]] — blue-sky wishlist (not the locked plan)
- [[Architecture/System Architecture]] — module map and design patterns
- [[Hardware/Pin Map]] — GPIO reference and constraints
- [[Development/Build & Flash]] — compile, upload, test commands

## Locked Build Order

```
Weather → QR Toolkit → Todo → Agenda   ⟵ batch 1 (autopilot, FULL PAUSE after Agenda)
Dev Companion, Voice Journal            ⟵ batch 2 (later)
```

> [!warning] Todo is deferred
> Google Tasks has no ICS/CalDAV feed. Code + 31 tests merged but app disabled in launcher. See [[Features/Todo]].
