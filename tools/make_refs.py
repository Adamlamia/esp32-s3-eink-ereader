#!/usr/bin/env python3
"""Convert images into the ESP32-S3 E-Ink E-Reader reference-viewer format.

pip install Pillow

The Dev Companion "References" section shows full-screen pinouts/schematics
that live on the SD card under /refs/ as .raw files. Each .raw file is a raw
dump of the panel's 4-bpp grayscale framebuffer (960x540, 16 gray levels),
ready to be memcpy()'d straight onto the display -- no decoding on the device.

This script prepares those .raw files from any Pillow-readable image
(PNG, JPG, BMP, GIF, ...):

    python tools/make_refs.py <input_image> [output.raw]
    python tools/make_refs.py --batch <input_dir> [output_dir]

Pipeline (matches docs/PROJECT_BRIEF.md Feature 5):
  1. Open the image.
  2. Fit it within 960x540 preserving aspect ratio (no crop), pad with white.
  3. Convert to 8-bit grayscale ("L") and quantize to the panel's 16 levels:
         level = pixel // 16        # 0..15  (0 = black, 15 = white)
  4. Pack two pixels per byte, high nibble first:
         byte = (left << 4) | right
     Row-major, no row padding (960 is even -> 480 bytes/row).
  5. Write exactly 259200 bytes (480 * 540).

In --batch mode every image in the input directory is converted to a .raw file
with the same stem, and a refs_index.txt manifest is written next to the
outputs. The manifest is one line per image:

    <filename>.raw|<Human Readable Label>

The label is derived from the file stem, title-cased (underscores/hyphens
become spaces). Copy the whole output directory to /refs/ on the SD card.
"""
import argparse
import os
import sys

from PIL import Image

# --- Panel geometry (must match src/config.h: DISPLAY_WIDTH/HEIGHT) ---------
EPD_WIDTH = 960
EPD_HEIGHT = 540
BYTES_PER_ROW = EPD_WIDTH // 2          # 480 (two 4-bpp pixels per byte)
RAW_SIZE = BYTES_PER_ROW * EPD_HEIGHT   # 259200 bytes exactly

# Image extensions we treat as convertible input in --batch mode.
IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff", ".webp")

WHITE = 255   # grayscale pad colour (panel 0xF)


def label_from_stem(stem):
    """Derive a human-readable label from a filename stem.

    "esp32-s3_pinout" -> "Esp32 S3 Pinout" (title-cased, separators -> spaces).
    """
    cleaned = stem.replace("_", " ").replace("-", " ")
    # Collapse runs of whitespace then title-case. title() is good enough for
    # ASCII pinout names; it leaves digits alone and capitalises each word.
    return " ".join(cleaned.split()).title()


def fit_to_screen(img):
    """Return a 960x540 white canvas with `img` scaled to fit, centred.

    Aspect ratio is preserved and the image is never cropped. The image is
    scaled so its largest dimension fills the panel ("fit to screen"); the
    viewer itself offers no zoom, so filling the screen here is the intended
    behaviour. Padding is white (panel level 0xF).
    """
    canvas = Image.new("L", (EPD_WIDTH, EPD_HEIGHT), WHITE)

    # Work in grayscale from here on so the resize/pad stays single-channel.
    src = img.convert("L")
    w, h = src.size
    if w <= 0 or h <= 0:
        return canvas

    scale = min(EPD_WIDTH / float(w), EPD_HEIGHT / float(h))
    new_w = max(1, int(round(w * scale)))
    new_h = max(1, int(round(h * scale)))

    # High-quality resample when shrinking or growing.
    try:
        resample = Image.Resampling.LANCZOS
    except AttributeError:                 # Pillow < 9.1 fallback
        resample = Image.LANCZOS
    resized = src.resize((new_w, new_h), resample)

    # Centre on the white canvas (fit-to-screen, no crop).
    x = (EPD_WIDTH - new_w) // 2
    y = (EPD_HEIGHT - new_h) // 2
    canvas.paste(resized, (x, y))
    return canvas


def pack_framebuffer(gray):
    """Pack a 960x540 'L' image into the 4-bpp framebuffer byte layout.

    Quantizes each pixel to 16 levels (pixel // 16) and packs two pixels per
    byte, HIGH nibble = LEFT pixel (even x), LOW nibble = RIGHT pixel (odd x),
    row-major with no padding. Returns a bytes object of exactly RAW_SIZE.
    """
    assert gray.mode == "L", "expected an 8-bit grayscale image"
    assert gray.size == (EPD_WIDTH, EPD_HEIGHT), "expected a 960x540 canvas"

    px = gray.tobytes()                    # row-major, 1 byte/pixel
    if len(px) != EPD_WIDTH * EPD_HEIGHT:
        raise ValueError("unexpected pixel buffer size: %d" % len(px))

    out = bytearray(RAW_SIZE)
    o = 0
    for i in range(0, len(px), 2):
        left = px[i] >> 4                  # 0..255 -> 0..15
        right = px[i + 1] >> 4
        out[o] = (left << 4) | (right & 0x0F)
        o += 1
    return bytes(out)


