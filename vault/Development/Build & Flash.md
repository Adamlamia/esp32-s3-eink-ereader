---
title: Build & Flash
tags: [development, build, platformio]
status: reference
---

# Build & Flash

> PlatformIO build commands and environment setup.

## Commands

| Action | Command |
|---|---|
| **Compile firmware** | `python -m platformio run` |
| **Flash firmware** | `python -m platformio run -t upload` |
| **Flash web UI** | `python -m platformio run -t uploadfs` |
| **Serial monitor** | `python -m platformio device monitor` |
| **Run native tests** | `python -m platformio test -e native` |
| **USB drive mode** | `python -m platformio run -e lilygo_t5_47_s3_usbdrive -t upload` |

> [!warning] PowerShell
> Use `;` as statement separator, **never `&&`**. PlatformIO is invoked as `python -m platformio` (bare `pio` is not on PATH).

## Environment

| Setting | Value |
|---|---|
| Board env | `lilygo_t5_47_s3` |
| Device | COM7 @ 115200 |
| Flash | 16 MB (QIO) |
| PSRAM | 8 MB (OPI, `qio_opi`) |
| Partitions | `partitions.csv` (dual OTA + LittleFS) |
| Filesystem | LittleFS |
| USB VID:PID | 303A:1001 |

## Native Test Setup

Native tests need a host GCC/Clang toolchain:

```powershell
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
python -m platformio test -e native
```

## Build Environments

| Env | Platform | Purpose |
|---|---|---|
| `lilygo_t5_47_s3` | espressif32 | Default firmware |
| `lilygo_t5_47_s3_usbdrive` | espressif32 | USB MSC mode |
| `native` | native | Host unit tests (Unity) |

## Library Dependencies

| Library | Version | For |
|---|---|---|
| LilyGo-EPD47 | git | Display driver |
| ESPAsyncWebServer | ^3.6.0 | Web portal |
| AsyncTCP | ^3.3.2 | Async TCP stack |
| ArduinoJson | ^7.2.0 | JSON parsing |
| QRCode | ^0.0.1 | QR encoding (firmware only) |

## See Also
- [[Development/Testing]]
- [[Development/Secrets & Config]]
- [[Hardware/Board Specs]]
