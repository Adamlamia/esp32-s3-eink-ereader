---
title: QR Toolkit
tags: [feature, app]
status: active
batch: "1"
---

# QR Toolkit

> Generate and display QR codes: WiFi, DuitNow (EMVCo), URL, text.

## Overview

Carousel of QR entries. Tap cycles between types. WiFi QR auto-generated from credentials. DuitNow QR: decode EMVCo payload + regenerate as scannable QR.

## Key Files

| File | Role |
|---|---|
| `src/apps/qr/QrApp.cpp` | App lifecycle + UI + QR rendering |
| `src/core/Emvco.h` | EMVCo TLV encode/decode (core seam) |
| `src/core/QrPayload.h` | QR payload formatting (core seam) |

## Configuration

| Define | Value | Description |
|---|---|---|
| `QR_MAX_ENTRIES` | 8 | Max carousel entries |
| `QR_LABEL_MAX` | 32 | Entry label buffer |
| `QR_PAYLOAD_MAX` | 320 | Payload buffer (fits QR v13) |
| `QR_WIFI_QR_MAX` | 192 | WiFi QR string buffer |
| `QR_EMVCO_MAX_FIELDS` | 16 | Max TLV fields decoded |
| `QR_EMVCO_VALUE_MAX` | 100 | Field value buffer |
| `QR_EMVCO_PAYLOAD_MAX` | 384 | EMVCo build buffer |
| `QR_MIN_VERSION` | 3 | Smallest QR version |
| `QR_MAX_VERSION` | 13 | Largest (425 byte-mode chars) |

## QR Types

1. **WiFi QR** — auto-generated from `WIFI_STA_SSID`/`WIFI_STA_PASS`
2. **DuitNow QR** — EMVCo payload, CRC validated, rendered back
3. **URL / Text QR** — configured via `secrets.h`

## Secrets

```
#define QR_PAYLOAD_0   "https://example.com"
#define QR_LABEL_0     "My Label"
#define QR_PAYLOAD_1   "00020101021126590014MY.GOV..."  // DuitNow EMVCo
#define QR_LABEL_1     "DuitNow"
```

## Tests

Native Unity tests cover `Emvco` encode/decode + `QrPayload` formatting.

## See Also
- [[Architecture/Core Logic Seams]]
