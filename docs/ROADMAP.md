# 🗺️ Roadmap & Creative Ideas

This file is the fun part. Below are features and project directions, grouped
from "finish the core reader" to "wild but doable on an ESP32-S3." Pick what
excites you — each is written as a self-contained mini-project.

Legend: 🟢 easy · 🟡 medium · 🔴 ambitious

---

## 1. Core reading experience
- 🟢 **Font size cycling** — expose the existing `TextReader::setFontSize()` on a
  button so you can go small/medium/large on the fly.
- 🟢 **Multiple fonts** — bundle a serif (reading) + sans (UI) font; let users
  switch in settings. LilyGo-EPD47 fonts are generated from TTFs.
- 🟡 **Day / night mode** — invert ink/paper for a "dark mode" page; great in
  low light with a front-light mod.
- 🟡 **Justified text + hyphenation** — nicer typographic pages.
- 🟡 **Table of contents / chapter jump** — detect `Chapter N` headings and let
  users jump.
- 🟡 **In-book search** — find a phrase and jump to the page (stream-scan the
  file, no full load).
- 🟢 **Reading progress %** — you already estimate total pages; show a progress
  bar and "~12 min left in chapter" using a words-per-minute setting.

## 2. Library & formats
- 🟡 **EPUB support (basic)** — an EPUB is a ZIP of XHTML. Unzip on-device,
  strip tags, and read the text. Start with un-DRM'd, simple EPUBs.
- 🟡 **Markdown / HTML rendering** — headings, bold, bullet lists.
- 🟢 **Cover thumbnails** — show a small image per book in the web library.
- 🟡 **Server-side conversion** — a tiny helper script on your laptop that
  converts PDF/EPUB → clean `.txt` before upload (keeps the MCU simple).
- 🟢 **"Send to reader" bookmarklet** — highlight text on any web page and push
  it to the device to read later (a read-it-later pocket clone).

## 3. The web portal, leveled up
- 🟡 **OTA firmware updates** — you already have dual-OTA partitions; add an
  `/update` page to upload new firmware from the browser (`Update.h`).
- 🟢 **Wi-Fi captive portal** — auto-pop the setup page when you join the AP;
  let users enter home Wi-Fi credentials (saved to NVS, not git).
- 🟢 **Reorder / rename / tag books** from the web UI.
- 🟡 **Reading stats dashboard** — pages/day, streaks, time read, exportable as
  JSON/CSV.
- 🟡 **Bookmark & highlight sync** — export/import your `bookmarks.json` so it
  survives re-flashing, or sync to a laptop folder.

## 4. E-ink "always-on" screens (zero-power art)
Because e-ink holds an image with no power, the sleeping reader can be a
display object:
- 🟢 **Sleep screen = current book cover** — looks like a real Kindle.
- 🟡 **Clock / calendar face** — wake on RTC, partial-refresh the time, sleep.
- 🟡 **Daily quote / word-of-the-day** — pull one line from a local file (or the
  net once a day) and show it.
- 🟡 **Weather dashboard** — fetch a forecast over Wi-Fi each morning.
- 🔴 **Photo frame mode** — dither uploaded images to 16-grey and display them.

## 5. Input & interaction
- 🟢 **Gesture buttons** — double-press = bookmark menu, triple = library.
- 🟡 **Capacitive touch strip** — tap edges to turn pages (ESP32-S3 has touch
  pins).
- 🟡 **Accelerometer page-turn** — tilt to flip (add an MPU-6050/LIS3DH).
- 🔴 **Voice memo highlights** — attach a mic + record short notes tied to a
  bookmark offset.

## 6. Comfort & power
- 🟢 **Deep-sleep timer** — auto-off after N minutes; wake to the exact page.
- 🟡 **Front-light control** — add addressable warm LEDs + PWM dimming for night
  reading.
- 🟡 **Battery health screen** — voltage, %, estimated hours left.
- 🟡 **Adaptive refresh** — full-refresh only when ghosting crosses a threshold.

## 7. Connected / "smart" ideas
- 🟡 **RSS / newspaper mode** — fetch feeds each morning, format into a "daily
  edition" you read like a book.
- 🟡 **Gutenberg browser** — search Project Gutenberg from the device and
  download `.txt` directly over Wi-Fi.
- 🟡 **Pocket / Wallabag integration** — sync your read-it-later queue.
- 🔴 **Home-assistant panel** — double as an e-ink dashboard when docked/charging.
- 🔴 **BLE page-turner** — pair a cheap BLE remote/pedal for hands-free reading.

## 8. Polish & project health
- 🟢 **GitHub Actions CI** — `pio run` on every push to catch build breaks.
- 🟢 **Release binaries** — attach compiled firmware to GitHub Releases for
  one-click flashing via [ESP Web Tools](https://esphome.github.io/esp-web-tools/).
- 🟢 **Screenshots / photos** in the README once it's on hardware.
- 🟡 **Unit tests** for the pagination/word-wrap logic (native `pio test`).

---

## Suggested next 3 steps
1. **Get it on hardware**: `pio run -t upload && pio run -t uploadfs`, confirm
   the panel, SD and Wi-Fi portal all come up; fix pin/board specifics.
2. **Font size + day/night** buttons — quick wins that make it feel real.
3. **OTA update page** — so every future improvement is a browser upload, no
   cable.

Have an idea that's not here? Open an issue and describe it — this list is meant
to grow.
