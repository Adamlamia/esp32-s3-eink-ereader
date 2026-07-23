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
| SD SCLK | 11 | shared SPI (matches LilyGo-EPD47 `utilities.h`) |
| SD MISO | 16 | |
| SD MOSI | 15 | |
| SD CS   | 42 | |
| User button | 21 | built-in; gesture-driven (see Navigation in the README) |
| PREV button | −1 | optional external button, **disabled** by default |
| NEXT button | −1 | optional external button, **disabled** by default |
| Battery ADC | 14 | on-board divider (`BATTERY_DIVIDER`); ADC2 — see caveat below |

> ⚠️ **GPIO0 and GPIO40 are e-paper control lines** on this board (CFG_STR and
> STH), so they **cannot** be used as buttons. The board exposes only **one**
> user button, GPIO21, wired to GND and read active-low — all reading, menu and
> library actions are driven from it by hold-duration gestures.
>
> These pins follow the LilyGo-EPD47 S3 reference but **vary by board
> revision**. Verify against your unit's schematic and adjust `config.h`.

## Optional external buttons
The reader works fully from the single GPIO21 button, so external buttons are
**off by default** (`BTN_PREV` / `BTN_NEXT` are `-1` in `config.h`). To add real
prev/next keys, pick a **free** GPIO (not 0, 40, or the SD/e-paper pins), set it
in `config.h`, and wire a momentary push button between that GPIO and **GND**.
Internal pull-ups are enabled in firmware (`INPUT_PULLUP`), so no external
resistor is needed.

```
GPIO xx ──[ button ]── GND      (previous page → set BTN_PREV = xx)
GPIO yy ──[ button ]── GND      (next page     → set BTN_NEXT = yy)
```

## Power / battery
- The reader uses **light sleep** after `IDLE_SLEEP_SECONDS` (default 120 s) of
  inactivity and wakes on the GPIO21 button.
- Wi-Fi is powered down `WEB_ACTIVE_MINUTES` (default 10 min) after boot to save
  energy; reading continues fully offline. Toggling Wi-Fi by hand (menu or the
  library's Wi-Fi row) disables this auto-shutoff.
- Battery voltage is read on **ADC2 (GPIO14)**, which the ESP32-S3 **cannot use
  while Wi-Fi is active** — the firmware skips battery sampling whenever the
  portal is on and keeps the last reading.
- E-ink retains the last image with **zero power**, so a "sleeping" reader still
  shows your page.
