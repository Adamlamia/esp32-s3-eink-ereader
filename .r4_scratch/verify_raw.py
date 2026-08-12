"""Scratch verifier: decode tools/refs/example.raw and confirm it matches the
quantized source canvas exactly (proves the packer is reversible + correct)."""
import os
import sys
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import make_refs as mr

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "tools", "refs", "example.raw")
SRC = os.path.join(ROOT, "tools", "refs", "example.png")

data = open(RAW, "rb").read()
assert len(data) == mr.RAW_SIZE, "bad size %d" % len(data)

# Decode: high nibble = left pixel (even x), low nibble = right pixel (odd x).
dec = bytearray(mr.EPD_WIDTH * mr.EPD_HEIGHT)
o = 0
for y in range(mr.EPD_HEIGHT):
    base = y * mr.EPD_WIDTH
    for xb in range(mr.BYTES_PER_ROW):
        b = data[y * mr.BYTES_PER_ROW + xb]
        dec[base + xb * 2] = (b >> 4) * 17       # level 0..15 -> ~0..255
        dec[base + xb * 2 + 1] = (b & 0x0F) * 17
        o += 1
decoded = Image.frombytes("L", (mr.EPD_WIDTH, mr.EPD_HEIGHT), bytes(dec))

# Reference: what the packer SHOULD have produced from the source.
with Image.open(SRC) as img:
    canvas = mr.fit_to_screen(img)
ref_levels = bytes(p >> 4 for p in canvas.tobytes())
dec_levels = bytes(p // 17 for p in dec)   # back to 0..15
# (decoded used *17 so //17 recovers the exact level)
assert ref_levels == dec_levels, "round-trip mismatch!"

# Spot checks: corners white, some dark content exists in the middle.
px = decoded.load()
assert px[2, 2] == 255, "top-left not white: %d" % px[2, 2]
assert px[mr.EPD_WIDTH - 3, mr.EPD_HEIGHT - 3] == 255, "bottom-right not white"
dark = sum(1 for p in dec if p < 64)
print("OK: round-trip exact; corners white; dark pixels =", dark)
print("decoded size:", decoded.size, decoded.mode)
