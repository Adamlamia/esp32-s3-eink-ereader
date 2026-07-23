"""Render the ESP32-S3 E-Ink E-Reader code-review as a multi-page PDF.

Mirrors tools/make_manual.py: Pillow-only (no extra deps), US-Letter raster
pages at 150 DPI saved together into a single PDF. Content is the CSPMO review
of the firmware / web UI / build / docs (source of truth: config.h + main.cpp).
"""
import os
from PIL import Image, ImageDraw, ImageFont

# --- Page geometry (US Letter @ 150 DPI) -----------------------------------
DPI = 150
PAGE_W = int(8.5 * DPI)   # 1275
PAGE_H = int(11 * DPI)    # 1650
MARGIN = 110

INK = (30, 30, 32)
SUBTLE = (110, 110, 116)
ACCENT = (35, 90, 140)
RULE = (205, 205, 210)
BAND = (238, 240, 244)
WHITE = (255, 255, 255)

# Tag palette (fill, text)
TAGS = {
    "CRITICAL":     ((183, 28, 28), WHITE),
    "SUGGESTION":   ((176, 120, 10), WHITE),
    "NICE-TO-HAVE": ((90, 96, 104), WHITE),
    "VERIFIED":     ((30, 110, 70), WHITE),
    "SURFACED":     ((90, 40, 120), WHITE),
}

OUT_PDF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "docs", "Code-Review-Report.pdf")
FONT_DIR = r"C:\Windows\Fonts"


def font(name, size):
    try:
        return ImageFont.truetype(os.path.join(FONT_DIR, name), size)
    except Exception:
        return ImageFont.load_default()


def wrap_text(draw, text, fnt, max_w):
    """Greedy word-wrap using a live ImageDraw for measurement."""
    words, lines, cur = text.split(), [], ""
    for w in words:
        trial = w if not cur else cur + " " + w
        if draw.textlength(trial, font=fnt) <= max_w:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


F_TITLE = font("arialbd.ttf", 58)
F_SUB = font("arial.ttf", 28)
F_H = font("arialbd.ttf", 33)
F_SH = font("arialbd.ttf", 24)
F_BODY = font("arial.ttf", 22)
F_BODY_B = font("arialbd.ttf", 22)
F_SMALL = font("arial.ttf", 17)
F_MONO = font("consola.ttf", 19)
F_TAG = font("arialbd.ttf", 17)


class Page:
    def __init__(self, pages, title=None):
        self.img = Image.new("RGB", (PAGE_W, PAGE_H), WHITE)
        self.d = ImageDraw.Draw(self.img)
        self.y = MARGIN
        self.pages = pages
        pages.append(self.img)
        if title:
            self.header(title)

    def _wrap(self, text, fnt, max_w):
        return wrap_text(self.d, text, fnt, max_w)

    def space(self, px):
        self.y += px

    def _room(self, need):
        """Start a fresh page if fewer than `need` px remain."""
        if self.y > PAGE_H - need:
            p = Page(self.pages)
            self.img, self.d, self.y = p.img, p.d, p.y

    def header(self, title):
        self.d.text((MARGIN, self.y), title, font=F_SH, fill=SUBTLE)
        self.y += 34
        self.d.line((MARGIN, self.y, PAGE_W - MARGIN, self.y), fill=RULE, width=2)
        self.y += 24

    def heading(self, text):
        self._room(200)
        self.d.text((MARGIN, self.y), text, font=F_H, fill=ACCENT)
        self.y += 50

    def subheading(self, text):
        self._room(120)
        self.space(4)
        self.d.text((MARGIN, self.y), text, font=F_SH, fill=INK)
        self.y += 36

    def para(self, text, fnt=F_BODY, color=INK, indent=0):
        max_w = PAGE_W - 2 * MARGIN - indent
        for line in self._wrap(text, fnt, max_w):
            self._room(80)
            self.d.text((MARGIN + indent, self.y), line, font=fnt, fill=color)
            self.y += fnt.size + 9
        self.y += 5

    def bullet(self, text, fnt=F_BODY):
        max_w = PAGE_W - 2 * MARGIN - 40
        lines = self._wrap(text, fnt, max_w)
        self._room(70)
        self.d.ellipse((MARGIN + 4, self.y + 9, MARGIN + 13, self.y + 18), fill=ACCENT)
        for line in lines:
            self._room(70)
            self.d.text((MARGIN + 34, self.y), line, font=fnt, fill=INK)
            self.y += fnt.size + 9
        self.y += 3

    def code(self, text):
        self._room(90)
        w = self.d.textlength(text, font=F_MONO)
        w = min(w, PAGE_W - 2 * MARGIN - 28)
        self.d.rounded_rectangle((MARGIN, self.y, MARGIN + w + 28, self.y + 34),
                                 radius=7, fill=BAND)
        self.d.text((MARGIN + 14, self.y + 7), text, font=F_MONO, fill=(40, 60, 90))
        self.y += 46

    def tag(self, label):
        self._room(120)
        tw = self.d.textlength(label, font=F_TAG)
        fill, txt = TAGS[label]
        self.d.rounded_rectangle((MARGIN, self.y, MARGIN + tw + 24, self.y + 28),
                                 radius=14, fill=fill)
        self.d.text((MARGIN + 12, self.y + 5), label, font=F_TAG, fill=txt)
        self.y += 38

    def finding(self, tag, title, where, why, fix):
        self.tag(tag)
        self.para(title, fnt=F_BODY_B)
        self.para("Where: " + where, fnt=F_SMALL, color=SUBTLE)
        self.para("Why: " + why)
        if fix:
            self.para("Fix: " + fix)
        self.space(6)
        self._room(60)
        self.d.line((MARGIN, self.y, PAGE_W - MARGIN, self.y), fill=RULE, width=1)
        self.space(12)

    def note(self, text):
        max_w = PAGE_W - 2 * MARGIN - 40
        lines = self._wrap(text, F_SMALL, max_w)
        h = 22 + len(lines) * (F_SMALL.size + 7)
        self._room(h + 40)
        self.d.rounded_rectangle((MARGIN, self.y, PAGE_W - MARGIN, self.y + h),
                                 radius=9, fill=(245, 247, 250))
        self.d.rectangle((MARGIN, self.y, MARGIN + 6, self.y + h), fill=ACCENT)
        yy = self.y + 11
        for line in lines:
            self.d.text((MARGIN + 24, yy), line, font=F_SMALL, fill=(70, 80, 95))
            yy += F_SMALL.size + 7
        self.y += h + 16