def convert_image(in_path, out_path):
    """Convert one image to a 259200-byte .raw framebuffer dump."""
    with Image.open(in_path) as img:
        canvas = fit_to_screen(img)
        raw = pack_framebuffer(canvas)

    if len(raw) != RAW_SIZE:
        raise ValueError("internal error: packed %d bytes, expected %d"
                         % (len(raw), RAW_SIZE))

    with open(out_path, "wb") as f:
        f.write(raw)
    return len(raw)


def write_manifest(entries, out_dir):
    """Write refs_index.txt (one '<file>.raw|Label' line per entry)."""
    manifest = os.path.join(out_dir, "refs_index.txt")
    with open(manifest, "w", encoding="utf-8", newline="\n") as f:
        for raw_name, label in entries:
            f.write("%s|%s\n" % (raw_name, label))
    return manifest


def list_images(in_dir):
    """Return sorted image filenames in a directory (case-insensitive ext)."""
    names = []
    for name in os.listdir(in_dir):
        full = os.path.join(in_dir, name)
        if os.path.isfile(full) and name.lower().endswith(IMAGE_EXTS):
            names.append(name)
    return sorted(names)


def cmd_single(args):
    in_path = args.input
    if not os.path.isfile(in_path):
        sys.exit("error: input image not found: %s" % in_path)

    if args.output:
        out_path = args.output
    else:
        stem = os.path.splitext(os.path.basename(in_path))[0]
        out_path = stem + ".raw"

    # Make sure the destination directory exists (e.g. "tools/refs/x.raw").
    out_dir = os.path.dirname(os.path.abspath(out_path))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    n = convert_image(in_path, out_path)
    print("wrote %s (%d bytes)" % (out_path, n))

    # A single conversion also drops a one-line manifest beside the output so
    # the result is drop-in ready for /refs/.
    stem = os.path.splitext(os.path.basename(out_path))[0]
    label = label_from_stem(stem)
    manifest = write_manifest([(os.path.basename(out_path), label)], out_dir)
    print("wrote %s (%s)" % (manifest, label))


def cmd_batch(args):
    in_dir = args.input
    if not os.path.isdir(in_dir):
        sys.exit("error: input directory not found: %s" % in_dir)

    out_dir = args.output if args.output else in_dir
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    names = list_images(in_dir)
    if not names:
        sys.exit("error: no images found in %s (expected one of %s)"
                 % (in_dir, ", ".join(IMAGE_EXTS)))

    entries = []
    for name in names:
        stem = os.path.splitext(name)[0]
        raw_name = stem + ".raw"
        out_path = os.path.join(out_dir, raw_name)
        n = convert_image(os.path.join(in_dir, name), out_path)
        label = label_from_stem(stem)
        entries.append((raw_name, label))
        print("  %-28s -> %-24s (%d bytes)  [%s]" % (name, raw_name, n, label))

    manifest = write_manifest(entries, out_dir)
    print("converted %d image(s); manifest: %s" % (len(entries), manifest))


def build_parser():
    epilog = (
        "framebuffer format:\n"
        "  960x540, 4 bits per pixel, 16 gray levels (0=black, 15=white).\n"
        "  Two pixels are packed per byte: HIGH nibble = LEFT pixel (even x),\n"
        "  LOW nibble = RIGHT pixel (odd x). Rows are stored top-to-bottom with\n"
        "  NO padding (960 is even), so each row is 480 bytes and a full frame\n"
        "  is exactly 480 * 540 = 259200 bytes. Grayscale is quantized with\n"
        "  level = pixel // 16. The device memcpy()s the .raw file straight\n"
        "  into the display framebuffer (DisplayManager::blitRaw).\n"
        "\n"
        "examples:\n"
        "  python tools/make_refs.py pinout.png                 # -> pinout.raw\n"
        "  python tools/make_refs.py pinout.png refs/p.raw      # explicit out\n"
        "  python tools/make_refs.py --batch imgs/ tools/refs/  # many + manifest\n"
    )
    p = argparse.ArgumentParser(
        prog="make_refs.py",
        description="Convert images to the E-Reader reference-viewer .raw format.",
        epilog=epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--batch", action="store_true",
                   help="treat INPUT as a directory of images and convert all of them")
    p.add_argument("input", help="input image file (or directory with --batch)")
    p.add_argument("output", nargs="?", default=None,
                   help="output .raw file (single) or output directory (--batch); "
                        "defaults to the input stem / input directory")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.batch:
        cmd_batch(args)
    else:
        cmd_single(args)


if __name__ == "__main__":
    main()
