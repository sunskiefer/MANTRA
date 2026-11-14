#!/usr/bin/env python3
"""art/tile.png (270 x 110): the plugin's tile in MPC's plugin list. Original art: a dark panel, an amber step ladder, the name."""
import os
from PIL import Image, ImageDraw, ImageFont
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
W, H = 270, 110
im = Image.new("RGB", (W, H), (20, 20, 20))
d = ImageDraw.Draw(im)
amber = (240, 168, 40)
heights = [34, 14, 26, 8, 40, 20, 30, 12, 36, 18, 28, 10]
x0, base = 14, 92
for i, h in enumerate(heights):
    x = x0 + i * 21
    d.rectangle([x, base - h, x + 14, base], fill=amber if i % 3 == 0 else (92, 70, 24))
f = ImageFont.truetype(os.path.join(ROOT, "art", "fonts", "TitilliumWeb-SemiBold.ttf"), 30)
d.text((14, 8), "MANTRA", font=f, fill=(242, 242, 242))
f2 = ImageFont.truetype(os.path.join(ROOT, "art", "fonts", "TitilliumWeb-SemiBold.ttf"), 13)
d.text((190, 22), "AUDIO MANGLER", font=f2, fill=amber, anchor="ra" if False else None)
im.save(os.path.join(ROOT, "art", "tile.png"))
print("art/tile.png")
