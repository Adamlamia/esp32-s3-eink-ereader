---
title: Reader App
tags: [feature, app]
status: active
batch: "1"
---

# E-Reader

> The core TXT book reader — the original purpose of the device.

## Overview

Reads `.txt` files from microSD (or LittleFS fallback). Supports pagination, bookmarks, font size toggle, and a library browser.

## Key Files

| File | Role |
|---|---|
| `src/apps/reader/ReaderApp.cpp` | App lifecycle + UI |
| `src/reader/TextReader.cpp` | Pagination + rendering |
| `src/core/Paginator.h` | Page break calculation (core seam) |
| `src/core/PageLayout.h` | Line/word layout metrics (core seam) |
| `src/storage/BookStorage.cpp` | SD + LittleFS abstraction |
| `src/bookmark/BookmarkManager.cpp` | Bookmark persistence |
| `src/display/ReaderFont.h` | Book body font data |
| `src/display/ReaderFontSmall.h` | Small font variant |

## Navigation (Single-Button)

| Gesture | Action |
|---|---|
| Quick tap (≤350ms) | Next page (instant on release) |
| Medium hold (350–750ms) | Previous page |
| Long hold (≥750ms) | Open menu |

## Menu Items

1. Resume reading (closes menu)
2. Library
3. Bookmark this page
4. Wi-Fi toggle

## Library Screen

- Lists all `.txt` files from `/books/`
- Virtual row at bottom: toggle font size (small/medium/large)
- Virtual row: Wi-Fi upload toggle
- Tap moves highlight, hold selects

## Configuration

| Define | Value | Description |
|---|---|---|
| `DISPLAY_WIDTH` | 960 | Screen width (px) |
| `DISPLAY_HEIGHT` | 540 | Screen height (px) |
| `MARGIN_X` | 26 | Side margins |
| `MARGIN_Y` | 20 | Top margin |
| `STATUS_H` | 30 | Status bar reserve |
| `DEFAULT_FONT_SIZE` | 1 | Default book font size |
| `FULL_REFRESH_EVERY` | 8 | Full-clear every N pages |

## Web Upload

Books can be uploaded via the web portal (`data/www/`). TXT only — no EPUB/PDF on-device conversion.

## See Also
- [[Architecture/App Framework]]
- [[Project/Project Overview]]