def build():
    pages = []

    # ---- Cover -----------------------------------------------------------
    cover = Image.new("RGB", (PAGE_W, PAGE_H), WHITE)
    d = ImageDraw.Draw(cover)
    d.rectangle((0, 0, PAGE_W, 340), fill=BAND)
    d.text((MARGIN, 120), "Code Review Report", font=F_TITLE, fill=INK)
    d.text((MARGIN, 205), "ESP32-S3 E-Ink E-Reader  -  firmware, web UI, build & docs",
           font=F_SUB, fill=ACCENT)
    d.text((MARGIN, 420),
           "Scope: end-to-end CSPMO audit (Correctness, Security, Performance,",
           font=F_BODY, fill=INK)
    d.text((MARGIN, 452),
           "Maintainability, Operational readiness).", font=F_BODY, fill=INK)
    d.text((MARGIN, 500),
           "Source of truth: src/config.h + src/main.cpp runtime behavior.",
           font=F_BODY, fill=INK)
    d.text((MARGIN, 548),
           "Roadmap items reviewed for labeling accuracy, not treated as bugs.",
           font=F_BODY, fill=INK)

    d.rounded_rectangle((MARGIN, 640, PAGE_W - MARGIN, 900), radius=12,
                        outline=RULE, width=2)
    d.text((MARGIN + 24, 664), "Findings at a glance", font=F_SH, fill=INK)
    rows = [
        ("CRITICAL", "1", "Unrestricted file delete via /api/delete (no path validation)"),
        ("SUGGESTION", "5", "Battery underflow, silent upload errors, save churn, doc, bookmark path"),
        ("NICE-TO-HAVE", "4", "Font-size no-op, long-word break, redundant scans, unused define"),
        ("SURFACED", "2", "Hardware-validation doc claim; hardcoded AP password (unchanged)"),
    ]
    yy = 712
    for tg, n, desc in rows:
        fill, txt = TAGS[tg]
        d.rounded_rectangle((MARGIN + 24, yy, MARGIN + 24 + 150, yy + 26),
                            radius=13, fill=fill)
        d.text((MARGIN + 36, yy + 4), tg, font=F_TAG, fill=txt)
        d.text((MARGIN + 190, yy + 2), n, font=F_BODY_B, fill=INK)
        for i, ln in enumerate(wrap_text(d, desc, F_SMALL, PAGE_W - 2 * MARGIN - 260)):
            d.text((MARGIN + 230, yy + i * 22), ln, font=F_SMALL, fill=SUBTLE)
        yy += 46

    d.line((MARGIN, 1360, PAGE_W - MARGIN, 1360), fill=RULE, width=2)
    d.text((MARGIN, 1380),
           "Firmware v0.1.0  -  LILYGO T5 4.7\" E-Paper S3  -  reviewer: senior "
           "embedded firmware auditor",
           font=F_SMALL, fill=SUBTLE)
    pages.append(cover)

    T = "ESP32-S3 E-Ink E-Reader  -  Code Review Report"

    # ---- 1. Summary + verified ------------------------------------------
    p = Page(pages, T)
    p.heading("1. Executive summary")
    p.para("The firmware is clean, well-layered, and honors the platform's "
           "known pitfalls. The single-button gesture state machine, pixel-"
           "accurate word-wrap, boot/resume rebuild, light-sleep and Wi-Fi "
           "auto-shutoff all match config.h. One Critical security hole exists "
           "in the web delete API; the remainder are robustness polish and one "
           "stale documentation claim.")
    p.subheading("Verified correct (no action needed)")
    p.bullet("Gesture bands in handleButtons() match BTN_DEBOUNCE_MS=30 / "
             "BTN_PREVHOLD_MS=350 / BTN_LONGPRESS_MS=750, including menu/library "
             "tap-move + hold-select and the virtual Wi-Fi row (g_librarySel==0).")
    p.bullet("get_text_bounds is always called with int32_t cursor pointers "
             "(textWidth, drawTextCentered) - never literals.")
    p.bullet("Battery (ADC2 / GPIO14) is sampled only while the Wi-Fi portal is "
             "off; last good value is retained otherwise.")
    p.bullet("Full-refresh per reader page (render -> flush(true)); GPIO0/GPIO40 "
             "never repurposed; PSRAM framebuffer (~253 KB) guarded with a null check.")
    p.bullet("layoutPage measures the composed line string, preventing right-"
             "edge clipping - the documented rationale is correct.")
    p.bullet("Docs match code: gesture timings, GPIO map, sleep 120s, Wi-Fi 10min, "
             ".txt-only policy, v0.1.0, and the HTTP API table. secrets.h is git-"
             "ignored and absent. OTA partitions and roadmap items are labeled as "
             "planned/later, never as working.")

    # ---- 2. Critical -----------------------------------------------------
    p = Page(pages, T)
    p.heading("2. Critical")
    p.finding(
        "CRITICAL",
        "C1 - /api/delete removes any file on the filesystem (no path validation)",
        "src/web/WebPortal.cpp (POST /api/delete handler)",
        "path is taken verbatim from the query string and passed straight to "
        "storage.remove(path). Any client on the AP can delete /bookmarks.json, "
        "/settings.json, /www/index.html or (on SD) any absolute path. The upload "
        "path is sanitized; delete is not.",
        "Restrict to a .txt file directly under BOOKS_DIR - no \"..\", no nested "
        "dirs - via a shared isBookPath() helper reused by the bookmark routes (S5). "
        "Reject others with HTTP 400.")

    # ---- 3. Suggestions --------------------------------------------------
    p = Page(pages, T)
    p.heading("3. Suggestions")
    p.finding(
        "SUGGESTION",
        "S1 - Battery percent underflows for a low battery, reporting 100%",
        "src/main.cpp readBatteryPercent()",
        "mv is uint32_t; below 3300 mV the (mv - 3300) subtraction wraps to a huge "
        "unsigned value, so after constrain() a near-empty battery displays as 100% "
        "- the opposite of reality.",
        "Do the arithmetic signed (extract batteryPercentFromMv(int) so it is unit-"
        "testable).")
    p.finding(
        "SUGGESTION",
        "S2 - Upload handler swallows open/write failures and always returns ok",
        "src/web/WebPortal.cpp (POST /api/upload)",
        "If createForUpload fails or a write is short (disk full), data is silently "
        "dropped and the completion callback still reports success. Violates \"no "
        "silent catch\".",
        "Track outcome in file-scope statics and respond 500 / ok:false; log the "
        "reason. Note the single static File handle allows only one upload at a time.")
    p.finding(
        "SUGGESTION",
        "S3 - Reading position re-serialized and written to flash on every render",
        "TextReader::render() + BookmarkManager::setLastPosition/save",
        "Each render() re-parses, mutates, re-serializes and rewrites bookmarks.json - "
        "even on menu-return, bookmark-drop and font-change, which reuse the same "
        "offset. That is heap churn in the render path plus needless LittleFS wear.",
        "Skip the persist when the offset is unchanged (minimal). A resident parsed "
        "document is a larger, separate refactor.")
    p.finding(
        "SUGGESTION",
        "S4 - Web UI tells users to \"triple-tap\" (no such gesture exists)",
        "data/www/index.html (\"How to add a book\" step)",
        "The device has no triple-tap; opening the library is a long-press -> Library "
        "(or you are already in the library when no book is open). Contradicts "
        "handleButtons() and the README navigation table.",
        "Reword to the actual hold gestures: long-press to open the menu, choose "
        "Library, tap to move, long-press to open a book.")
    p.finding(
        "SUGGESTION",
        "S5 - /api/bookmark and /api/bookmarks accept an arbitrary path",
        "src/web/WebPortal.cpp (bookmark routes)",
        "Lower risk than delete (no removal) but an arbitrary path lets a client "
        "inflate bookmarks.json with keys for non-existent books.",
        "Gate both routes with the same isBookPath() helper from C1.")

    # ---- 4. Nice-to-have + surfaced -------------------------------------
    p = Page(pages, T)
    p.heading("4. Nice-to-have")
    p.bullet("N1 - drawText ignores fontSize: \"size 2\" titles render at the single "
             "FiraSans size (acknowledged TODO; centering uses matching bounds, so "
             "cosmetic).")
    p.bullet("N2 - A single token wider than usableWidth (~908 px) is placed as-is and "
             "clips the right edge, because the wrap check is guarded by line.length(). "
             "Rare in prose; a char-break fallback would close it.")
    p.bullet("N3 - Redundant listBooks() directory scans on every library navigation "
             "(2-3x per redraw). Dominated by the e-ink refresh, so low impact.")
    p.bullet("N4 - SETTINGS_FILE is defined in config.h but never referenced.")

    p.heading("5. Surfaced for human decision (not changed)")
    p.note("Hardware-validation claims. The README states \"v0.1.0 - running on "
           "hardware\" and that features \"all work on the LILYGO T5 4.7\" S3\". This "
           "cannot be verified from code, and the brief says to surface - not silently "
           "edit - such claims. Confirm on the bench or soften the wording.")
    p.note("Hardcoded AP password \"read1234\" (config.h). This is the device's own "
           "SoftAP key (publicly documented in the README/manual), not a home-network "
           "secret, so it is acceptable to ship. The brief forbids changing the AP auth "
           "model or this password without sign-off - no change made.")

    # ---- 6. Test opportunities ------------------------------------------
    p = Page(pages, T)
    p.heading("6. Test opportunities (native pio test)")
    p.para("Pure logic worth covering; enumerated as cases only. Measurement is "
           "injected through a stub; storage/JSON is abstracted behind a seam.")
    p.subheading("Word-wrap (layoutPage core / toAscii)")
    p.bullet("Composed-line measurement wraps where summing word+space would under-count.")
    p.bullet("A line fitting exactly at maxW does not wrap; one unit over does.")
    p.bullet("\\n forces a break; \\r is ignored; runs of spaces collapse to one advance.")
    p.bullet("Over-wide single token (N2) asserts the documented (unbroken) behavior.")
    p.bullet("toAscii maps smart quotes / em- & en-dash / ellipsis to ASCII, drops other "
             "multibyte, and leaves ASCII byte offsets intact.")
    p.subheading("Page-offset math")
    p.bullet("nextPage/prevPage index <-> offset round-trip.")
    p.bullet("prevPage at index 0 and nextPage at EOF are no-ops.")
    p.bullet("open() resume-rebuild reproduces the same boundaries as sequential paging "
             "(guards the \"stuck page on resume\" bug).")
    p.bullet("endOffset clamps to _fileSize with no double-counted deferred word.")
    p.subheading("Bookmark round-trip")
    p.bullet("setLastPosition -> getLastPosition (unknown book -> 0).")
    p.bullet("addBookmark -> listBookmarks matches offset/label; removeBookmark respects bounds.")
    p.bullet("setLastOpenedBook -> getLastOpenedBook; empty/corrupt JSON loads defaults "
             "without crashing.")
    p.subheading("Validation & helpers")
    p.bullet("isBookPath accepts /books/a.txt and /books/A.TXT; rejects /bookmarks.json, "
             "/books/../secret, /books/sub/a.txt, /books/a.bin, empty name.")
    p.bullet("readBatteryPercent (post-fix) maps <3300 mV->0, >4200 mV->100, midpoint~=50.")
    p.bullet("humanSize boundaries at 1 KiB and 1 MiB.")

    # Footer on every content page
    for idx, im in enumerate(pages[1:], start=2):
        dd = ImageDraw.Draw(im)
        dd.line((MARGIN, PAGE_H - 66, PAGE_W - MARGIN, PAGE_H - 66),
                fill=RULE, width=1)
        dd.text((MARGIN, PAGE_H - 52), "ESP32-S3 E-Ink E-Reader - Code Review",
                font=F_SMALL, fill=SUBTLE)
        pn = "Page %d" % idx
        w = dd.textlength(pn, font=F_SMALL)
        dd.text((PAGE_W - MARGIN - w, PAGE_H - 52), pn, font=F_SMALL, fill=SUBTLE)

    os.makedirs(os.path.dirname(OUT_PDF), exist_ok=True)
    pages[0].save(OUT_PDF, "PDF", resolution=DPI,
                  save_all=True, append_images=pages[1:])
    print("Wrote", OUT_PDF, "(%d pages)" % len(pages))
    return pages


if __name__ == "__main__":
    build()
