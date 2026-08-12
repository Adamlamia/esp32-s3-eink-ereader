---
title: Pin Map
tags: [hardware, gpio, pins]
status: reference
board: LILYGO T5 4.7" E-Paper S3
---

# GPIO Pin Map

> Complete GPIO reference for the LILYGO T5 4.7" S3 board.

## Active Pins

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| SD SCLK | 11 | OUT | Shared SPI |
| SD MISO | 16 | IN | |
| SD MOSI | 15 | OUT | |
| SD CS | 42 | OUT | |
| User Button (onboard) | 21 | IN | Active-low, `INPUT_PULLUP` |
| Battery ADC | 14 | IN | ADC2 — see caveat |
| Button A (planned) | 9 | IN | Two-button nav, not yet wired |
| Button B (planned) | 46 | IN | Two-button nav, not yet wired |

## I2S Microphone (Voice Journal)

| Function | GPIO | Notes |
|---|---|---|
| I2S BCLK | 48 | Bit clock → header "SCL" |
| I2S WS | 45 | Word select → header "MISO" |
| I2S DATA | 39 | Serial data → header "CS" |
| I2S L/R | GND | Left channel mono |

Sample rate: 16000 Hz, 32-bit words (INMP441 outputs 24-bit in 32-bit I2S).

## Reserved / Unavailable GPIOs

| GPIO | Reason |
|---|---|
| 0 | E-paper CFG_STR — **cannot use as button** |
| 40 | E-paper STH — **cannot use as button** |
| 26–37 | Internal flash + PSRAM (OPI) — **physically unavailable** |
| EN | Hardware reset — not a GPIO |

> [!danger] ADC2 + WiFi Conflict
> Battery ADC is on **ADC2 (GPIO14)**. The ESP32-S3 **cannot use ADC2 while WiFi is active**. The firmware skips battery sampling when the portal is on and keeps the last reading.

## Button Gesture Timing

| Parameter | Value | Description |
|---|---|---|
| Debounce | 30 ms | Ignore contact bounce |
| Quick tap | ≤350 ms | Next page / move highlight |
| Medium hold | 350–750 ms | Previous page |
| Long hold | ≥750 ms | Open menu / select item |
| Burst window | 350 ms | Multi-tap accumulation window |

## See Also
- [[Hardware/Board Specs]]
- [[Hardware/Shopping List]]
- [[Architecture/App Framework]]
