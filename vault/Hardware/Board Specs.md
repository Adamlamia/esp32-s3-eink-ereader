---
title: Board Specs
tags: [hardware, board]
status: reference
board: LILYGO T5 4.7" E-Paper S3
---

# LILYGO T5 4.7" E-Paper S3

## Specifications

| Spec | Value |
|---|---|
| **MCU** | ESP32-S3 (dual-core Xtensa LX7) |
| **Flash** | 16 MB (QIO) |
| **PSRAM** | 8 MB (OPI) — required for full-screen framebuffer |
| **Display** | ED047TC1, 4.7", 960×540, 16-level grayscale |
| **Driver IC** | SSD1681 (via LilyGo-EPD47 library) |
| **Connectivity** | Wi-Fi 802.11 b/g/n + BLE 5.0 |
| **Storage** | microSD slot (SPI) + internal flash |
| **Power** | USB-C charging + on-board Li-Po charger + battery ADC |
| **USB** | Native USB-JTAG (VID:PID = 303A:1001) |
| **Serial** | COM7 @ 115200 |

## Display Details

- **Resolution:** 960 × 540 pixels
- **Colors:** 16-level grayscale
- **Framebuffer:** ~259 KB (stored in PSRAM)
- **Full refresh:** ~800 ms (blocking)
- **Partial refresh:** supported but ghosting accumulates
- **Full-clear cycle:** every 8 pages (`FULL_REFRESH_EVERY`)

## Power Management

- Light sleep after 120s inactivity (`IDLE_SLEEP_SECONDS`)
- WiFi auto-off after 10 minutes (`WEB_ACTIVE_MINUTES`)
- E-ink retains last image with **zero power**
- Battery: 3.7V Li-Po 1S, JST-PH 2.0mm, 1500–2000 mAh recommended

## Build Environments

| Env | Purpose | Command |
|---|---|---|
| `lilygo_t5_47_s3` | Default firmware | `pio run` / `pio run -t upload` |
| `lilygo_t5_47_s3_usbdrive` | USB MSC mode | `pio run -e lilygo_t5_47_s3_usbdrive -t upload` |
| `native` | Host unit tests | `pio test -e native` |

## See Also
- [[Hardware/Pin Map]]
- [[Hardware/Shopping List]]
- [[Development/Build & Flash]]
