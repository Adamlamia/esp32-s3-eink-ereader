# 📖 ESP32-S3 E-Ink E-Reader

A minimalist, hackable e-reader built on the **LILYGO T5 4.7" E-Paper S3**
(ESP32-S3). Load your own plain-text books, keep your place with automatic
"continue reading" and named bookmarks, and manage your library from a
**self-hosted website** — just connect your laptop or phone to the reader's
Wi-Fi and upload.

> Status: **v0.2.0 — running on hardware.** Reading, word-wrap pagination,
> bookmarks, the on-device menu/library, selectable font size and the Wi-Fi
> upload portal all work on the LILYGO T5 4.7" S3. See the
> [Roadmap](docs/ROADMAP.md) for what's next and a pile of ideas to make it
> your own.

---

## ✨ Features

- **Local upload website** — the device hosts its own Wi-Fi AP
  (`EReader-Setup`) and a web page at **http://ereader.local**. Drag-and-drop
  `.txt` files from your laptop straight onto the reader. No cloud, no cables.
- **Book storage** — books live on the **microSD card** when present, and fall
  back to internal flash (LittleFS) otherwise. Big libraries welcome.
- **Continue reading** — the last book and exact reading position are saved and
  restored automatically on boot, behind a personalised welcome screen (e-ink
  keeps the last page on screen even with the power off).
- **Selectable font size** — two book text sizes (Georgia ~14 pt / ~10 pt),
  toggled from the last row of the on-device library; the choice is remembered
  across reboots.
- **On-device menu & library** — hold the button while reading to open a menu
  (Resume · Library · Bookmark here · Wi-Fi on/off) and browse or switch books
  right on the device — no phone required.
- **Bookmarks** — drop a quick bookmark from the on-device menu ("Bookmark
  here"), or add named bookmarks from the web UI.
- **E-ink friendly** — word-wrapped pagination, periodic full-refresh to fight
  ghosting, and light-sleep power management for long battery life.
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
| 1× on-board button | Drives everything by hold-duration gestures (GPIO21) |
| 2× tactile buttons (optional) | External prev/next page — disabled by default (see pin map) |

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
5. Read with the single on-board button: **quick tap = next page**, **medium
   hold = previous page**, **long hold = menu**. See **Navigation** below.

> 💡 **Where to get free books:** [Project Gutenberg](https://www.gutenberg.org/)
> offers tens of thousands of public-domain titles as *Plain Text UTF-8*.

---

## 🎮 Navigation (one button)

Everything is driven by the single on-board button (GPIO21). Actions are chosen
by **how long you hold**:

**While reading**

| Gesture | Hold time | Action |
|---|---|---|
| Quick tap | < 350 ms | Next page |
| Medium hold | 350–750 ms | Previous page |
| Long hold | ≥ 750 ms | Open the menu |

**In the menu / library list**

| Gesture | Action |
|---|---|
| Single tap | Move the highlight box (wraps around) |
| Hold | Select / open the highlighted item |

- The **menu** (Resume · Library · Bookmark here · Wi-Fi upload) opens with a
  long hold while reading.
- The **library** lists your books between two virtual rows: row 0 is a
  **Wi-Fi upload: ON/OFF** toggle and the last row is a **Font size:
  Normal/Small** toggle. Tap to move the box, hold to open a book or flip a
  toggle.
- A fast double-tap is *not* used: a full e-ink refresh (~0.8 s) blocks button
  polling, so hold-duration bands are used instead for reliable input.
- Timings live in [`src/config.h`](src/config.h) (`BTN_PREVHOLD_MS`,
  `BTN_LONGPRESS_MS`). Optional external prev/next buttons can be enabled there.

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
  library. Firmware runs on the LILYGO T5 4.7" S3; if your board revision
  differs, verify the pin map in [docs/HARDWARE.md](docs/HARDWARE.md).
- Never commit real Wi-Fi passwords: put them in `src/secrets.h` (git-ignored).

## 📜 License
[MIT](LICENSE) © 2026 Muhammad Sufi Adam Bin Samsuhaimi
