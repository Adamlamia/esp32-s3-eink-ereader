---
title: Shopping List
tags: [hardware, shopping]
status: reference
---

# Hardware Shopping List

| Part | Spec | For |
|---|---|---|
| **Battery** | 3.7V Li-Po 1S, JST-PH 2.0mm 2-pin, ~1500–2000 mAh, with protection circuit | Power |
| **microSD** | 16 GB, Class 10, FAT32 | Storage |
| **Mic** | INMP441 I2S MEMS module | Voice Journal |
| **Tact switches** | 2× momentary normally-open (6×6mm or 12×12mm) + caps | Two-button nav |
| **Buzzer** *(optional)* | Bare passive buzzer (PWM-driven) — **NOT** KY-012 (that's active) | Audio cues |

## Wiring: Two-Button Nav

Each switch: one leg → GPIO, other → GND. `INPUT_PULLUP` — no external resistor needed.

```
GPIO 9  ──[ Button A ]── GND    (Forward)
GPIO 46 ──[ Button B ]── GND    (Back / Home)
```

> [!warning] Verify GPIO availability
> Confirm GPIO 9 and 46 are broken out on the board header before permanent soldering.

## See Also
- [[Hardware/Board Specs]]
- [[Hardware/Pin Map]]
