// ===========================================================================
//  DevCompanionApp.cpp  —  reference viewer + GitHub dashboard UI (DEV·R1)
// ===========================================================================
//  Presentation + gesture routing only. Reuses:
//    core::parseRefsIndex   — refs_index.txt manifest parsing (host-tested)
//    core::GithubModel      — CiState + cache serialize/deserialize (host-tested)
//    GithubStore            — /github.json cache load
//    GithubSync             — on-demand Wi-Fi/NTP/HTTPS refresh
//    DisplayManager::blitRaw— full-screen 4-bpp framebuffer blit
//  Rendering mirrors the other apps: clearBuffer -> draw -> flush(true) (full
//  refresh on every screen change keeps the e-ink panel ghost-free). The
//  reference viewer is the one exception: it blits the image first (no clear)
//  so the whole panel becomes the image, then overlays the label plate.
// ===========================================================================
#include "DevCompanionApp.h"
#include "GithubStore.h"
#include "GithubSync.h"
#include "app/AppManager.h"
#include "core/CalendarDate.h"     // civilFromUtc (fixed-offset UTC+8 formatting)
#include "core/CalendarEvent.h"    // CAL_TZ_OFFSET_SEC / CAL_CLOCK_MIN_EPOCH

#include <FS.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <string>

// --- Small formatting helpers (fixed-offset UTC+8, mirrors WeatherApp) -------
static String pad2(unsigned v) {
    return v < 10 ? String("0") + String(v) : String(v);
}

// "YYYY-MM-DD HH:MM" local date/time for the last-sync stamp.
static String fmtDateTime(int64_t utc) {
    int64_t y; unsigned m, d, hh, mm, ss;
    core::civilFromUtc(utc, CAL_TZ_OFFSET_SEC, y, m, d, hh, mm, ss);
    return String((long)y) + "-" + pad2(m) + "-" + pad2(d) + " " + pad2(hh) + ":" + pad2(mm);
}

// --- Construction ----------------------------------------------------------
DevCompanionApp::DevCompanionApp(SystemContext &ctx) : _ctx(ctx) {}

// --- Lifecycle -------------------------------------------------------------
void DevCompanionApp::onEnter() {
    _section = Section::Refs;
    _screen  = Screen::Main;
    _menuSel = 0;
    _refIndex = 0;
    _ghHighlight = 0;

    scanRefs();          // /refs/refs_index.txt or alphabetical .raw fallback
    loadGithubCache();   // warm the GitHub cache (displayed when the user switches)
    renderCurrent();     // start on the References section
}

void DevCompanionApp::onExit() {
    // Nothing to persist: the refs index is re-scanned and the GitHub cache is
    // re-read on every onEnter; no radios were left on (GithubSync is RAII).
    _screen  = Screen::Main;
    _section = Section::Refs;
}

// --- References: index loading ---------------------------------------------
void DevCompanionApp::scanRefs() {
    _refCount = 0;
    _refIndex = 0;
    fs::FS &fs = _ctx.storage.fs();

    // 1. Prefer the manifest (refs_index.txt) when present + readable.
    if (fs.exists(REFS_INDEX_FILE)) {
        File f = fs.open(REFS_INDEX_FILE, "r");
        if (f) {
            size_t sz = f.size();
            // A manifest of REFS_MAX_ENTRIES lines is well under 1 KB; anything
            // larger is treated as corrupt and ignored (fallback enumeration).
            if (sz > 0 && sz < 4096) {
                std::string buf;
                buf.resize(sz);
                size_t rd = f.readBytes(&buf[0], sz);
                if (rd == sz)
                    _refCount = core::parseRefsIndex(buf.c_str(), _refs, REFS_MAX_ENTRIES);
            }
            f.close();
        }
    }
    if (_refCount > 0) {
        Serial.printf("[DevCompanion] refs: %d entr(ies) from manifest\n", _refCount);
        return;
    }

    // 2. Fallback: enumerate .raw files in /refs/ alphabetically. The FS may
    //    return entries in any order, so collect then insertion-sort by name.
    File dir = fs.open(REFS_DIR);
    if (dir && dir.isDirectory()) {
        for (File f = dir.openNextFile(); f && _refCount < REFS_MAX_ENTRIES;
             f = dir.openNextFile()) {
            String fn = String(f.name());
            int slash = fn.lastIndexOf('/');
            String base = (slash >= 0) ? fn.substring(slash + 1) : fn;
            String lower = base; lower.toLowerCase();
            if (lower.endsWith(".raw")) {
                core::RefsEntry &e = _refs[_refCount];
                core::refsEntryClear(e);
                strncpy(e.file, base.c_str(), REFS_FILE_MAX - 1);
                e.file[REFS_FILE_MAX - 1] = '\0';
                // label = filename sans extension (same fallback as the parser)
                core::refs_detail::labelFromFilename(e.file, e.label, REFS_LABEL_MAX);
                _refCount++;
            }
            f.close();
        }
        dir.close();

        // alphabetical insertion sort by filename (FS order is not guaranteed)
        for (int i = 1; i < _refCount; i++) {
            core::RefsEntry key = _refs[i];
            int j = i - 1;
            while (j >= 0 && strcmp(_refs[j].file, key.file) > 0) {
                _refs[j + 1] = _refs[j];
                j--;
            }
            _refs[j + 1] = key;
        }
        Serial.printf("[DevCompanion] refs: %d .raw file(s) enumerated\n", _refCount);
    } else {
        Serial.println("[DevCompanion] refs: no /refs/ directory");
        if (dir) dir.close();
    }
}

