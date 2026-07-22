# 🔧 Hardware Guide

## Target board: LILYGO T5 4.7" E-Paper S3

| Spec | Value |
|------|-------|
| MCU | ESP32-S3 (dual-core, Wi-Fi + BLE) |
| Flash | 16 MB |
| PSRAM | 8 MB (OPI) — **required** for the full-screen framebuffer |
| Display | ED047TC1, 4.7", 960 × 540, 16-level grayscale |
| Driver library | [LilyGo-EPD47](https://github.com/Xinyuan-LilyGo/LilyGo-EPD47) |
| Storage | microSD slot (SPI) + internal flash |
| Power | USB-C, on-board Li-Po charger + battery ADC |

> ⚠️ **Confirm your exact board.** LILYGO ships several e-paper boards. If yours
> is the older **ESP32 (not S3)** T5 4.7", use the `epdiy` / classic EPD47
> profile and a non-S3 board id in `platformio.ini`. If it is a **T-Display-S3
> (LCD, not e-ink)**, the display layer needs TFT_eSPI instead of EPD47.

### How to identify your board
- Look for the silkscreen label on the PCB (e.g. `T5-ePaper-S3`).
- On the serial monitor at boot, the ESP32 chip revision is printed.
- Cross-check against the [LILYGO product page](https://www.lilygo.cc/).

## Pin map (as configured in `src/config.h`)

| Function | GPIO | Notes |
|----------|------|-------|
| SD SCLK | 14 | shared SPI |
| SD MISO | 16 | |
| SD MOSI | 15 | |
| SD CS   | 13 | |
| BOOT button | 0 | built-in; short = next page, long = bookmark |
| PREV button | 39 | optional external tactile button to GND |
| NEXT button | 40 | optional external tactile button to GND |
| Battery ADC | 14 | via on-board divider (`BATTERY_DIVIDER`) |

> These pins follow common T5 4.7" S3 references but **vary by board
> revision**. Verify against your unit's schematic and adjust `config.h`.
> The SD and battery-ADC entries above intentionally reuse GPIO 14 as a
> placeholder — split them onto distinct pins for your revision.

## Optional external buttons
Wire a momentary push button between the GPIO and **GND**. Internal pull-ups are
enabled in firmware (`INPUT_PULLUP`), so no external resistor is needed.

```
GPIO 39 ──[ button ]── GND      (previous page)
GPIO 40 ──[ button ]── GND      (next page)
```

## Power / battery
- The reader uses **light sleep** after `IDLE_SLEEP_SECONDS` of inactivity and
  wakes on the BOOT button.
- Wi-Fi is powered down `WEB_ACTIVE_MINUTES` after boot to save energy; reading
  continues fully offline.
- E-ink retains the last image with **zero power**, so a "sleeping" reader still
  shows your page.
