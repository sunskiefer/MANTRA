#!/usr/bin/env python3
"""MANTRA knob filmstrips from the user's knob_render.py (tools/art/knob_render.py: their lighting and geometry, only the
backdrop is transparent now). One strip per colour: FR frames stacked down, frame k = angle -135 + 270*k/(FR-1) degrees,
each frame TILE x TILE px. Usage: make_knobs.py [colour ...]  (default: all six).  Output: tools/art/raw/knob_<colour>.png (the knob alone); tools/art/knob_base.py then sets each one on its lit bezel -> art/knobs/knob_<colour>.png"""
import os, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
import knob_render as kr
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FR, TILE = 128, 200

def frame(a):
    name, k = a
    return kr.render(kr.lin(kr.COL[name]), -135.0 + 270.0 * k / (FR - 1)).resize((TILE, TILE), Image.LANCZOS)

if __name__ == "__main__":
    names = [n.upper() for n in sys.argv[1:]] or list(kr.COL)
    os.makedirs(os.path.join(ROOT, "tools", "art", "raw"), exist_ok=True)
    with Pool(2) as pool:
        for n in names:
            frames = pool.map(frame, [(n, k) for k in range(FR)])
            strip = Image.new("RGBA", (TILE, TILE * FR))
            for k, f in enumerate(frames):
                strip.paste(f, (0, k * TILE))
            out = os.path.join(ROOT, "tools", "art", "raw", "knob_%s.png" % n.lower())
            strip.save(out, optimize=True)
            print(out, os.path.getsize(out) // 1024, "KB", flush=True)