// --- References: rendering -------------------------------------------------
void DevCompanionApp::renderRefs() {
    if (_refCount == 0) { renderRefEmpty(); return; }
    if (_refIndex < 0 || _refIndex >= _refCount) _refIndex = 0;
    renderRefImage();
}

void DevCompanionApp::renderRefEmpty() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(70, "Dev Companion - References", 2);
    d.drawBookText(MARGIN_X, 200, "No reference images.");
    d.drawBookText(MARGIN_X, 236, "Copy .raw files to /refs/ on SD.");
    d.drawBookText(MARGIN_X, 264, "Make them with: python tools/make_refs.py <img>");
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14,
                   "MediumHold=GitHub   LongHold=menu");
    d.flush(true);
}

void DevCompanionApp::renderRefImage() {
    DisplayManager &d = _ctx.display;
    const core::RefsEntry &e = _refs[_refIndex];
    String path = String(REFS_DIR) + "/" + String(e.file);

    // Observability (DEV·R2): say WHICH image is going on the panel, so a blank
    // / wrong picture is diagnosable from serial alone (was errors-only before).
    Serial.printf("[DevCompanion] refs: show %d/%d '%s' (%s)\n",
                  _refIndex + 1, _refCount, e.label, path.c_str());

    // Read the .raw dump (259200 bytes) into a PSRAM buffer, then blit it into
    // the framebuffer. PSRAM (8 MB) easily holds one frame; the buffer is freed
    // before the flush so nothing lingers. A missing / corrupt (wrong-size) file
    // is NOT blitted — the framebuffer is never cleared on this path, so a short
    // dump would leave a stale/garbage tail on the panel; instead we fall
    // through to the readable placeholder below (DEV·R2 short-file fix).
    bool drawn = false;
    uint8_t *buf = (uint8_t *)ps_malloc(REFS_RAW_SIZE);
    if (!buf) {
        Serial.println("[DevCompanion] refs: ps_malloc failed (need PSRAM)");
    } else {
        File f = _ctx.storage.fs().open(path, "r");
        if (f) {
            size_t sz = f.size();
            // A valid dump is EXACTLY one framebuffer; anything else is corrupt
            // (truncated SD write / wrong converter) — reject, never partial-blit.
            if (!core::refsRawSizeValid(sz)) {
                Serial.printf("[DevCompanion] refs: %s bad size %u (want %u) - corrupt?\n",
                              path.c_str(), (unsigned)sz, (unsigned)REFS_RAW_SIZE);
            } else {
                size_t rd = f.readBytes((char *)buf, sz);
                if (rd == sz) { d.blitRaw(buf, rd); drawn = true; }
                else Serial.printf("[DevCompanion] refs: short read %u/%u\n",
                                   (unsigned)rd, (unsigned)sz);
            }
            f.close();
        } else {
            Serial.printf("[DevCompanion] refs: open(%s) failed\n", path.c_str());
        }
        free(buf);   // ps_malloc'd memory is released with the standard free()
    }

    if (!drawn) {
        d.clearBuffer();
        d.drawTextCentered(DISPLAY_HEIGHT / 2 - 30, String(e.label), 2);
        d.drawBookText(MARGIN_X, DISPLAY_HEIGHT / 2 + 24,
                       "Could not read " + path);
        d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14,
                       "Tap=next   MediumHold=GitHub   LongHold=menu");
        d.flush(true);
        return;
    }

    // --- Label overlay: a small white plate bottom-left, label text on it ----
    // The bundled fonts are ASCII-only and draw dark ink, so a white plate keeps
    // the label legible over any image (no white text is available).
    String label = String(e.label);
    int tw = d.textWidth(label, true);          // book-font width
    const int padX = 12;
    int plateH = d.readerLineHeight() + 12;
    int plateW = tw + padX * 2;
    int plateX = MARGIN_X;
    int plateY = DISPLAY_HEIGHT - plateH - 10;
    d.fillRectShade(plateX, plateY, plateW, plateH, 255);          // 255 = white
    d.drawBookText(plateX + padX, plateY + plateH - 8, label);

    // --- Small position plate bottom-right ("n/total") -----------------------
    String pos = String(_refIndex + 1) + "/" + String(_refCount);
    int pw = d.textWidth(pos, true);
    int pX = DISPLAY_WIDTH - MARGIN_X - pw - padX * 2;
    d.fillRectShade(pX, plateY, pw + padX * 2, plateH, 255);
    d.drawBookText(pX + padX, plateY + plateH - 8, pos);

    d.flush(true);   // full refresh: whole panel rewritten, kill ghosting
}

