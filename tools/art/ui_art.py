#!/usr/bin/env python3
"""MANTRA 0.2.0 panel art: flat dark page, fader caps / slots, wide key tiles.   python3 tools/art/ui_art.py  (writes the fixed art)

Used by gen_force.py (slot(), tile()) for the images whose size depends on the layout."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.abspath(os.path.join(HERE, "..", "..", "art"))
COLOURS = ["red", "green", "blue", "brown", "black", "grey"]
HDR_H = 70
W, H = 1280, 628


# --------------------------------------------------------------------------------------------- the page
def panel():
    """The flat dark page of the panel editor (#141414) with its thin line under the header; no texture, no band."""
    im = Image.new("RGB", (W, H), (20, 20, 20))
    d = ImageDraw.Draw(im)
    d.line((0, HDR_H, W, HDR_H), fill=(44, 44, 44), width=1)
    # marker pixels for src/step_touch.cc: 2 x 2 at the top-left and bottom-right corners of the page (its colours: MARK_TL / MARK_BR there)
    TL = [(19, 11, 83), (83, 11, 19), (11, 83, 19), (19, 83, 11)]
    BR = [(83, 19, 11), (11, 19, 83), (57, 11, 57), (11, 57, 57)]
    for k in range(4):
        im.putpixel((k & 1, k >> 1), TL[k])
        im.putpixel((W - 2 + (k & 1), H - 2 + (k >> 1)), BR[k])
    return im


# --------------------------------------------------------------------------------------------------- caps
def caps():
    """Cap pictures from CSXXL's cap art: vertical (art/caps/cap_<c>.png) and turned for horizontal faders (cap_<c>_h.png)."""
    src = {"red": "cap_red", "green": "cap_green", "blue": "cap_blue", "black": "cap_black", "grey": "cap_white"}
    out = {}
    for c, f in src.items():
        im = Image.open(os.path.join(HERE, f + ".png")).convert("RGBA")
        out[c] = im
    # brown: the white cap's light line tinted brown
    w = np.asarray(out["grey"]).astype(np.float32)
    lum = w[:, :, :3].mean(2)
    t = np.clip((lum - 150.0) / 90.0, 0, 1)[:, :, None]
    tint = np.array([214, 142, 88], np.float32) / 255.0
    rgb = w[:, :, :3] * (1 - t) + (w[:, :, :3] * tint) * t
    br = np.dstack([rgb, w[:, :, 3]]).clip(0, 255).astype(np.uint8)
    out["brown"] = Image.fromarray(br, "RGBA")
    for c, im in out.items():
        im = im.crop(im.getbbox())
        im.save(os.path.join(ART, "caps", "cap_%s.png" % c))
        im.rotate(90, expand=True).save(os.path.join(ART, "caps", "cap_%s_h.png" % c))


# --------------------------------------------------------------------------------------------------- slot
def slot(w, h, vert, path):
    """A fader's slot (the track image, stretched over the whole slider box): a black groove with a light lip and tick marks."""
    S = 3
    im = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    g = 12 * S   # groove thickness
    if vert:
        cx = w * S // 2
        box = (cx - g // 2, 8 * S, cx + g // 2, h * S - 8 * S)
    else:
        cy = h * S // 2
        box = (8 * S, cy - g // 2, w * S - 8 * S, cy + g // 2)
    lip = 2 * S
    off = S if True else 0
    # a bevel lit from the top: the lower lip catches the light, the upper lip is in shadow (dark panel)
    def rr(dx, dy, grow, fill):
        d.rounded_rectangle((box[0] - grow + dx, box[1] - grow + dy, box[2] + grow + dx, box[3] + grow + dy), radius=g // 2 + grow, fill=fill)
    rr(0, off, lip, (118, 119, 126, 255))       # light lower lip
    rr(0, -off // 2, lip, (4, 4, 5, 255))       # dark upper lip
    rr(0, 0, lip // 2, (44, 44, 48, 255))
    d.rounded_rectangle(box, radius=g // 2, fill=(6, 6, 7, 255))
    # inner shadow on the top / left
    inner = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(inner).rounded_rectangle((box[0] + S, box[1] + S, box[2] - S, box[3] - S), radius=g // 2 - S, outline=(30, 30, 33, 255), width=S)
    im.alpha_composite(inner)
    # ticks: 11, alongside the groove
    for i in range(11):
        t = i / 10.0
        big = i in (0, 5, 10)
        L = (9 if big else 5) * S
        if vert:
            y = int(box[1] + g // 2 + t * (box[3] - box[1] - g))
            for sx in (-1, 1):
                x0 = cx + sx * (g // 2 + 7 * S)
                d.line((x0, y, x0 + sx * L, y), fill=(112, 113, 120, 255), width=S)
        else:
            x = int(box[0] + g // 2 + t * (box[2] - box[0] - g))
            for sy in (-1, 1):
                y0 = cy + sy * (g // 2 + 7 * S)
                d.line((x, y0, x, y0 + sy * L), fill=(112, 113, 120, 255), width=S)
    im.resize((w, h), Image.LANCZOS).save(path)


# --------------------------------------------------------------------------------------------------- tiles
def _keys(colour, lit):
    return Image.open(os.path.join(ART, "keys", "key_%s_%s.png" % (colour, "on" if lit else "off"))).convert("RGBA")


FLAT = {"red": (214, 69, 69), "green": (59, 179, 105), "blue": (59, 130, 214), "brown": (138, 90, 58), "black": (42, 42, 42), "grey": (180, 182, 186)}


def tile(colour, w, h, lit, path):
    """A wide tile exactly like the panel editor's: one flat colour, rounded corners (7 px), a small soft shadow under it. Lit = brighter.
    (The six key pictures stay for the switches; tiles, header buttons and the preset lists are flat.)"""
    S = 4
    W_, H_ = w * S, h * S
    c = np.array(FLAT.get(colour, FLAT["grey"]), np.float32)
    if lit:
        c = np.minimum(255.0, c * 1.18 + 22.0)
    m, bot, top = 2 * S, 4 * S, 1 * S
    rad = 7 * S
    face = (m, top, W_ - m, H_ - bot)
    sh = Image.new("L", (W_, H_), 0)
    ImageDraw.Draw(sh).rounded_rectangle((face[0], face[1] + 2 * S, face[2], face[3] + 2 * S), radius=rad, fill=150)
    sh = sh.filter(ImageFilter.GaussianBlur(1.6 * S))
    out = Image.new("RGBA", (W_, H_), (0, 0, 0, 0))
    out.putalpha(sh)
    layer = Image.new("RGBA", (W_, H_), tuple(int(v) for v in c) + (255,))
    mask = Image.new("L", (W_, H_), 0)
    ImageDraw.Draw(mask).rounded_rectangle(face, radius=rad, fill=255)
    layer.putalpha(mask)
    out.alpha_composite(layer)
    out.resize((w, h), Image.LANCZOS).save(path)


def _band_h(k, ys, x0, x1, mw, bh):
    """A top/bottom band: the key's own strip, tiled across (grain stays grain, the LED glow only where it is in the strip's middle)."""
    strip = k.crop((x0, ys[0], x1, ys[1]))
    sw = strip.width
    out = Image.new("RGBA", (mw, strip.height))
    x = 0
    flip = False
    while x < mw:
        s = strip.transpose(Image.FLIP_LEFT_RIGHT) if flip else strip
        out.alpha_composite(s, (x, 0))
        x += sw
        flip = not flip
    return out.resize((mw, bh), Image.LANCZOS)


if __name__ == "__main__":
    os.makedirs(os.path.join(ART, "caps"), exist_ok=True)
    panel().save(os.path.join(ART, "panel.png"))
    caps()
    print("panel, caps ok")
