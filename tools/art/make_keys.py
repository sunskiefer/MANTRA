#!/usr/bin/env python3
"""Cut the 6 colours x (LED off, LED lit) key pictures out of the user's sheet (keys_source.png) with a
rounded-rectangle alpha (their art is untouched inside it; the sheet's drop shadow is left out).
Writes art/keys/key_<colour>_<off|on>.png (all the same size)."""
import os, sys
import numpy as np
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'art', 'keys')
os.makedirs(OUT, exist_ok=True)
im = Image.open(os.path.join(HERE, 'keys_source.png')).convert('RGB')
a = np.asarray(im).astype(int)
bg = np.array([35, 34, 30])
m = np.abs(a - bg).sum(2) > 28
COLS = ['red', 'green', 'blue', 'brown', 'black', 'grey']
XR = [(25, 236), (288, 496), (549, 757), (810, 1018), (1072, 1280), (1333, 1541)]
YR = [(25, 318), (355, 648)]
R = 13           # corner radius of their keys (px, source scale)
SS = 4
W = H = None
boxes = {}
for r, (y0, y1) in enumerate(YR):
    for c, (x0, x1) in enumerate(XR):
        sub = m[y0:y1, x0:x1]
        cols = np.where(sub.mean(0) > 0.3)[0]
        rows = np.where(sub.mean(1) > 0.3)[0]
        boxes[(r, c)] = (x0 + cols[0], y0 + rows[0], x0 + cols[-1] + 1, y0 + rows[-1] + 1)
W = int(np.median([b[2] - b[0] for b in boxes.values()])) - 3
H = int(np.median([b[3] - b[1] for b in boxes.values()])) - 10
print('key size', W, H)
mask = Image.new('L', (W * SS, H * SS), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, W * SS - 1, H * SS - 1), radius=R * SS, fill=255)
mask = mask.resize((W, H), Image.LANCZOS)
for (r, c), (x0, y0, x1, y1) in boxes.items():
    crop = im.crop((x0, y0, x0 + W, y0 + H)).convert('RGBA')   # anchored top-left: the sheet's shadow falls bottom-right
    crop.putalpha(mask)
    crop.save(os.path.join(OUT, 'key_%s_%s.png' % (COLS[c], 'off' if r == 0 else 'on')))
print('ok')
