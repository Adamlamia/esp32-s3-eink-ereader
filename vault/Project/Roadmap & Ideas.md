---
title: Roadmap & Ideas
tags: [roadmap, ideas]
status: reference
---

# Roadmap & Creative Ideas

> Blue-sky wishlist — ideas, **not** the locked plan. For the locked plan see [[Project/Project Overview]].

Legend: 🟢 easy · 🟡 medium · 🔴 ambitious

## Core Reading Experience
- 🟢 Font size cycling — expose `TextReader::setFontSize()` on a button
- 🟢 Multiple fonts — serif (reading) + sans (UI)
- 🟡 Day / night mode — invert ink/paper
- 🟡 Justified text + hyphenation
- 🟡 Table of contents / chapter jump
- 🟡 In-book search — stream-scan, no full load
- 🟢 Reading progress % — progress bar + time estimate

## Library & Formats
- 🟡 EPUB support (basic) — unzip, strip tags, read text
- 🟡 Markdown / HTML rendering
- 🟢 Cover thumbnails in web library
- 🟡 Server-side conversion (PDF/EPUB → clean .txt)
- 🟢 "Send to reader" bookmarklet

## Web Portal
- 🟡 OTA firmware updates — `/update` page
- 🟢 Wi-Fi captive portal — auto-pop setup page
- 🟢 Reorder / rename / tag books from web UI
- 🟡 Reading stats dashboard
- 🟡 Bookmark & highlight sync

## E-ink Always-On Screens
- 🟢 Sleep screen = current book cover
- 🟡 Clock / calendar face — wake on RTC, partial-refresh
- 🟡 Daily quote / word-of-the-day
- 🟡 Weather dashboard
- 🔴 Photo frame mode — dither to 16-grey

## Input & Interaction
- ✅ Single-button gesture UI (done)
- 🟢 More gesture shortcuts
- 🟡 Capacitive touch strip
- 🟡 Accelerometer page-turn
- 🔴 Voice memo highlights

## Comfort & Power
- 🟢 Deep-sleep timer — auto-off after N minutes
- 🟡 Front-light control — PWM dimming
- 🟡 Battery health screen
- 🟡 Adaptive refresh — threshold-based full-clear

## Connected / Smart
- 🟡 RSS / newspaper mode
- 🟡 Gutenberg browser
- 🟡 Pocket / Wallabag integration
- 🔴 Home-assistant panel
- 🔴 BLE page-turner

## Polish & Project Health
- 🟢 GitHub Actions CI
- 🟢 Release binaries — ESP Web Tools
- 🟢 Screenshots / photos in README
- 🟡 Unit tests for pagination/word-wrap
