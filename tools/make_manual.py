"""Generate the ESP32-S3 E-Ink E-Reader user manual as a multi-page PDF.

Uses only Pillow (already available) so there are no extra dependencies.
Each page is rendered as a Letter-size raster at 150 DPI and the pages are
saved together into a single PDF.
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

COVER_IMG = r"C:\Users\ASUS TUF\.qoder\vibe_images\ereader_cover_1784808920.png"
OUT_PDF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "docs", "ESP32-S3-EInk-EReader-User-Manual.pdf")

FONT_DIR = r"C:\Windows\Fonts"


def font(name, size):
    try:
        return ImageFont.truetype(os.path.join(FONT_DIR, name), size)
    except Exception:
        return ImageFont.load_default()


F_TITLE = font("arialbd.ttf", 60)
F_SUB = font("arial.ttf", 30)
F_H = font("arialbd.ttf", 34)
F_SH = font("arialbd.ttf", 25)
F_BODY = font("arial.ttf", 23)
F_BODY_B = font("arialbd.ttf", 23)
F_SMALL = font("arial.ttf", 18)
F_MONO = font("consola.ttf", 22)


class Page:
    def __init__(self, pages, title=None):
        self.img = Image.new("RGB", (PAGE_W, PAGE_H), WHITE)
        self.d = ImageDraw.Draw(self.img)
        self.y = MARGIN
        self.pages = pages
        pages.append(self.img)
        if title:
            self.header(title)

    # -- helpers -----------------------------------------------------------
    def _wrap(self, text, fnt, max_w):
        words, lines, cur = text.split(), [], ""
        for w in words:
            trial = w if not cur else cur + " " + w
            if self.d.textlength(trial, font=fnt) <= max_w:
                cur = trial
            else:
                if cur:
                    lines.append(cur)
                cur = w
        if cur:
            lines.append(cur)
        return lines

    def space(self, px):
        self.y += px

    def header(self, title):
        self.d.text((MARGIN, self.y), title, font=F_SH, fill=SUBTLE)
        self.y += 38
        self.d.line((MARGIN, self.y, PAGE_W - MARGIN, self.y), fill=RULE, width=2)
        self.y += 26

    def heading(self, text):
        if self.y > PAGE_H - 220:
            return Page(self.pages).heading(text)
        self.d.text((MARGIN, self.y), text, font=F_H, fill=ACCENT)
        self.y += 52
        return self

    def subheading(self, text):
        self.space(6)
        self.d.text((MARGIN, self.y), text, font=F_SH, fill=INK)
        self.y += 38

    def para(self, text, fnt=F_BODY, color=INK, indent=0):
        max_w = PAGE_W - 2 * MARGIN - indent
        for line in self._wrap(text, fnt, max_w):
            self.d.text((MARGIN + indent, self.y), line, font=fnt, fill=color)
            self.y += fnt.size + 10
        self.y += 6

    def bullet(self, text):
        max_w = PAGE_W - 2 * MARGIN - 40
        lines = self._wrap(text, F_BODY, max_w)
        self.d.ellipse((MARGIN + 4, self.y + 9, MARGIN + 14, self.y + 19), fill=ACCENT)
        for i, line in enumerate(lines):
            self.d.text((MARGIN + 34, self.y), line, font=F_BODY, fill=INK)
            self.y += F_BODY.size + 10
        self.y += 4

    def step(self, n, text):
        max_w = PAGE_W - 2 * MARGIN - 56
        lines = self._wrap(text, F_BODY, max_w)
        self.d.ellipse((MARGIN, self.y, MARGIN + 30, self.y + 30), fill=ACCENT)
        self.d.text((MARGIN + 9, self.y + 3), str(n), font=F_SMALL, fill=WHITE)
        for line in lines:
            self.d.text((MARGIN + 50, self.y + 1), line, font=F_BODY, fill=INK)
            self.y += F_BODY.size + 10
        self.y += 6

    def code(self, text):
        w = self.d.textlength(text, font=F_MONO)
        self.d.rounded_rectangle((MARGIN, self.y, MARGIN + w + 28, self.y + 40),
                                 radius=8, fill=BAND)
        self.d.text((MARGIN + 14, self.y + 8), text, font=F_MONO, fill=(40, 60, 90))
        self.y += 54

    def gesture_row(self, gesture, action):
        row_h = 54
        self.d.line((MARGIN, self.y + row_h, PAGE_W - MARGIN, self.y + row_h),
                    fill=RULE, width=1)
        self.d.text((MARGIN + 8, self.y + 13), gesture, font=F_BODY_B, fill=ACCENT)
        self.d.text((MARGIN + 430, self.y + 13), action, font=F_BODY, fill=INK)
        self.y += row_h

    def note(self, text):
        max_w = PAGE_W - 2 * MARGIN - 40
        lines = self._wrap(text, F_SMALL, max_w)
        h = 24 + len(lines) * (F_SMALL.size + 8)
        self.d.rounded_rectangle((MARGIN, self.y, PAGE_W - MARGIN, self.y + h),
                                 radius=10, fill=(245, 247, 250))
        self.d.rectangle((MARGIN, self.y, MARGIN + 6, self.y + h), fill=ACCENT)
        yy = self.y + 12
        for line in lines:
            self.d.text((MARGIN + 24, yy), line, font=F_SMALL, fill=(70, 80, 95))
            yy += F_SMALL.size + 8
        self.y += h + 18


def build():
    pages = []

    # ---- Cover page ------------------------------------------------------
    cover = Image.new("RGB", (PAGE_W, PAGE_H), WHITE)
    d = ImageDraw.Draw(cover)
    d.rectangle((0, 0, PAGE_W, 300), fill=BAND)
    d.text((MARGIN, 120), "ESP32-S3 E-Ink E-Reader", font=F_TITLE, fill=INK)
    d.text((MARGIN, 200), "User Manual", font=F_SUB, fill=ACCENT)

    if os.path.exists(COVER_IMG):
        im = Image.open(COVER_IMG).convert("RGB")
        max_w = PAGE_W - 2 * MARGIN
        max_h = 760
        r = min(max_w / im.width, max_h / im.height)
        im = im.resize((int(im.width * r), int(im.height * r)))
        cover.paste(im, ((PAGE_W - im.width) // 2, 380))

    d.text((MARGIN, 1240),
           "A minimalist, self-hosted e-reader for plain-text books.",
           font=F_SUB, fill=INK)
    d.text((MARGIN, 1300),
           "Runs on a LILYGO T5 4.7\" E-Paper S3  -  Firmware v0.1.0",
           font=F_SMALL, fill=SUBTLE)
    d.line((MARGIN, 1360, PAGE_W - MARGIN, 1360), fill=RULE, width=2)
    d.text((MARGIN, 1380),
           "Upload books over Wi-Fi  -  Read with automatic bookmarks  -  "
           "Drive it all from one button",
           font=F_SMALL, fill=SUBTLE)
    pages.append(cover)

    # ---- 1. Overview / Getting started ----------------------------------
    p = Page(pages, "ESP32-S3 E-Ink E-Reader  -  User Manual")
    p.heading("1. Overview")
    p.para("This device is a small, self-hosted e-reader for plain-text (.txt) "
           "books. You upload books to it over its own Wi-Fi network from a "
           "phone or laptop, then read them on the crisp 4.7\" e-ink screen. "
           "It remembers where you stopped and lets you drop bookmarks - all "
           "from a single button.")
    p.subheading("What you need")
    p.bullet("The e-reader board (LILYGO T5 4.7\" E-Paper S3).")
    p.bullet("A USB-C cable to charge and flash the device.")
    p.bullet("Optional: a 1000 mAh Li-Po battery for cordless reading.")
    p.bullet("Optional: a microSD card formatted FAT32 for a larger library.")
    p.bullet("Any phone or laptop with Wi-Fi and a web browser.")

    p.heading("2. Getting Started")
    p.step(1, "Power the device using the USB-C cable or a charged battery.")
    p.step(2, "On boot it briefly shows \"Starting up...\", then opens your "
              "last book, or the Library screen if none is saved.")
    p.step(3, "Books are stored on the microSD card when one is inserted "
              "(FAT32); otherwise they are kept on the internal flash memory.")
    p.note("No microSD card required. Without a card the reader automatically "
           "falls back to internal storage, so everything still works - you "
           "just have less room for books.")

    # ---- 3. Adding books -------------------------------------------------
    p = Page(pages, "ESP32-S3 E-Ink E-Reader  -  User Manual")
    p.heading("3. Adding Books Over Wi-Fi")
    p.para("The reader hosts its own Wi-Fi network and a small web page you "
           "use to upload books. No home router or internet is needed.")
    p.step(1, "On your phone or laptop, open Wi-Fi settings and join the "
              "network named  EReader-Setup.")
    p.para("Password:", fnt=F_BODY_B)
    p.code("read1234")
    p.step(2, "Open a web browser and go to the portal address:")
    p.code("http://ereader.local     (or  http://192.168.4.1)")
    p.step(3, "Drag and drop your .txt files onto the page, or click to "
              "browse and select them.")
    p.step(4, "Uploaded books appear in the Library on the device. You can "
              "also delete books from the same page.")
    p.note("To save battery, the Wi-Fi portal automatically turns off about "
           "10 minutes after boot. You can switch it back on at any time with "
           "a quadruple tap of the BOOT button (see Navigation).")

    # ---- 4. Navigation ---------------------------------------------------
    p = Page(pages, "ESP32-S3 E-Ink E-Reader  -  User Manual")
    p.heading("4. Navigation - the single BOOT button")
    p.para("Everything is controlled with the one on-board BOOT button using "
           "simple tap gestures. Tap quickly; a short pause ends a sequence.")
    p.space(6)
    p.d.rectangle((MARGIN, p.y, PAGE_W - MARGIN, p.y + 44), fill=BAND)
    p.d.text((MARGIN + 8, p.y + 11), "GESTURE", font=F_SH, fill=SUBTLE)
    p.d.text((MARGIN + 430, p.y + 11), "WHAT IT DOES", font=F_SH, fill=SUBTLE)
    p.y += 44
    p.gesture_row("Single tap", "Next page")
    p.gesture_row("Double tap", "Previous page")
    p.gesture_row("Triple tap", "Open the Library screen")
    p.gesture_row("Quadruple tap", "Turn the Wi-Fi portal ON / OFF")
    p.gesture_row("Long press (~0.7s)", "Drop a bookmark at this spot")
    p.space(24)
    p.subheading("Optional dedicated buttons")
    p.para("If you wire external Previous / Next buttons to the free GPIO pins "
           "(see the hardware notes), they turn pages directly without needing "
           "the tap gestures.")

    # ---- 5. Features + rest ---------------------------------------------
    p = Page(pages, "ESP32-S3 E-Ink E-Reader  -  User Manual")
    p.heading("5. Features")
    p.bullet("Reads plain-text .txt books with automatic word-wrapped pages.")
    p.bullet("Resumes automatically at your last book and reading position.")
    p.bullet("Named bookmarks you can drop with a long press.")
    p.bullet("On-device Library listing of all uploaded books.")
    p.bullet("Built-in Wi-Fi upload portal - no app or cloud account needed.")
    p.bullet("Optional USB drive mode: mount the microSD over USB to copy "
             "books via cable (separate firmware variant).")
    p.bullet("E-ink friendly: periodic full refresh to clear ghosting, and "
             "light sleep to save power.")

    p.heading("6. Turning Wi-Fi On and Off")
    p.para("Quadruple-tap the BOOT button to toggle the upload portal. The "
           "screen confirms with \"Wi-Fi On\" (and the join details) or "
           "\"Wi-Fi Off\". Toggling by hand overrides the automatic power-"
           "saving shutoff, so it stays exactly as you set it.")

    p.heading("7. Power & Battery")
    p.bullet("After about 2 minutes idle the reader enters light sleep; press "
             "BOOT to wake it.")
    p.bullet("Reading works fully offline - Wi-Fi is only needed to add books.")

    p.heading("8. Troubleshooting")
    p.bullet("\"No books yet\": upload a .txt via the Wi-Fi portal.")
    p.bullet("microSD not detected: make sure it is FAT32 and fully seated; "
             "the reader still runs on internal storage without it.")
    p.bullet("Can't open ereader.local: use http://192.168.4.1 instead.")
    p.bullet("Portal not showing: quadruple-tap BOOT to switch Wi-Fi back on.")

    # Footer on every content page
    for idx, im in enumerate(pages[1:], start=2):
        dd = ImageDraw.Draw(im)
        dd.line((MARGIN, PAGE_H - 70, PAGE_W - MARGIN, PAGE_H - 70),
                fill=RULE, width=1)
        dd.text((MARGIN, PAGE_H - 56), "ESP32-S3 E-Ink E-Reader",
                font=F_SMALL, fill=SUBTLE)
        pn = "Page %d" % idx
        w = dd.textlength(pn, font=F_SMALL)
        dd.text((PAGE_W - MARGIN - w, PAGE_H - 56), pn, font=F_SMALL, fill=SUBTLE)

    os.makedirs(os.path.dirname(OUT_PDF), exist_ok=True)
    pages[0].save(OUT_PDF, "PDF", resolution=DPI,
                  save_all=True, append_images=pages[1:])
    print("Wrote", OUT_PDF, "(%d pages)" % len(pages))
    return pages


if __name__ == "__main__":
    pgs = build()
    if os.environ.get("PREVIEW"):
        for i, im in enumerate(pgs, 1):
            im.save(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "_preview_p%d.png" % i))
        print("previews written")
