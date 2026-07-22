# 📖 ESP32-S3 E-Ink E-Reader

A minimalist, hackable e-reader built on the **LILYGO T5 4.7" E-Paper S3**
(ESP32-S3). Load your own plain-text books, keep your place with automatic
"continue reading" and named bookmarks, and manage your library from a
**self-hosted website** — just connect your laptop or phone to the reader's
Wi-Fi and upload.

> Status: **v0.1.0 — early / functional scaffold.** The architecture, build
> system and web portal are in place. See the [Roadmap](docs/ROADMAP.md) for
> what's next and a pile of ideas to make it your own.

---

## ✨ Features

- **Local upload website** — the device hosts its own Wi-Fi AP
  (`EReader-Setup`) and a web page at **http://ereader.local**. Drag-and-drop
  `.txt` files from your laptop straight onto the reader. No cloud, no cables.
- **Book storage** — books live on the **microSD card** when present, and fall
  back to internal flash (LittleFS) otherwise. Big libraries welcome.
- **Continue reading** — the last book and exact reading position are saved and
  restored automatically on boot (e-ink keeps the last page on screen even with
  the power off).
- **Bookmarks** — drop a quick bookmark with a long-press of the BOOT button, or
  add named bookmarks from the web UI.
- **E-ink friendly** — word-wrapped pagination, periodic full-refresh to fight
  ghosting, and deep/light-sleep power management for long battery life.
- **OTA-ready partitions** — dual app slots so you can add over-the-air firmware
  updates later.

---

## 🧰 Hardware

| Item | Notes |
|------|-------|
| **LILYGO T5 4.7" E-Paper S3** | ESP32-S3, 16 MB flash, 8 MB PSRAM, ED047TC1 panel (960×540, 16-grey) |
| microSD card (optional) | FAT32, holds your book library |
| USB-C cable | Flashing + charging |
| Li-Po battery (optional) | For untethered reading |
| 2× tactile buttons (optional) | External prev/next page (see pin map) |

Full wiring and board notes: [docs/HARDWARE.md](docs/HARDWARE.md).

---

## 🚀 Getting started

### 1. Install tooling
- [Visual Studio Code](https://code.visualstudio.com/) + the
  [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension.

### 2. Build & flash the firmware
```bash
# from the project root
pio run                 # compile
pio run -t upload       # flash firmware over USB
pio device monitor      # watch serial logs (115200 baud)
```

### 3. Flash the web UI (LittleFS)
The upload website lives in `data/www/` and must be flashed to the filesystem:
```bash
pio run -t uploadfs
```

### 4. Read
1. Power on the reader.
2. On your laptop/phone, join Wi-Fi **`EReader-Setup`** (password `read1234`).
3. Open **http://ereader.local** (or the IP printed on the serial monitor).
4. Drag a `.txt` file onto the page. It appears in your library and on the
   device.
5. Use the BOOT button (short = next page, long = bookmark) or the optional
   external buttons to read.

> 💡 **Where to get free books:** [Project Gutenberg](https://www.gutenberg.org/)
> offers tens of thousands of public-domain titles as *Plain Text UTF-8*.

---

## 🗂 Project layout

```
.
├── platformio.ini          # build config (board, PSRAM, libs, partitions)
├── partitions.csv          # dual-OTA + large LittleFS layout
├── data/www/               # the local website (flashed to LittleFS)
│   ├── index.html
│   ├── style.css
│   └── app.js
├── src/
│   ├── main.cpp            # boot flow, buttons, sleep
│   ├── config.h            # pins, Wi-Fi, reader defaults — tweak here
│   ├── display/            # DisplayManager: e-paper framebuffer + text
│   ├── storage/            # BookStorage: SD / LittleFS abstraction
│   ├── bookmark/           # BookmarkManager: positions + named bookmarks
│   ├── reader/             # TextReader: word-wrap pagination
│   └── web/                # WebPortal: Wi-Fi AP + upload site + JSON API
└── docs/                   # hardware guide + roadmap / ideas
```

## 🔌 HTTP API (served by the device)

| Method | Path | Purpose |
|--------|------|---------|
| GET  | `/api/books` | List library + active storage |
| POST | `/api/upload` | Multipart `.txt` upload |
| POST | `/api/delete?path=…` | Delete a book |
| GET  | `/api/bookmarks?path=…` | List bookmarks + last position |
| POST | `/api/bookmark` | Add a named bookmark |

---

## ⚠️ Notes & caveats
- The board id in `platformio.ini` is a generic ESP32-S3 devkit profile tuned
  for the T5 4.7" S3 (PSRAM/flash). If your revision differs, see
  [docs/HARDWARE.md](docs/HARDWARE.md).
- Display calls target the [LilyGo-EPD47](https://github.com/Xinyuan-LilyGo/LilyGo-EPD47)
  library. Firmware has not yet been validated on physical hardware — treat
  v0.1.0 as a strong starting scaffold to flash and iterate on.
- Never commit real Wi-Fi passwords: put them in `src/secrets.h` (git-ignored).

## 📜 License
[MIT](LICENSE) © 2026 Muhammad Sufi Adam Bin Samsuhaimi
