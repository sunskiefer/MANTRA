#!/usr/bin/env python3
"""Page preview with the live text: takes the framework's page composites (studio.py preview: panel, knobs, faders, keys, tiles) and writes on
top what the host draws live: knob / fader names and values, tile captions, page titles, popups, preset lists, at the real sizes
(Meta4MPCXXL, label_scale 1.7: name 35.7 px, value 37.4 px, tiles 26 px bold).  The font here is Titillium (the MPC font is not on this machine) and the
values are the parameters' defaults (a few are filled in as examples), so the picture is the layout and the sizes, not a screenshot.

  python3 tools/preview_pages.py <skin dir "Plugin Skins"> <out dir>
"""
import json, os, re, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import model as M, spec as S

FONT_DIR = os.path.join(ROOT, "third_party", "mpc-vst-plugins", "tools", "html_art", "fonts")
def font(sz, bold=False):
    return ImageFont.truetype(os.path.join(FONT_DIR, "TitilliumWeb-%s.ttf" % ("Bold" if bold else "Regular")), int(round(sz)))

def parse_conf(path):
    items = []
    for ln in open(path, encoding="utf-8"):
        ln = ln.strip()
        if not ln or ln.startswith("#") or ln.startswith("[") or "=" not in ln:
            continue
        kind, _, rest = ln.partition(" ")
        d = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', rest))
        d = {k: v.strip('"') for k, v in d.items()}
        d["kind"] = kind
        items.append(d)
    return items

def fmt_value(e):
    p = e.p; v = p.df
    if p.labels: return p.labels[int(round(v))] if int(round(v)) < len(p.labels) else str(v)
    if p.suffix == "Hz":
        return "%.2f kHz" % (v / 1000) if v >= 1000 else "%d Hz" % round(v)
    if p.suffix == "dB": return "%.1f dB" % v
    if p.suffix == "ms": return "%.1f ms" % v
    if p.step == 1: return "%d" % round(v)
    if p.lo == -1 and p.hi == 1: return "%+d %%" % round(v * 100)
    return "%d %%" % round(100 * (v - p.lo) / (p.hi - p.lo)) if (p.hi - p.lo) else str(v)

def find_rects(im, col=(70, 110, 160)):
    """the outlined 'live areas' the framework draws (value boxes, popups, buttons): [(x, y, w, h)]"""
    px = im.load(); W, H = im.size; out = []
    for y in range(H):
        for x in range(W):
            if px[x, y] != col: continue
            if x > 0 and px[x - 1, y] == col: continue
            if y > 0 and px[x, y - 1] == col: continue
            w = 1
            while x + w < W and px[x + w, y] == col: w += 1
            h = 1
            while y + h < H and px[x, y + h] == col: h += 1
            if w > 20 and h > 20 and px[x + w - 1, y + h - 1] == col: out.append((x, y, w, h))
    return out

def composites(skin_dir, dst, outlines=False):
    """the framework's page composites, without the blue 'live area' outlines it draws for value boxes"""
    sys.path.insert(0, os.path.join(ROOT, "third_party", "mpc-vst-plugins", "tools"))
    import studio
    from PIL import ImageDraw as ID
    orig = ID.ImageDraw.rectangle
    def rect(self, xy, fill=None, outline=None, width=1):
        if outline == (70, 110, 160): return None
        return orig(self, xy, fill=fill, outline=outline, width=width)
    if not outlines: ID.ImageDraw.rectangle = rect
    try:
        studio.preview(skin_dir, os.path.join(dst, "p_%d.png"))
    finally:
        ID.ImageDraw.rectangle = orig

