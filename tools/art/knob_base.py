#!/usr/bin/env python3
"""Sets each knob frame (tools/art/raw/knob_<colour>.png, the user's render) on a lit bezel with a soft shadow on the panel, so the
knob sits IN the panel instead of floating on it. The knob itself is not touched; the bezel and shadow do not turn.
  python3 tools/art/knob_base.py [colour ...]   -> art/knobs/knob_<colour>.png   (default: all six)
  python3 tools/art/knob_base.py --preview      -> /tmp/knob_base_preview.png (three frames of each colour on the panel colour)
The light comes from the top (as in knob_render.py): the bezel is lighter at the top, its upper rim catches the light, the lower rim
and the foot of the knob are in shadow."""
import os, sys
import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
T = 200                      # frame size (px), as make_knobs.py
SS = 4                       # supersampling of the bezel
CX, CY, R = 100.0, 98.5, 93.0  # bezel centre / radius in frame px
COLOURS = ["red", "green", "blue", "brown", "black", "grey"]


def base():
    n = T * SS
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    X = (xx + 0.5) / SS - CX
    Y = (yy + 0.5) / SS - CY
    r = np.hypot(X, Y)
    cov = np.clip((R - r) * SS + 0.5, 0, 1)                    # bezel coverage (anti-aliased edge)
    t = np.clip(Y / R, -1, 1) * 0.5 + 0.5                      # 0 at the top, 1 at the bottom
    top, bot = np.array([84, 84, 90], np.float32), np.array([22, 22, 24], np.float32)
    col = top[None, None, :] * (1 - t[..., None]) + bot[None, None, :] * t[..., None]
    # the top rim catches the light, the bottom rim is dark (a bevel 3 px wide)
    ny = -Y / np.maximum(r, 1e-3)                              # +1 at the top
    band = np.clip((r - (R - 3.0)) / 3.0, 0, 1)                # 0 inside, 1 at the edge
    light = np.clip(ny, 0, 1) ** 1.3 * band
    dark = np.clip(-ny, 0, 1) ** 1.3 * band
    col = col + light[..., None] * np.array([120, 120, 128], np.float32) - dark[..., None] * 18.0
    # a recess ring where the knob stands: dark right around the skirt (the skirt reaches 85 px), fading out
    ao = np.exp(-(((r - 88.0) / 4.0) ** 2)) * (r > 85.0)
    col = col * (1 - 0.55 * ao[..., None])
    rgba = np.dstack([np.clip(col, 0, 255), cov * 255]).astype(np.uint8)
    bez = Image.fromarray(rgba, "RGBA").resize((T, T), Image.LANCZOS)
    # the shadow the bezel throws on the panel: a little down, soft
    sh = np.zeros((n, n), np.float32)
    sh[np.hypot(X, Y - 3.0) < R] = 1.0
    sh = Image.fromarray((sh * 255).astype(np.uint8), "L").resize((T, T), Image.LANCZOS).filter(ImageFilter.GaussianBlur(2.6))
    sh = np.array(sh, np.float32) / 255.0 * 0.80
    shadow = Image.fromarray(np.dstack([np.zeros((T, T, 3), np.uint8), (sh * 255).astype(np.uint8)]), "RGBA")
    out = Image.new("RGBA", (T, T), (0, 0, 0, 0))
    out.alpha_composite(shadow)
    out.alpha_composite(bez)
    return out


def build(name, bz):
    src = Image.open(os.path.join(HERE, "raw", "knob_%s.png" % name))
    nf = src.size[1] // T
    out = Image.new("RGBA", src.size, (0, 0, 0, 0))
    for k in range(nf):
        fr = Image.new("RGBA", (T, T), (0, 0, 0, 0))
        fr.alpha_composite(bz)
        fr.alpha_composite(src.crop((0, k * T, T, (k + 1) * T)))
        out.paste(fr, (0, k * T))
    return out


if __name__ == "__main__":
    args = sys.argv[1:]
    bz = base()
    if args[:1] == ["--preview"]:
        sheet = Image.new("RGBA", (T * 3 + 40, T * 6 + 70), (20, 20, 20, 255))
        for i, c in enumerate(COLOURS):
            src = Image.open(os.path.join(HERE, "raw", "knob_%s.png" % c))
            for j, k in enumerate((0, 50, 110)):
                fr = Image.new("RGBA", (T, T), (0, 0, 0, 0))
                fr.alpha_composite(bz)
                fr.alpha_composite(src.crop((0, k * T, T, (k + 1) * T)))
                sheet.alpha_composite(fr, (10 + j * (T + 10), 10 + i * (T + 10)))
        sheet.save("/tmp/knob_base_preview.png")
        sys.exit(0)
    os.makedirs(os.path.join(ROOT, "art", "knobs"), exist_ok=True)
    for c in (args or COLOURS):
        out = build(c.lower(), bz)
        p = os.path.join(ROOT, "art", "knobs", "knob_%s.png" % c.lower())
        out.save(p, optimize=True)
        print(p, os.path.getsize(p) // 1024, "KB", flush=True)
