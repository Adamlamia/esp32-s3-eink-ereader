---
title: Dev Companion App
tags: [feature, app, sync]
status: active
batch: "2"
---

# Dev Companion

> Reference viewer + read-only GitHub dashboard.

## Overview

Two modes:
1. **Reference viewer** — full-screen pinouts/schematics stored on SD as `.raw` files
2. **GitHub dashboard** — configurable repo list, shows open PRs + issues + last CI status

## Key Files

| File | Role |
|---|---|
| `src/apps/devcompanion/DevCompanionApp.cpp` | App lifecycle + UI |
| `src/apps/devcompanion/GithubStore.cpp` | JSON cache persistence |
| `src/apps/devcompanion/GithubSync.cpp` | GitHub API fetch + RAII WiFi |
| `src/core/RefsIndex.h` | Reference image index parsing (core seam) |
| `src/core/GithubModel.h` | GitHub API response parsing (core seam) |

## Configuration

| Define | Value | Description |
|---|---|---|
| `REFS_DIR` | `/refs` | SD dir for .raw images |
| `REFS_INDEX_FILE` | `/refs/refs_index.txt` | Optional manifest |
| `REFS_FILE_MAX` | 32 | Filename buffer |
| `REFS_LABEL_MAX` | 40 | Label buffer |
| `REFS_MAX_ENTRIES` | 16 | Max reference images |
| `REFS_RAW_SIZE` | 259200 | 960×540/2 bytes (4-bpp framebuffer) |
| `GITHUB_CACHE_FILE` | `/github.json` | Cache path |
| `GITHUB_NAME_MAX` | 40 | "owner/repo" buffer |
| `GITHUB_MAX_REPOS` | 4 | Max configured repos |
| `GITHUB_STALE_SEC` | 4h | On-open resync threshold |
| `GITHUB_BODY_MAX` | 8192 | API body cap |

## Secrets

```
#define GITHUB_PAT       "ghp_..."
#define GITHUB_REPO_0    "Adamlamia/esp32-s3-eink-ereader"
```

## Reference Image Format

`.raw` files are 4-bpp framebuffer dumps (259,200 bytes each). Prepared by a host-side Pillow conversion script. Fit-to-screen, no zoom.

## Tests

Native Unity tests cover `RefsIndex` and `GithubModel` parsing.

## See Also
- [[Architecture/Core Logic Seams]]