def main(src, out, fonts=None):
    os.makedirs(out, exist_ok=True)
    conf = parse_conf(os.path.join(ROOT, "layout.conf"))
    pj = json.load(open(os.path.join(ROOT, "params.json")))["params"]
    P = {p["key"]: p for p in pj}
    ids = open(os.path.join(ROOT, "src", "param_ids.h"), encoding="utf-8").read()
    mt = re.search(r"CAP_TEXT\[[^\]]*\] = \{(.*?)\};", ids, re.S)
    caps = re.findall(r'"((?:[^"\\]|\\.)*)"', mt.group(1)) if mt else []
    capkeys = [p["key"] for p in pj if p["key"].startswith("cap_")]
    FULL = dict(zip(capkeys, caps)) if len(caps) == len(capkeys) else {}
    def capname(k): return FULL.get(k, P.get(k, {}).get("name", ""))
    E = {M.key_of(e): e for e in S.ALL}
    import presets as PR
    names_by_style = {}
    ps = json.load(open(os.path.join(ROOT, "presets.json")))["presets"]
    sty_of = {"Basic": 0, "Gates": 1, "Filter Sequences": 2, "Glitch": 3, "Movement": 4, "Delays": 5, "Lo-Fi": 6, "Space": 7, "Layers": 8, "Destruction": 9}
    allp = PR.all_presets()
    for pr, pl in zip(allp, ps):
        names_by_style.setdefault(sty_of[pr.cat], []).append(pl["name"])
    for vi, (pid, title, _t) in enumerate(M.PAGES):
        f = os.path.join(src, "p_0_mode%d-1.png" % (737 + vi))
        if not os.path.exists(f): continue
        im = Image.open(f).convert("RGB"); d = ImageDraw.Draw(im)
        W, H = im.size
        def show(it):
            w = it.get("when")
            return (not w) or ("pg_%s" % pid) in [x.split(":")[0] for x in w.split(",")]
        its = [i for i in conf if show(i)]
        boxes = []
        # erase the host's blue "live area" outlines: the real boxes are drawn below
        def txt(cx, cy, s, sz, col=(242, 242, 242), bold=False, anchor="mm", x=None):
            d.text((x if x is not None else cx, cy), s, font=font(sz, bold), fill=col, anchor=anchor)
        def hexcol(h, dflt=(20, 20, 20)):
            try: return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
            except Exception: return dflt
        for it in its:
            k = it["kind"]; key = it.get("key", "")
            cx = float(it.get("cx", 0)); cy = float(it.get("cy", 0)) - 86
            if k in ("knob", "slider_h", "slider_v") and key in E and not re.match(r"m\d_s\d\d", key):
                e = E[key]; r = float(it.get("r", 0))
                if k == "slider_v": continue
                # the box the skin generator makes (shadow_skin.py: knob / slider_h value label)
                if k == "knob":
                    rr = int(float(it["r"])); sz = 2 * rr + 10
                    cw = max(221, int(it["bw"]) if it.get("bw") else 0, sz)
                    name_h = (max(48, round(float(it.get("ns", 35.7)) * 1.2)) if it.get("ns") != "0" else 0)
                    value_y = sz // 2 + rr + 2 + name_h + (2 if name_h else 0)
                    vh = max(44, round(float(it.get("vs", 37.4)) * 1.2)); top = cy - sz / 2
                else:
                    sw_, sh_ = float(it["w"]), float(it["h"])
                    cw = max(sw_, float(it["bw"])) if it.get("bw") else max(130, sw_)
                    ns = float(it.get("ns", 17)); name_h = 0 if ns == 0 else max(20, round(ns * 1.2))
                    value_y = sh_ + 2 + name_h + (2 if name_h else 0)
                    vh = max(26, round(float(it.get("vs", 22)) * 1.2)); top = cy - sh_ / 2
                x, y, w, h = cx - cw / 2, top + value_y, cw, vh
                d.rectangle((x, y, x + w - 1, y + h - 1), fill=(17, 17, 17), outline=(58, 58, 58))
                txt(x + w / 2, y + h / 2, fmt_value(e), float(it.get("vs", 37.4)))
            elif k == "button":
                cap = capname(it.get("get", ""))
                col = hexcol(it.get("tcolor", "141414"))
                txt(cx, cy, cap, float(it.get("tsize", 26)), col, True)
            elif k == "readout":
                if key == "page_title": s = title
                elif key == "pr_info": s = "1 / %d" % -(-len(names_by_style.get(1, [])) // 16)
                else: s = capname(key)
                ts = float(it.get("tsize", 26)); al = it.get("talign", "center")
                col = hexcol(it.get("tcolor", "f2f2f2"), (242, 242, 242))
                w = float(it.get("w", 200))
                if al == "left": txt(cx, cy, s, ts, col, True, "lm", x=cx - w / 2 + 10)
                else: txt(cx, cy, s, ts, col, True)
            elif k == "popup":
                w, h = float(it.get("w", 210)), float(it.get("h", 52))
                opts = P.get(key, {}).get("options", [""]); s = opts[min(len(opts) - 1, int(P.get(key, {}).get("default", 0)))]
                txt(cx - 12, cy, s, 28, (242, 242, 242), False)
            elif k == "list":
                x0, y0 = float(it["x"]), float(it["y"]) - 86; w, h = float(it["w"]), float(it["h"])
                cols, rows = int(it["cols"]), int(it["rows"]); th = float(it["th"]); gap = float(it["gap"])
                cw = (w - gap * (cols - 1)) / cols
                if key == "pr_sty": lst = M.PR_STYLES
                else: lst = names_by_style.get(1, [])[:16]
                n = 0
                for c in range(cols):
                    for r_ in range(rows):
                        if n >= len(lst): break
                        yy = y0 + r_ * (th + gap) + th / 2; xx = x0 + c * (cw + gap)
                        txt(0, yy, lst[n], 26, (20, 20, 20), True, "lm", x=xx + 16); n += 1
                        if (key == "pr_sty" and n == 2) or (key != "pr_sty" and n == 3):
                            pass
        # the two header tiles etc. are buttons (above). Knob / key names are drawn by the composite already.
        im.save(os.path.join(out, "%02d_%s.png" % (vi, pid)))
    print("pages ->", out)

if __name__ == "__main__":
    # python3 tools/preview_pages.py <skin dir ("Plugin Skins")> <out dir>
    tmp = os.path.join(sys.argv[2], "_composites"); os.makedirs(tmp, exist_ok=True)
    composites(sys.argv[1], tmp)
    main(tmp, sys.argv[2])