// --- GitHub: cache + auto-sync decision ------------------------------------
void DevCompanionApp::loadGithubCache() {
    GithubStore store(_ctx.storage.fs());
    _repoCount = store.load(_repos, GITHUB_MAX_REPOS, _ghLastSync);
    if (_repoCount < 0) _repoCount = 0;        // defensive (load never returns <0)
    if (_ghHighlight >= _repoCount) _ghHighlight = 0;
}

bool DevCompanionApp::shouldAutoSyncGithub() const {
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS) && defined(GITHUB_PAT)
    // Cheap pre-checks (mirror WeatherApp): never fight the portal over radio.
    if (_ctx.portal && _ctx.portal->isRunning()) return false;
    if (_ghLastSync == 0) return true;         // never synced -> try now
    // Staleness needs a valid clock (no battery-backed RTC; see WeatherApp).
    int64_t t = (int64_t)time(nullptr);
    if (t < CAL_CLOCK_MIN_EPOCH) return false;
    return (t - _ghLastSync) > GITHUB_STALE_SEC;
#else
    return false;   // no STA secrets and/or no PAT compiled in -> nothing to sync
#endif
}

// --- GitHub: rendering -----------------------------------------------------
String DevCompanionApp::ciGlyph(core::CiState ci) const {
    // ASCII markers only: the bundled FiraSans / ReaderFont have no glyph for
    // U+2713 / U+2717 (check / cross), so the prompt's "✓/✗" map to OK / FAIL.
    switch (ci) {
        case core::CiState::Success: return "OK";
        case core::CiState::Failure: return "FAIL";
        case core::CiState::Pending: return "...";
        case core::CiState::None:    return "-";
        default:                     return "?";    // Unknown
    }
}

String DevCompanionApp::ghLastSyncLine() const {
    if (_ghLastSync <= 0) return "Never synced - MediumHold to sync";
    return String("Last sync: ") + fmtDateTime(_ghLastSync);
}

void DevCompanionApp::renderGithub() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(50, "Dev Companion - GitHub", 2);
    d.drawBookText(MARGIN_X, 92, ghLastSyncLine());

#ifndef GITHUB_PAT
    // No PAT compiled in: empty state (the firmware still builds + runs).
    d.drawBookText(MARGIN_X, 200, "Set GITHUB_PAT in secrets.h");
    d.drawBookText(MARGIN_X, 236, "(plus GITHUB_REPO_0..3). Use a read-only PAT.");
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14,
                   "MediumHold=sync   LongHold=menu");
    d.flush(true);
    return;
#else
    if (_repoCount == 0) {
        d.drawBookText(MARGIN_X, 200, "No GitHub data yet.");
        d.drawBookText(MARGIN_X, 236, "MediumHold to sync now.");
        d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14,
                       "MediumHold=sync   LongHold=menu");
        d.flush(true);
        return;
    }
    if (_ghHighlight < 0 || _ghHighlight >= _repoCount) _ghHighlight = 0;

    // One line per repo: "owner/repo   PRs:N  Issues:N  CI:OK"
    int y  = 170;
    int lh = 60;
    for (int i = 0; i < _repoCount; i++) {
        const core::GithubRepoStatus &s = _repos[i];
        if (i == _ghHighlight)
            d.drawSelectionBox(MARGIN_X - 10, y - 34, d.usableWidth() + 20, 48);
        String line = String(s.name) + "   PRs:" + String(s.openPRs)
                    + "  Issues:" + String(s.openIssues)
                    + "  CI:" + ciGlyph(s.lastCi);
        d.drawBookText(MARGIN_X, y, line);
        y += lh;
    }
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 14,
                   "Tap=next   MediumHold=sync   LongHold=menu");
    d.flush(true);
#endif
}

// --- Shared rendering / navigation -----------------------------------------
void DevCompanionApp::renderCurrent() {
    if (_section == Section::Refs) renderRefs();
    else                           renderGithub();
}

