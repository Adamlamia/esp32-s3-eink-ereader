"""Scratch generator: build a sample 960x540 'pinout' PNG for make_refs.py.

This is only used to validate tools/make_refs.py end-to-end (deliverable #8).
The generated PNG is a throwaway source; the committed artifact is the .raw.
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 960, 540
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tools", "refs", "example.png")


def font(size, bold=True):
    for name in ("arialbd.ttf" if bold else "arial.ttf", "arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(os.path.join(r"C:\Windows\Fonts", name), size)
        except Exception:
            pass
    return ImageFont.load_default()


img = Image.new("L", (W, H), 255)
d = ImageDraw.Draw(img)

# Border frame
d.rectangle([8, 8, W - 9, H - 9], outline=0, width=4)

# Title
d.text((W // 2, 48), "PINOUT EXAMPLE", fill=0, anchor="mm", font=font(54))
d.text((W // 2, 96), "ESP32-S3 E-Ink E-Reader  -  reference viewer test card",
       fill=60, anchor="mm", font=font(22, bold=False))

# A fake IC body in the centre with pins down both sides (grayscale bars).
bx, by, bw, bh = 360, 170, 240, 300
d.rectangle([bx, by, bx + bw, by + bh], outline=0, width=3)
d.text((bx + bw // 2, by + bh // 2), "ESP32-S3", fill=0, anchor="mm", font=font(28))

# Pins: left side (dark) and right side (mid gray) to exercise several levels.
for i in range(10):
    y = by + 18 + i * 28
    d.rectangle([bx - 46, y, bx - 6, y + 16], fill=0)          # left pins: black
    d.rectangle([bx + bw + 6, y, bx + bw + 46, y + 16], fill=128)  # right: gray
    d.text((bx - 52, y + 8), "GPIO%d" % i, fill=0, anchor="rm", font=font(15, False))
    d.text((bx + bw + 52, y + 8), "3V3" if i % 2 else "GND",
           fill=0, anchor="lm", font=font(15, False))

# Gray-level ramp across the bottom to show all 16 quantized steps.
ramp_y = H - 60
cell = (W - 40) // 16
for i in range(16):
    shade = 255 - i * 17   # 255 .. ~0
    d.rectangle([20 + i * cell, ramp_y, 20 + (i + 1) * cell, ramp_y + 28],
                fill=shade, outline=0)
d.text((W // 2, ramp_y - 12), "16-level grayscale ramp (white -> black)",
       fill=0, anchor="mm", font=font(16, False))

os.makedirs(os.path.dirname(OUT), exist_ok=True)
img.save(OUT)
print("wrote", OUT, img.size, img.mode)
