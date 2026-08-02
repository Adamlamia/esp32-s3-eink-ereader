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
- **QR Toolkit** — show Wi-Fi, DuitNow payment and URL/text QR codes full-
  screen on the e-ink; tap cycles entries, long-hold opens the menu. Payloads
  live in `src/secrets.h` only; the Wi-Fi password is never shown on screen.
- **Todo (Tasks calendar)** — a checklist synced from a dedicated "Tasks"
  Google Calendar via the same secret-ICS-URL mechanism (zero new auth).
  Tap moves, medium-hold toggles done, long-hold opens the menu. Done-state
  is device-local only (`/todo.json`); tasks are edited on your phone.
- **Agenda (split-view launcher)** — the home screen is a split view: the
  app list on the left, and **today's timeline** on the right, merged from
  the calendar cache (`/calendar.json`) by a pure, host-tested seam
  (`src/core/AgendaMerge.h`) — no new network code. All-day items lead
  (alphabetical), then timed events as `HH:MM  Title` with the **next item
  highlighted**. A clean slot for Tasks is reserved ("pending backend")
  while the Todo app is deferred.

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

## 🌤 Weather (Open-Meteo)

The launcher's **Weather** app shows current conditions (temperature, weather
glyph, feels-like, humidity, wind) plus a **3-day forecast** for your
location, fetched from [Open-Meteo](https://open-meteo.com/) — **free, no API
key**. The last snapshot is cached at `/weather.json` on the active
filesystem; the app re-fetches automatically when you open it with a cache
older than 3 h (battery- and portal-guarded), and a **tap** refreshes on
demand (long-hold opens the menu: *Refresh now* · *Back to Home*). It reuses
the calendar's Wi-Fi/NTP/HTTPS lifecycle: STA-only, portal-guarded, and the
radio is always powered off afterwards. Without any configuration it targets
**Kuala Lumpur**; to point it at your location, add to `src/secrets.h`
(git-ignored — never commit it):

```c
#define WEATHER_LAT    3.1390f          // degrees, +N/-S
#define WEATHER_LON    101.6869f        // degrees, +E/-W
#define WEATHER_LABEL  "Kuala Lumpur"   // shown on screen (<= 23 chars)
```

All three lines are optional — the firmware is fully functional without them.
Wi-Fi still requires `WIFI_STA_SSID` / `WIFI_STA_PASS` in `src/secrets.h`;
without those the app shows its empty state and refresh reports
"No Wi-Fi secrets (src/secrets.h)".

---

## 🔳 QR Toolkit

The launcher's **QR Toolkit** app renders a carousel of QR codes full-screen
on the e-ink: **tap** cycles entries (wraps), **long-hold** opens the menu
(*Back to Home*). Each entry is drawn centred with its label and a
payload-type caption:

- **Wi-Fi QR** — generated automatically from `WIFI_STA_SSID` /
  `WIFI_STA_PASS` (zxing `WIFI:` format; the specials `\ ; , : "` are
  escaped in SSID and password; open networks use `T:nopass`). The password
  is **never drawn on screen** — it lives only in `src/secrets.h` -> RAM ->
  QR bitmap.
- **DuitNow / EMVCo payment QR** — paste a raw EMVCo QRPS payload; the app
  validates its structure and CRC16-CCITT trailer (tag 63) and captions it
  as a payment QR.
- **URL / free-text QR** — anything else (http(s) payloads are captioned
  URL, everything else Text).

Add entries in `src/secrets.h` (git-ignored — never commit it); every line
is optional:

```c
#define QR_PAYLOAD_0   "https://example.com/menu"
#define QR_LABEL_0     "Cafe menu"
#define QR_PAYLOAD_1   "00020101021126...6304ABCD"   // raw EMVCo/DuitNow QRPS
#define QR_LABEL_1     "DuitNow"
// ... up to QR_PAYLOAD_7 / QR_LABEL_7
```

With neither Wi-Fi creds nor any `QR_PAYLOAD_n`, the app shows a helpful
empty-state screen — the firmware still builds and runs.

## ✅ Todo (Tasks calendar)

The launcher's **Todo** app renders a dedicated **"Tasks" Google Calendar**
as an e-ink checklist: every **all-day event** is one task (its title is the
task text); timed events are ignored. You add / edit / delete tasks in the
Google Calendar app on your phone and the device picks them up on the next
sync — it reuses the calendar's existing ICS mechanism (**zero new auth**):
STA-only Wi-Fi, portal-guarded, NTP-time-fixed, HTTPS GET, and the radio is
always powered off afterwards. The last sync is cached at `/todo.json` on
the active filesystem; the app re-fetches automatically when you open it
with a cache older than 6 h (battery- and portal-guarded), and a long-hold
opens the menu (**Sync now** · **Back to Home**).

| Gesture (main screen) | Action |
|---|---|
| Tap | Move the highlight (wraps; the list auto-pages to follow) |
| Medium hold | Toggle "done" on the highlighted task — saved immediately |
| Long hold | Open the menu |

**Done-state is device-local only:** toggles are stored in `/todo.json` and
are **never** pushed back to Google. Completed tasks are matched by the ICS
`UID`, so they stay ticked even if you rename the task on your phone; stale
done-keys are pruned on the next sync when a task disappears.

Point it at your own Tasks calendar with its **secret ICS address**
(Google Calendar → Settings → the calendar → "Secret address in iCal
format"), added to `src/secrets.h` (git-ignored — never commit it):

```c
#define TODO_ICS_URL    "https://calendar.google.com/calendar/ical/.../basic.ics"
#define TODO_ICS_LABEL  "Tasks"   // shown on screen (defaults to "Tasks")
```

Both lines are optional — without `TODO_ICS_URL` the firmware is fully
functional and the app shows "No Tasks calendar (set TODO_ICS_URL)". Wi-Fi
still requires `WIFI_STA_SSID` / `WIFI_STA_PASS` in `src/secrets.h`.

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