void DevCompanionApp::switchSection(Section s) {
    _section = s;
    _screen  = Screen::Main;
    if (s == Section::GitHub) {
        loadGithubCache();
        // On-enter auto-sync: only when stale/empty AND the cheap pre-checks
        // pass (shouldAutoSyncGithub never touches the radio itself).
        if (shouldAutoSyncGithub()) {
            Serial.printf("[DevCompanion] GitHub on-open resync (sync=%lld)\n",
                          (long long)_ghLastSync);
            runSync();          // splash -> sync -> reload -> renderGithub
        } else {
            // Observability (DEV·R2): log WHY no auto-sync ran, so an empty/stale
            // panel is diagnosable from serial (fresh cache, unfixed clock, portal
            // up, or no secrets). The on-screen text covers the never-synced case.
            Serial.printf("[DevCompanion] GitHub on-open: auto-sync skipped "
                          "(sync=%lld repos=%d; fresh/unfixed-clock/portal/no-secrets)\n",
                          (long long)_ghLastSync, _repoCount);
            renderGithub();
        }
    } else {
        renderRefs();
    }
}

// --- Input -----------------------------------------------------------------
void DevCompanionApp::onButton(ButtonEvent ev) {
    if (_screen == Screen::Menu) {
        // Menu convention matches the other apps: Tap moves, LongHold selects.
        if (ev == ButtonEvent::Tap) {
            _menuSel = (_menuSel + 1) % MENU_COUNT;
            renderMenu();
        } else if (ev == ButtonEvent::LongHold) {
            menuSelect();
        }
        return;
    }

    // --- Main screen: LongHold always opens the menu ---
    if (ev == ButtonEvent::LongHold) { openMenu(); return; }

    if (_section == Section::Refs) {
        if (ev == ButtonEvent::Tap) {
            if (_refCount > 0) {
                _refIndex = (_refIndex + 1) % _refCount;   // next image, wrap
                renderRefs();
            }
            // Tap in the EMPTY state: nothing to cycle — deliberately ignored.
        } else if (ev == ButtonEvent::MediumHold) {
            switchSection(Section::GitHub);                // switch section
        }
    } else { // GitHub
        if (ev == ButtonEvent::Tap) {
            if (_repoCount > 0) {
                _ghHighlight = (_ghHighlight + 1) % _repoCount;  // next row, wrap
                renderGithub();
            }
        } else if (ev == ButtonEvent::MediumHold) {
            runSync();                                     // manual "Sync now"
        }
    }
}

// --- Menu ------------------------------------------------------------------
static String menuLabelFor(int i) {
    switch (i) {
        case 0:  return String("References");
        case 1:  return String("GitHub");
        default: return String("Back to Home");
    }
}

void DevCompanionApp::renderMenu() {
    DisplayManager &d = _ctx.display;
    d.clearBuffer();
    d.drawTextCentered(74, "Dev Companion Menu", 2);

    int lh = d.lineHeightFor(1) + 16;
    int x  = DISPLAY_WIDTH / 2 - 220;
    int y  = 210;
    for (int i = 0; i < MENU_COUNT; i++) {
        String label = menuLabelFor(i);
        if (i == _menuSel) {
            int w = d.textWidth(label, false);
            d.drawSelectionBox(x - 18, y - 36, w + 36, 52);
        }
        d.drawText(x, y, label, 1);
        y += lh;
    }
    d.drawBookText(MARGIN_X, DISPLAY_HEIGHT - 16, "Tap = move    Hold = select");
    d.flush(true);
}

void DevCompanionApp::openMenu() {
    _menuSel = 0;
    _screen  = Screen::Menu;
    renderMenu();
}

void DevCompanionApp::menuSelect() {
    if (_menuSel == 0) { switchSection(Section::Refs);   return; }
    if (_menuSel == 1) { switchSection(Section::GitHub); return; }
    // "Back to Home"
    if (_ctx.manager) _ctx.manager->requestHome();
}

// --- Actions ---------------------------------------------------------------
void DevCompanionApp::runSync() {
    _screen  = Screen::Main;
    _section = Section::GitHub;
    _syncing = true;                    // wantsSleep() == false for the duration
    _ctx.display.showMessage("GitHub", "Syncing... (Wi-Fi + NTP)");

    GithubSync sync(_ctx);
    GithubSyncResult r = sync.run();    // blocking; safe stack (own 24 KB task)
    _syncing = false;

    loadGithubCache();                  // pick up whatever the sync wrote

    Serial.printf("[DevCompanion] sync: ok=%d reposOk=%d failed=%d msg='%s'\n",
                  (int)r.ok, r.reposOk, r.reposFailed, r.message);
    _ctx.display.showMessage(r.ok ? "Sync OK" : "Sync failed", String(r.message));
    delay(1600);                        // let the user read the result
    renderGithub();
}
