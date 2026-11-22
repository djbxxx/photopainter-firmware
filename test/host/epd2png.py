#!/usr/bin/env python3
"""Render a 192000-byte .epd frame (800x480, 4bpp panel indices) to PNG for inspection."""
import sys
from PIL import Image

PAL = [(0, 0, 0), (255, 255, 255), (0, 255, 0), (0, 0, 255), (255, 0, 0), (255, 255, 0), (255, 128, 0)]
src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
assert len(data) == 192000, f"{src}: {len(data)} bytes"
im = Image.new("RGB", (800, 480))
px = im.load()
for i, b in enumerate(data):
    y, x = divmod(i * 2, 800)
    for k, v in enumerate((b >> 4, b & 15)):
        px[x + k, y] = PAL[v] if v < 7 else (255, 0, 255)
im.save(dst)
