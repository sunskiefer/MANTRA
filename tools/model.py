"""MANTRA (Force / MPC OS) -- the one model everything else is generated from.

  spec.py     the engine's parameter spec (359 parameters, shared with the DSP)
  model.py    this file: the Force parameter list (engine + steps + UI), the 37 pages, every control with a default place
  gen_force.py  writes params.json, src/param_ids.h, layout_default.json, layout.conf, presets.json, vst.json, docs

The parameter list is append-only once a version ships (MPC stores values by index): the first 359 keys are the engine's
own, in spec order; everything else is appended after them.

Page = 1280 x 628 (layout.conf adds 86 to y, the Force Shadow offset); the top 70 px are the header.
Text sizes follow Meta4MPCXXL: label_scale 1.7 (knob name 35.7 px in a 221 x 48 box, value 37.4 px).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spec  # noqa: E402

NMOD, NSTEP, NMATRIX = spec.NMOD, spec.NSTEP, spec.NMATRIX
N_ENG = len(spec.ALL)

# ---------------------------------------------------------------------------------------------------------- labels
SHORT_LABEL = {
    "Resonance": "RES", "Feedback": "FDBACK", "Playback Speed": "PB SPEED", "Sync Speed": "SYNC DIV", "Ping-Pong": "PING",
    "Trem Dest": "TREM TO", "Flip Dest": "FLIP TO", "Tremolo": "TREM", "Pan Flip": "FLIP", "Low Cut": "LOW CUT",
    "Hi Cut": "HI CUT", "Bypass": "BYPASS", "Reverse": "REVERSE", "Speed L": "SPEED L", "Speed R": "SPEED R",
    "Sync L": "DIV L", "Sync R": "DIV R", "Treble": "TREBLE", "Clipper": "CLIPPER", "Routing": "ROUTING",
    "Volume": "VOLUME", "Master": "MASTER", "Stretch": "STRETCH", "Smooth": "SMOOTH", "Shuffle": "SHUFFLE",
    "Tension": "TENSION", "Invert": "INVERT",
}


def label_of(e):
    u = e.p.ui
    return SHORT_LABEL.get(u, u.upper())


def key_of(e):
    return e.name.lower()


EFF_NAMES = ["FILTER", "DISTORTION", "DELAY", "LO-FI", "FLANGER", "GLITCH"]
EFF_TAG = ["FLT", "DST", "DLY", "LOF", "FLG", "GLT"]
EFF_PREFIX = ["Fi", "Di", "De", "Lo", "Fl", "Gl"]


def qname(e):
    """The parameter's name: MPC draws it under the knob, so it is the short on-screen label (never longer than 10
    characters, one 35.7 px Meta4 name box) with its place in front where it fits (A / B, M1..M8, X1..X24, EQ, VERB)."""
    p, u = e.p, label_of(e)
    if e.scope == "layer":
        r = e.name.split("_", 1)[1]
        L = spec.LAYER_NAMES[e.layer]
        for i, pre in enumerate(EFF_PREFIX):
            if r.startswith(pre) and not r.startswith("Slot"):
                t = "%s %s %s" % (L, EFF_TAG[i], u)
                return t if len(t) <= 10 else "%s %s" % (L, u)
        return "%s %s" % (L, u)
    if e.scope == "global":
        ov = {"G_EqOn": "EQ ON", "G_RvOn": "VERB ON", "G_RvTreble": "VERB TREB", "G_OutVol": "OUT VOL", "G_OutPan": "OUT PAN",
              "G_OutMix": "OUT MIX", "G_MasterOn": "MASTER ON", "G_Speed": "MOD SPEED"}
        if e.name in ov:
            return ov[e.name]
        g = p.group
        pre = {"EQ": "EQ ", "Reverb": "VERB "}.get(g, "")
        t = pre + u
        return t if len(t) <= 10 else u
    if e.scope == "mod":
        t = "M%d %s" % (e.mod_i + 1, u)
        return t if len(t) <= 10 else u
    if e.scope == "matrix":
        return "X%d %s" % (e.slot + 1, {"Src": "SRC", "Dst": "DEST", "Depth": "DEPTH", "Byp": "BYP"}[p.name])
    return u


DEST_SHORT = []
for _n in spec.DEST_NAMES:
    s = _n
    for a, b in (("Distortion", "Dist"), ("Filter", "Flt"), ("Delay", "Dly"), ("Flanger", "Flg"), ("Glitch", "Glt"),
                 ("Lo-Fi", "LoFi"), ("Resonance", "Res"), ("Feedback", "Fdbk"), ("Low Cut", "LoCut"), ("Hi Cut", "HiCut"),
                 ("Treble", "Treb"), ("Reverb", "Rev"), ("Output", "Out"), ("Volume", "Vol"), ("Stretch", "Strch"),
                 ("Smooth", "Smth"), ("Playback Speed", "PbSpd")):
        s = s.replace(a, b)
    DEST_SHORT.append(s)

# ---------------------------------------------------------------------------------------------------------- formats
F_NONE, F_PCT, F_PAN, F_HZ, F_DB, F_MS, F_NUM, F_INT, F_SPCT, F_BIT, F_MULT, F_STEP, F_DEST, F_BPM, F_HZLFO = range(15)
K_CONT, K_ENUM, K_INT, K_STEP, K_HOST, K_VIEW, K_TRIG, K_TITLE, K_CAP, K_QL, K_FLAG, K_HIDDEN, K_BANK, K_ROW, K_SEL = range(15)

INT_PARAMS = {"Steps", "Shift"}          # whole numbers, shown as numbers (a real value, not 0..1)


def fmt_of(e):
    p = e.p
    if p.labels:
        return F_NONE
    n = p.name
    if e.scope == "matrix":
        return F_DEST if n == "Dst" else F_SPCT if n == "Depth" else F_NONE
    if n in INT_PARAMS:
        return F_INT
    if n == "StepWrite":
        return F_NONE
    if n == "Bpm":
        return F_BPM
    if n == "Speed" and e.scope == "global":
        return F_MULT
    if n == "Speed" and e.scope == "mod":
        return F_HZLFO
    if p.suffix == "Hz":
        return F_HZLFO if p.hi <= 30 else F_HZ
    if p.suffix == "dB":
        return F_DB
    if p.suffix == "ms":
        return F_MS
    if p.lo == -1 and p.hi == 1:
        return F_PAN if n.endswith("Pan") else F_SPCT
    if p.lo < 0:
        return F_SPCT
    if n == "LoBit":
        return F_BIT
    if p.hi > 1:
        return F_NUM
    return F_PCT


def kind_of_entry(e):
    p = e.p
    if p.labels:
        return K_ENUM
    if e.scope == "matrix":
        if p.name == "Src":
            return K_ENUM
        if p.name == "Dst":
            return K_INT
    if p.name in INT_PARAMS:
        return K_INT
    if p.name == "StepWrite":
        return K_HIDDEN
    return K_CONT


def src_labels():
    return ["-"] + ["M%d" % (i + 1) for i in range(NMOD)]


def norm_of(p, v):
    n = (v - p.lo) / (p.hi - p.lo)
    n = min(1.0, max(0.0, n))
    if p.centre is not None:
        n = n ** p.skew()
    return n


def from_norm(p, n):
    n = min(1.0, max(0.0, n))
    if p.centre is not None:
        n = n ** (1.0 / p.skew())
    return p.lo + n * (p.hi - p.lo)


# ------------------------------------------------------------------------------------------------------------ pages
# (id, title, tile text). Index in this list = the `view` parameter's value.
PAGES = [("overview", "MANTRA", "")]
for _l in spec.LAYER_NAMES:
    for _id, _t, _tile in (("mix", "MIX", "MIX"), ("fi", "FILTER", "FILTER"), ("di", "DISTORTION", "DIST"),
                           ("de", "DELAY", "DELAY"), ("lo", "LO-FI", "LO-FI"), ("fl", "FLANGER", "FLANGER"),
                           ("gl", "GLITCH", "GLITCH")):
        PAGES.append(("%s_%s" % (_l.lower(), _id), "LAYER %s  %s" % (_l, _t), _tile))
for _m in range(NMOD):
    PAGES.append(("mod%d" % (_m + 1), "MODULATOR %d" % (_m + 1), "M%d" % (_m + 1)))
for _m in range(NMOD):
    PAGES.append(("st%d" % (_m + 1), "MODULATOR %d  STEPS" % (_m + 1), "S%d" % (_m + 1)))
for _x in range(4):
    PAGES.append(("mx%d" % (_x + 1), "MATRIX  SLOTS %d-%d" % (_x * 6 + 1, _x * 6 + 6), "%d-%d" % (_x * 6 + 1, _x * 6 + 6)))
PAGES += [("master1", "MASTER  EQ + REVERB", "EQ+REV"), ("master2", "MASTER  OUTPUT", "OUTPUT"), ("presets", "PRESETS", "PRESETS")]
N_VIEWS = len(PAGES)
PAGE_INDEX = {p[0]: i for i, p in enumerate(PAGES)}
assert N_VIEWS == 38

# ------------------------------------------------------------------------------------------------------- colours
# The six colours of the knob / key / cap pictures (never yellow). One page = one colour; art/userlayout.json may give a control
# its own ("colour"). Modulators green, matrix blue, master black; the effect pages follow their own colour.
COLOURS = ["red", "green", "blue", "brown", "black", "grey"]
EFF_COLOUR = {"mix": "grey", "fi": "red", "di": "brown", "de": "blue", "lo": "grey", "fl": "red", "gl": "brown"}


def page_colour(pid):
    if pid.startswith("mod") or pid.startswith("st"):
        return "green"
    if pid.startswith("mx"):
        return "blue"
    if pid.startswith("master"):
        return "black"
    if pid in ("overview", "presets"):
        return "grey"
    return EFF_COLOUR[pid.split("_", 1)[1]]


# knobs that make more sense as faders (the panel editor can swap any of them back)
FADER_NAMES = {"Level", "FiMix", "DiMix", "DeMix", "LoMix", "FlMix", "GlMix", "DeFb", "FlFb", "FlDepth", "TremDepth", "FlipDepth",
               "G_EqBass", "G_EqMid", "G_EqTreble", "G_RvMix", "G_OutVol", "G_OutMix"}


def is_fader(e):
    r = e.name if e.scope == "global" else e.name.split("_", 1)[1]
    return r in FADER_NAMES


# ------------------------------------------------------------------------------------------------------ parameters
EXTRA = []        # (key, name, kind, extra) appended after the 359 engine parameters, in this exact order
STEP0 = N_ENG
for _m in range(NMOD):
    for _s in range(NSTEP):
        EXTRA.append(("m%d_s%02d" % (_m + 1, _s + 1), "M%d Step %d" % (_m + 1, _s + 1), K_STEP, {}))
EXTRA.append(("g_hostsync", "Host Sync", K_HOST, {}))
EXTRA.append(("view", "Page", K_VIEW, {}))
EXTRA.append(("nav_zoom", "Overview", K_TRIG, {}))
EXTRA.append(("nav_prev", "Prev Page", K_TRIG, {}))
EXTRA.append(("nav_next", "Next Page", K_TRIG, {}))
for _i in range(1, N_VIEWS):
    EXTRA.append(("go_" + PAGES[_i][0], "Go " + PAGES[_i][1].title(), K_TRIG, {}))
EXTRA.append(("stepbank", "Q 17-32", K_BANK, {}))
EXTRA.append(("in_sec", "In Section", K_FLAG, {}))
EXTRA.append(("page_title", "Page Title", K_TITLE, {}))
# ---- 0.2.0: randomiser (one button + one amount per modulator / step / matrix page) and the preset manager
RND_AMOUNTS = ["10 %", "30 %", "50 %", "70 %", "90 %", "100 %"]
RND_PAGES = ["mod%d" % (m + 1) for m in range(NMOD)] + ["st%d" % (m + 1) for m in range(NMOD)] + ["mx%d" % (x + 1) for x in range(4)]
for _p in RND_PAGES:
    EXTRA.append(("rnd_" + _p, "RND", K_TRIG, {}))
    EXTRA.append(("rnda_" + _p, "RND Amount", K_SEL, {"options": RND_AMOUNTS, "default": 2}))
PR_STYLES = ["BASIC", "GATES", "FILTER SEQ", "GLITCH", "MOVEMENT", "DELAYS", "LO-FI", "SPACE", "LAYERS", "DESTRUCTION", "MY PRESETS"]
PR_ROWS, PR_COLS = 8, 2                  # the preset list: 2 columns x 8 rows = 16 per page
PR_PER_PAGE = PR_ROWS * PR_COLS
EXTRA.append(("pr_style", "Style", K_SEL, {"options": PR_STYLES, "default": 0}))
EXTRA.append(("pr_sel", "Preset", K_SEL, {"int": (0, 999), "default": 0}))
EXTRA.append(("pr_pg", "List Page", K_SEL, {"int": (0, 999), "default": 0}))
for _n in ("pr_prev", "pr_next", "pr_save", "pr_new", "pr_del"):
    EXTRA.append((_n, {"pr_prev": "List Prev", "pr_next": "List Next", "pr_save": "Save", "pr_new": "Save New", "pr_del": "Delete"}[_n], K_TRIG, {}))
EXTRA.append(("pr_cur", "Preset Name", K_TITLE, {}))
EXTRA.append(("pr_info", "List Info", K_TITLE, {}))
for _i in range(len(PR_STYLES)):
    EXTRA.append(("pr_sty_%d" % (_i + 1), "Style %d" % (_i + 1), K_ROW, {}))
for _i in range(PR_PER_PAGE):
    EXTRA.append(("pr_row_%d" % (_i + 1), "Preset %d" % (_i + 1), K_ROW, {}))
# captions (static texts drawn as live text) are appended by gen_force.py once the layout is known: key cap_<slug>
# then the 16 Q-Links
QL_COUNT = 16


# ------------------------------------------------------------------------------------------------------- controls
SCR_W, SCR_H, HDR_H = 1280, 628, 70


def E_(name):
    for e in spec.ALL:
        if e.name == name:
            return e
    raise KeyError(name)


def ctl(name, label=None, kind=None):
    e = E_(name)
    k = kind_of_entry(e)
    if kind is None:
        if k == K_CONT:
            kind = "slider_h" if is_fader(e) else "knob"
        elif k == K_INT:
            kind = "slider_h" if (e.scope == "matrix" and e.p.name == "Dst") else "knob"
        elif k == K_ENUM:
            n = len(e.p.labels) if e.p.labels else NMOD + 1
            kind = "toggle" if n == 2 else "popup"
    return {"kind": kind, "key": key_of(e), "label": label or label_of(e)}


def pair(a, b, sel):
    """Two controls in one place: `a` while the switch `sel` is Off, `b` while it is On (free time / synced division)."""
    ca, cb = ctl(a), ctl(b)
    ca["alt"] = cb
    ca["alt_sel"] = sel
    return ca


def layer_pages(l):
    L = spec.LAYER_NAMES[l]
    n = lambda s: "%s_%s" % (L, s)
    return {
        "mix": [ctl(n("Level")), ctl(n("Pan")), ctl(n("Bypass"), "BYPASS")] + [ctl(n("Slot%d" % i), "SLOT %d" % i) for i in range(1, 7)],
        "fi": [ctl(n("FiCut")), ctl(n("FiRes")), ctl(n("FiMix")), ctl(n("FiLow")), ctl(n("FiHi")), ctl(n("FiType"), "TYPE"), ctl(n("FiByp"))],
        "di": [ctl(n("DiDrive")), ctl(n("DiFuzz")), ctl(n("DiTone")), ctl(n("DiOut")), ctl(n("DiMix")), ctl(n("DiType"), "TYPE"),
               ctl(n("DiMono")), ctl(n("DiByp"))],
        "de": [pair(n("DeL"), n("DeLS"), n("DeSync")), pair(n("DeR"), n("DeRS"), n("DeSync")), ctl(n("DeFb")), ctl(n("DeLP")),
               ctl(n("DeBP")), ctl(n("DeIn")), ctl(n("DeMix")), ctl(n("DeSync")), ctl(n("DePing")), ctl(n("DeByp"))],
        "lo": [ctl(n("LoBit")), ctl(n("LoRate")), ctl(n("LoNoise")), ctl(n("LoTone")), ctl(n("LoMix")), ctl(n("LoByp"))],
        "fl": [pair(n("FlSpeed"), n("FlDiv"), n("FlSync")), ctl(n("FlDelay")), ctl(n("FlFb")), ctl(n("FlWidth")), ctl(n("FlTone")),
               ctl(n("FlDepth")), ctl(n("FlMix")), ctl(n("FlMode"), "MODE"), ctl(n("FlSync")), ctl(n("FlByp"))],
        "gl": [ctl(n("GlSize")), ctl(n("GlScale")), ctl(n("GlStretch")), ctl(n("GlSmooth")), ctl(n("GlMix")), ctl(n("GlMode"), "MODE"),
               ctl(n("GlRev")), ctl(n("GlByp"))],
    }


def mod_page(m):
    n = lambda s: "M%d_%s" % (m + 1, s)
    return [pair(n("Speed"), n("Div"), n("Sync")), ctl(n("Steps")), ctl(n("Gate")), ctl(n("Smooth")), ctl(n("Shuffle")),
            ctl(n("Tension")), ctl(n("Shift")), ctl(n("TremDepth")), ctl(n("FlipDepth")), ctl(n("Mode"), "MODE"),
            ctl(n("Sync")), ctl(n("Invert")), ctl(n("TremDest"), "TREM TO"), ctl(n("FlipDest"), "FLIP TO")]


MASTER1 = ["G_EqBass", "G_EqMid", "G_EqFreq", "G_EqTreble", "G_EqOn", "G_RvSize", "G_RvDecay", "G_RvMix", "G_RvBass", "G_RvTreble",
           "G_RvIn", "G_RvOn"]
MASTER2 = ["G_OutVol", "G_OutPan", "G_OutMix", "G_Speed", "G_Bpm", "G_Routing", "G_OutClip", "G_MasterOn"]
CAPTION_FOR = {"G_EqOn": "EQ", "G_RvOn": "REVERB", "G_RvIn": "VERB IN", "G_EqFreq": "MID FREQ", "G_EqMid": "MID", "G_EqBass": "BASS",
               "G_EqTreble": "TREBLE", "G_OutPan": "PAN", "G_OutMix": "MIX", "G_Speed": "SPEED", "G_Bpm": "BPM",
               "G_OutClip": "CLIPPER", "G_MasterOn": "MASTER"}


def master_pages():
    def mk(names):
        out = []
        for nm in names:
            c = ctl(nm)
            if nm in CAPTION_FOR:
                c["label"] = CAPTION_FOR[nm]
            out.append(c)
        return out
    m2 = mk(MASTER2)
    m2.insert(7, {"kind": "toggle", "key": "g_hostsync", "label": "HOST SYNC"})
    return mk(MASTER1), m2


def page_controls():
    """page index -> list of control dicts (without positions)"""
    out = {}
    for l in range(2):
        for pid, items in layer_pages(l).items():
            out[PAGE_INDEX["%s_%s" % (spec.LAYER_NAMES[l].lower(), pid)]] = items
    for m in range(NMOD):
        out[PAGE_INDEX["mod%d" % (m + 1)]] = mod_page(m) + [{"kind": "button", "key": "go_st%d" % (m + 1), "label": "STEPS >"}]
    for m in range(NMOD):
        steps = [{"kind": "slider_v", "key": "m%d_s%02d" % (m + 1, s + 1), "label": str(s + 1)} for s in range(NSTEP)]
        steps.append({"kind": "button", "key": "go_mod%d" % (m + 1), "label": "< SHAPE"})
        steps.append({"kind": "toggle", "key": "stepbank", "label": "Q 17-32"})
        out[PAGE_INDEX["st%d" % (m + 1)]] = steps
    for x in range(4):
        items = []
        for s in range(6):
            slot = x * 6 + s + 1
            src, byp = ctl("X%d_Src" % slot, "SOURCE"), ctl("X%d_Byp" % slot, "BYP")
            src["nocap"] = True
            byp["noname"] = True
            items += [src, ctl("X%d_Dst" % slot, "DESTINATION"), ctl("X%d_Depth" % slot, "DEPTH", "slider_h"), byp]
        out[PAGE_INDEX["mx%d" % (x + 1)]] = items
    m1, m2 = master_pages()
    out[PAGE_INDEX["master1"]] = m1
    out[PAGE_INDEX["master2"]] = m2
    return out


# --------------------------------------------------------------------------------------------- default placement
COLS = 5
CELL_W = 240
X0 = 160


def _cell_xy(i, n, rows, pitch, y0):
    r, c = divmod(i, COLS)
    # centre a short last row
    in_row = COLS if r < rows - 1 else n - (rows - 1) * COLS
    x0 = X0 + (COLS - in_row) * CELL_W / 2.0
    return int(round(x0 + c * CELL_W)), int(round(y0 + r * pitch))


def place_grid(items, extra_top=0):
    """Default positions for an effect / modulator / master page: a 5 column grid, row by row, in list order."""
    body = [it for it in items if it["kind"] != "button" or it["key"].startswith("go_") is False]
    btns = [it for it in items if it not in body]
    n = len(body)
    rows = max(1, int(math.ceil(n / float(COLS))))
    area_top, area_h = HDR_H + 8 + extra_top, SCR_H - HDR_H - 16 - extra_top
    pitch = min(200, area_h // rows)
    y0 = area_top + (area_h - rows * pitch) // 2
    r_knob = 46 if rows <= 2 else 36
    for i, it in enumerate(body):
        cx, top = _cell_xy(i, n, rows, pitch, y0)
        ch = 2 * r_knob + 107
        k = it["kind"]
        if k == "knob":
            it.update(x=cx, y=top + r_knob + 5, r=r_knob)
        elif k == "toggle":
            it.update(x=cx, y=top + ch // 2, w=170, h=82)
        elif k == "popup":
            it.update(x=cx, y=top + ch // 2 + 22, w=210, h=52, cap_y=top + ch // 2 - 24)
        elif k == "slider_h":      # a fader: the slot, then its name and value below (a block of about 130 px)
            it.update(x=cx, y=top + ch // 2 - 40, w=200, h=50)
        else:
            it.update(x=cx, y=top + 80, w=210, h=52)
        if "alt" in it:
            it["alt"].update({a: b for a, b in it.items() if a in ("x", "y", "w", "h", "r", "cap_y")})
    for j, b in enumerate(btns):
        b.update(x=1185, y=y0 + (rows - 1) * pitch + 110 + j * 70, w=150, h=52)
    return items


STEP_X0, STEP_DX, STEP_Y0, STEP_H = 43, 38, 82, 350   # the step faders (page pixels): first centre, pitch, top, height (src/step_touch reads these)


def place_steps(items):
    sl = [it for it in items if it["kind"] == "slider_v"]
    for i, it in enumerate(sl):
        it.update(x=STEP_X0 + STEP_DX * i, y=STEP_Y0 + STEP_H // 2, w=34, h=STEP_H)
    for it in items:
        if it["key"].startswith("go_"):
            it.update(x=190, y=SCR_H - 52, w=230, h=56)
        elif it["key"] == "stepbank":
            it.update(x=520, y=SCR_H - 100, w=240, h=82)
    return items


def place_matrix(items):
    """Six rows (pitch 80): Source popup | Destination slider | Depth slider | Bypass pill, with column captions on top."""
    y0, pitch = 152, 80
    cols = {"SOURCE": 130, "DESTINATION": 470, "DEPTH": 880, "BYP": 1190}
    ctrl = [it for it in items if it["kind"] != "caption"]
    for i, it in enumerate(ctrl):
        y = y0 + (i // 4) * pitch
        k = it["label"]
        x = cols[k]
        if k == "SOURCE":
            it.update(x=x, y=y, w=200, h=52)
        elif k == "DESTINATION":
            it.update(x=x, y=y, w=380, h=34)
        elif k == "DEPTH":
            it.update(x=x, y=y, w=300, h=34)
        else:
            it.update(x=x, y=y, w=53, h=37)
    items[:] = ctrl + [{"kind": "caption", "key": "cap_mxh_" + k.lower(), "label": k, "x": x, "y": 96, "w": 200 if k != "DESTINATION" else 380, "h": 36}
                       for k, x in cols.items()]
    return items


TILE_W, TILE_H = 150, 60


def overview_items():
    """Overview tiles: one row per section."""
    items = []
    rows = [("LAYER A", [(i, t) for i, (pid, ttl, t) in enumerate(PAGES) if pid.startswith("a_")], 128),
            ("LAYER B", [(i, t) for i, (pid, ttl, t) in enumerate(PAGES) if pid.startswith("b_")], 258),
            ("MODULATORS", [(i, t) for i, (pid, ttl, t) in enumerate(PAGES) if pid.startswith("mod")], 388),
            ("MATRIX", [(i, t) for i, (pid, ttl, t) in enumerate(PAGES) if pid.startswith("mx")], 518),
            ("MASTER", [(i, t) for i, (pid, ttl, t) in enumerate(PAGES) if pid.startswith("master") or pid == "presets"], 518)]
    for name, tiles, y in rows:
        x0 = 100 if name != "MASTER" else 820
        if len(tiles) == 8:
            x0 = 40
        pitch = 165 if len(tiles) <= 7 else 150
        if name == "MATRIX":
            pitch = 175
        if name == "MASTER":
            pitch = 150
        label = "MASTER + PRESETS" if name == "MASTER" else name
        items.append({"kind": "caption", "key": "cap_" + name.lower().replace(" ", "_"), "label": label, "x": x0 + (60 if name != "MASTER" else 150), "y": y - 44, "w": 300 if name == "MASTER" else 240, "h": 36})
        for j, (idx, t) in enumerate(tiles):
            items.append({"kind": "tile", "key": "go_" + PAGES[idx][0], "label": t, "x": x0 + j * pitch + 60, "y": y + 4, "w": TILE_W if (len(tiles) < 8 and name != "MASTER") else 138, "h": TILE_H})
    return items


def header_items():
    return [
        {"kind": "tile", "key": "nav_zoom", "label": "OVERVIEW", "x": 110, "y": 36, "w": 170, "h": 50, "when": "in_sec:1", "colour": "grey"},
        {"kind": "tile", "key": "nav_prev", "label": "<", "x": 250, "y": 36, "w": 64, "h": 50, "colour": "grey"},
        {"kind": "tile", "key": "nav_next", "label": ">", "x": 324, "y": 36, "w": 64, "h": 50, "colour": "grey"},
        {"kind": "readout", "key": "page_title", "label": "", "x": 660, "y": 36, "w": 500, "h": 50, "tcolor": "f2f2f2"},
    ]


def presets_items():
    """The PRESETS page: styles list (left), the paged preset list (2 x 8), list paging and the save / new / delete buttons."""
    it = [{"kind": "list", "key": "pr_sty", "cols": 1, "rows": len(PR_STYLES), "x": 24, "y": 78, "w": 270, "th": 44, "gap": 4, "label": "STYLES",
           "colour": "grey"},
          {"kind": "list", "key": "pr_row", "cols": PR_COLS, "rows": PR_ROWS, "x": 316, "y": 78, "w": 940, "th": 52, "gap": 6, "label": "PRESETS",
           "order": "cols", "colour": "grey"},
          {"kind": "tile", "key": "pr_prev", "label": "<", "x": 360, "y": 580, "w": 76, "h": 52},
          {"kind": "tile", "key": "pr_next", "label": ">", "x": 456, "y": 580, "w": 76, "h": 52},
          {"kind": "readout", "key": "pr_info", "label": "", "x": 640, "y": 580, "w": 190, "h": 40, "tsize": 26, "talign": "center"},
          {"kind": "tile", "key": "pr_save", "label": "SAVE", "x": 790, "y": 580, "w": 120, "h": 52},
          {"kind": "tile", "key": "pr_new", "label": "SAVE NEW", "x": 930, "y": 580, "w": 150, "h": 52},
          {"kind": "tile", "key": "pr_del", "label": "DELETE", "x": 1100, "y": 580, "w": 130, "h": 52, "colour": "red"},
          {"kind": "readout", "key": "pr_cur", "label": "", "x": 1040, "y": 36, "w": 440, "h": 50, "hdr": True, "tcolor": "f2f2f2"}]
    return it


def add_rnd(items, pid):
    """The randomiser: a small RND key and its amount (10 .. 100 %) in the header strip of a modulator / step / matrix page."""
    items.append({"kind": "tile", "key": "rnd_" + pid, "label": "RND", "x": 1060, "y": 36, "w": 100, "h": 46, "hdr": True, "colour": "grey"})
    items.append({"kind": "popup", "key": "rnda_" + pid, "label": "AMOUNT", "x": 1196, "y": 36, "w": 132, "h": 46, "hdr": True, "nocap": True})


def default_layout():
    pc = page_controls()
    pages = []
    for i, (pid, title, tile) in enumerate(PAGES):
        if i == 0:
            items = overview_items()
        else:
            items = pc.get(i, [])
            if pid.startswith("st"):
                place_steps(items)
            elif pid.startswith("mx"):
                place_matrix(items)
            elif pid == "presets":
                items = presets_items()
            else:
                place_grid(items)
            if pid in RND_PAGES:
                add_rnd(items, pid)
        col = page_colour(pid)
        for it in items:
            if it["kind"] == "tile" and pid == "overview":
                it.setdefault("colour", page_colour(it["key"][3:]))
            else:
                it.setdefault("colour", col)
            if "alt" in it:
                it["alt"].setdefault("colour", it["colour"])
        pages.append({"index": i, "id": pid, "title": title, "items": items})
    return {"version": 1, "screen": [SCR_W, SCR_H], "header": header_items(), "pages": pages}
