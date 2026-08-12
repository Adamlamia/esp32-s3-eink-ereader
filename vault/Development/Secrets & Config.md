---
title: Secrets & Config
tags: [development, config, secrets]
status: reference
---

# Secrets & Configuration

> How secrets and config work in this project.

## config.h

Central configuration at `src/config.h`. Contains:
- Firmware version (`FW_VERSION = "0.2.0"`)
- Owner name (`OWNER_NAME = "Adam"`)
- WiFi AP defaults
- Pin assignments
- Button gesture timing
- Per-app sizing constants
- Power management defaults

All values are compile-time `#define`s. Many have `#ifndef` guards so secrets.h can override.

## secrets.h

Located at `src/secrets.h`. **Git-ignored — never commit.**

Contains:
- WiFi credentials (`WIFI_STA_SSID`, `WIFI_STA_PASS`)
- Calendar ICS URLs (`CAL_ICS_URL_0` .. `CAL_ICS_URL_3`)
- Weather overrides (`WEATHER_LAT`, `WEATHER_LON`, `WEATHER_LABEL`)
- QR payloads (`QR_PAYLOAD_0` .. `QR_PAYLOAD_7`)
- Todo ICS URL (`TODO_ICS_URL`)
- GitHub PAT + repos (`GITHUB_PAT`, `GITHUB_REPO_0` .. `GITHUB_REPO_3`)
- Voice backend (`VOICE_BACKEND_URL`, `VOICE_BACKEND_TOKEN`)

Included via `__has_include` — firmware builds with or without it.

> [!danger] Never commit secrets
> `src/secrets.h` is in `.gitignore`. The file contains API keys and passwords.

## Graceful Empty State

Every app builds and runs without `secrets.h`:
- Calendar shows empty cache
- Weather uses default Kuala Lumpur coords
- QR shows empty carousel
- Todo shows "set TODO_ICS_URL" message
- GitHub shows "set GITHUB_PAT" message

## Partition Layout

Defined in `partitions.csv`:
- Dual OTA slots
- Large LittleFS region for web UI + books

## See Also
- [[Development/Build & Flash]]
- [[Project/Project Overview]]
