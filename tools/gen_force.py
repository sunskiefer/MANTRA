#!/usr/bin/env python3
"""MANTRA Force build generator.   python3 tools/gen_force.py   (run from the repo root)

Reads  tools/model.py (+ spec.py, presets.py)  and  art/userlayout.json (the layout exported by the panel editor, optional)
Writes params.json  src/param_ids.h  art/layout_default.json  layout.conf  presets.json  vst.json  docs/INVENTORY.md
       docs/layout_map.csv  panel_editor/MANTRA-panel-editor.html (via tools/panel_editor/make_editor.py)

Positions live in ONE place: art/userlayout.json (or the default). A control the user moved stays where they put it.
"""
import json
import math
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import model as M  # noqa: E402
import spec  # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools", "art"))
import ui_art  # noqa: E402

S = spec
Y_OFF = 86


def slug(s):
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_") or "x"


# ------------------------------------------------------------------------------------------------ the layout
USER_KEEP = ("x", "y", "w", "h", "r", "cap_y", "label", "hidden", "colour")
ADDABLE = {"knob": ("cont",), "slider_h": ("cont", "int", "step"), "slider_v": ("cont", "int", "step"),
           "toggle": ("enum2",), "popup": ("enum",)}


SWAPPABLE = ("knob", "slider_h")      # a continuous control may be a knob or a fader (panel editor: Type)


def ok_swap(key):
    for e in S.ALL:
        if M.key_of(e) == key:
            return M.kind_of_entry(e) == M.K_CONT
    return False


def addable_keys():
    """key -> (name, class) for every parameter the editor's 'Add control' may bind: cont / int / step / enum2 / enum."""
    out = {}
    for e in S.ALL:
        k = M.kind_of_entry(e)
        key = M.key_of(e)
        if k == M.K_CONT:
            out[key] = (M.qname(e), "cont")
        elif k == M.K_INT:
            out[key] = (M.qname(e), "int")
        elif k == M.K_ENUM:
            n = len(e.p.labels) if e.p.labels else M.NMOD + 1
            out[key] = (M.qname(e), "enum2" if n == 2 else "enum")
    for key, name, k, _x in M.EXTRA:
        if k == M.K_STEP:
            out[key] = (name, "step")
        elif k == M.K_HOST:
            out[key] = (name, "enum2")
    return out


def load_layout():
    """-> (layout, default): the layout to build (default + the user's moves and additions) and the untouched default."""
    import copy
    d = M.default_layout()
    default = copy.deepcopy(d)
    path = os.path.join(ROOT, "art", "userlayout.json")
    if os.path.exists(path):
        u = json.load(open(path))
        upd = {}
        for pg in u.get("pages", []) + [{"id": "hdr", "items": u.get("header", [])}]:
            for it in pg.get("items", []):
                upd[(pg["id"], it["key"], it.get("alt_sel", ""))] = it
        for pg in d["pages"] + [{"id": "hdr", "items": d["header"]}]:
            for it in pg["items"]:
                o = upd.get((pg["id"], it["key"], it.get("alt_sel", "")))
                if o:
                    for k in USER_KEEP:
                        if k in o:
                            it[k] = o[k]
                    if o.get("kind") in SWAPPABLE and it["kind"] in SWAPPABLE and ok_swap(it["key"]):
                        it["kind"] = o["kind"]
                    if "alt" in it and "alt" in o:
                        for k in USER_KEEP:
                            if k in o["alt"]:
                                it["alt"][k] = o["alt"][k]
        # controls the user added in the editor
        ok = addable_keys()
        added = 0
        for pg in u.get("pages", []):
            tgt = next((p for p in d["pages"] if p["id"] == pg["id"]), None)
            for it in pg.get("items", []):
                if not it.get("user") or tgt is None:
                    continue
                kind, key = it.get("kind"), it.get("key")
                if key not in ok or kind not in ADDABLE or ok[key][1] not in ADDABLE[kind]:
                    print("layout: ignored added control %s/%s on %s (not a valid binding)" % (kind, key, pg["id"]))
                    continue
                n = {k: it[k] for k in ("kind", "key", "label", "x", "y", "w", "h", "r", "cap_y", "hidden", "colour") if k in it}
                n["user"] = True
                n.setdefault("label", ok[key][0])
                tgt["items"].append(n)
                added += 1
        print("layout: merged art/userlayout.json (%d moved/edited, %d added)" % (len(upd), added))
    return d, default


# ------------------------------------------------------------------------------------------------ parameters
def build_params(layout):
    """-> (entries, meta): the full ordered parameter list. Engine params first (spec order), then EXTRA, caps, flags, Q-Links."""
    P = []   # dicts for params.json
    meta = []  # same order: dict(kind, fmt, eng, labels, lo, hi)

    def add(entry, **m):
        P.append(entry)
        meta.append(m)

    for e in S.ALL:
        p, k = e.p, M.kind_of_entry(e)
        ent = {"key": M.key_of(e), "name": M.qname(e)}
        if k == M.K_ENUM:
            labels = list(p.labels) if p.labels else M.src_labels()
            ent.update(options=labels, default=int(round(p.df)))
            add(ent, kind=k, fmt=M.F_NONE, eng=e.idx, labels=labels, lo=0, hi=len(labels) - 1)
        elif k == M.K_INT:
            hi = p.hi if e.scope != "matrix" else len(S.DEST_NAMES) - 1
            ent.update({"min": float(p.lo), "max": float(hi), "default": float(p.df), "display": "int", "dynamic_display": True})
            if e.scope == "matrix":
                ent["nudge_pct"] = 1.2
            add(ent, kind=k, fmt=M.fmt_of(e), eng=e.idx, labels=None, lo=p.lo, hi=hi)
        elif k == M.K_HIDDEN:
            ent.update({"min": 0.0, "max": 1.0, "default": 0.0, "poll": False})
            add(ent, kind=k, fmt=M.F_NONE, eng=e.idx, labels=None, lo=0, hi=1)
        else:
            ent.update({"min": 0.0, "max": 1.0, "default": round(M.norm_of(p, p.df), 6), "dynamic_display": True})
            add(ent, kind=k, fmt=M.fmt_of(e), eng=e.idx, labels=None, lo=p.lo, hi=p.hi)
    assert len(P) == M.N_ENG
    for key, name, k, _x in M.EXTRA:
        ent = {"key": key, "name": name}
        if k == M.K_STEP:
            ent.update({"min": 0.0, "max": 1.0, "default": 1.0, "dynamic_display": True})
        elif k == M.K_HOST:
            ent.update({"options": ["Off", "On"], "default": 1})
        elif k == M.K_VIEW:
            ent.update({"min": 0.0, "max": float(M.N_VIEWS - 1), "default": 0.0, "display": "int", "poll": False, "refresh_display": True})
        elif k == M.K_TRIG:
            ent.update({"min": 0.0, "max": 1.0, "default": 0.0, "momentary": True, "type": "trigger", "refresh_display": True})
        elif k == M.K_BANK:
            ent.update({"options": ["Q 1-16", "Q 17-32"], "default": 0, "refresh_display": True})
        elif k == M.K_FLAG:
            ent.update({"options": ["Off", "On"], "default": 0})
        elif k == M.K_TITLE:
            ent.update({"min": 0.0, "max": 0.0, "default": 0.0, "display": "string"})
        elif k == M.K_ROW:     # a list row (preset / style): the framework's list protocol (text = get(key), selected = get(key_on), a tap = set(key, 1))
            ent.update({"min": 0.0, "max": 1.0, "default": 0.0, "display": "string"})
        elif k == M.K_SEL:     # a choice kept by the plugin itself (never sent to the DSP)
            if "options" in _x:
                ent.update({"options": list(_x["options"]), "default": _x["default"]})
                add(ent, kind=k, fmt=M.F_NONE, eng=-1, labels=list(_x["options"]), lo=0, hi=len(_x["options"]) - 1)
                if key.startswith("pr_"):
                    ent["refresh_display"] = True
                continue
            lo, hi = _x["int"]
            ent.update({"min": float(lo), "max": float(hi), "default": float(_x["default"]), "display": "int", "dynamic_display": True,
                        "nudge_pct": 10, "refresh_display": True})
            if key == "pr_pg":
                ent.update({"poll": False})
            add(ent, kind=k, fmt=M.F_INT, eng=-1, labels=None, lo=lo, hi=hi)
            continue
        add(ent, kind=k, fmt=M.F_NONE, eng=-1, labels=None, lo=0, hi=1)
    return P, meta


def collect_texts(layout):
    """Every static text that is drawn as live text: tile / button labels, captions, popup captions. -> ordered list"""
    texts = []

    def see(t):
        if t and t not in texts:
            texts.append(t)
    for it in layout["header"]:
        if it["kind"] == "tile":
            see(it["label"])
    for pg in layout["pages"]:
        for it in pg["items"]:
            if it["kind"] in ("tile", "caption", "button"):
                see(it["label"])
            elif it["kind"] == "popup":
                see(it["label"])
                if "alt" in it:
                    see(it["alt"]["label"])
            if "alt" in it and it["alt"]["kind"] == "popup":
                see(it["alt"]["label"])
    return texts


def caption_registry(found):
    """art/captions.lst: every caption text ever used, one per line, in first-use order (append-only)."""
    path = os.path.join(ROOT, "art", "captions.lst")
    known = [l.rstrip("\n") for l in open(path)] if os.path.exists(path) else []
    keys = {cap_key(t) for t in known}
    for t in found:
        if cap_key(t) not in keys:
            known.append(t)
            keys.add(cap_key(t))
    open(path, "w", newline="\n").write("".join(t + "\n" for t in known))
    return known


def cap_key(text):
    return "cap_" + slug(text.replace("<", "lt").replace(">", "gt"))


def flag_defs(layout):
    """Derived 0/1 params: page flags pg_<id> (view == page) and mode flags for a control pair (page + switch + mode)."""
    flags = []
    for i, pg in enumerate(layout["pages"]):
        flags.append(("pg_" + pg["id"], i, None, None))
    seen = {}
    for pg in layout["pages"]:
        for it in pg["items"]:
            if "alt" in it:
                for mode in (0, 1):
                    k = "pm_%s_%s_%d" % (pg["id"], it["alt_sel"].lower(), mode)
                    if k not in seen:
                        seen[k] = 1
                        flags.append((k, M.PAGE_INDEX[pg["id"]], it["alt_sel"].lower(), mode))
    return flags


# ------------------------------------------------------------------------------------------------ q-links
def reading_order(items):
    out = []
    for it in items:
        if it.get("hidden"):
            continue
        out.append(it)
    # rows by y (60 px bands), then x
    out.sort(key=lambda it: (round(it["y"] / 90.0), it["x"]))
    return out


def qlink_tables(layout, key_index, flag_index):
    """-> VIEW_QL[view][bank][16] of (a, b, sel): the parameter (or the pair's two parameters) each Q-Link turns on that page"""
    tab = []
    for i, pg in enumerate(layout["pages"]):
        pid = pg["id"]
        banks = []
        if pid.startswith("st"):
            sliders = [it for it in pg["items"] if it["kind"] == "slider_v"]
            sliders.sort(key=lambda it: it["x"])
            for b in range(2):
                row = [(key_index[sliders[b * 16 + n]["key"]], -1, -1) for n in range(16)]
                banks.append(row)
        elif pid.startswith("mx"):
            by = {"DEPTH": [], "DESTINATION": [], "SOURCE": []}
            for it in pg["items"]:
                if it["kind"] != "caption" and it["label"] in by:
                    by[it["label"]].append(it)
            for k in by:
                by[k].sort(key=lambda it: it["y"])
            order = by["DEPTH"] + by["DESTINATION"] + by["SOURCE"]
            row = [(key_index[it["key"]], -1, -1) for it in order][:16]
            row += [(-1, -1, -1)] * (16 - len(row))
            banks = [row, row]
        elif i == 0:
            row = [(-1, -1, -1)] * 16
            banks = [row, row]
        elif pid == "presets":      # the style, then the preset: Q-Link 1 / 2 (or the data wheel on the list)
            row = [(key_index["pr_style"], -1, -1), (key_index["pr_sel"], -1, -1)] + [(-1, -1, -1)] * 14
            banks = [row, row]
        else:
            order = [it for it in reading_order(pg["items"]) if it["kind"] in ("knob", "toggle", "popup", "slider_h", "slider_v") and not it.get("hdr")]
            row = []
            for it in order[:16]:
                if "alt" in it:
                    row.append((key_index[it["key"]], key_index[it["alt"]["key"]], key_index[it["alt_sel"].lower()]))
                else:
                    row.append((key_index[it["key"]], -1, -1))
            row += [(-1, -1, -1)] * (16 - len(row))
            banks = [row, row]
        tab.append(banks)
    return tab


# ------------------------------------------------------------------------------------------------ outputs
def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write(path, text):
    full = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    open(full, "w", newline="\n").write(text)


def main():
    import shutil
    for d in ("tiles", "slots"):
        shutil.rmtree(os.path.join(ROOT, "art", d), ignore_errors=True)
    layout, default_layout = load_layout()
    texts = caption_registry(collect_texts(layout))
    P, meta = build_params(layout)
    flags = flag_defs(layout)
    for k, v, sel, mode in flags:
        P.append({"key": k, "name": k[:10], "options": ["Off", "On"], "default": 0})
        meta.append(dict(kind=M.K_FLAG, fmt=M.F_NONE, eng=-1, labels=None, lo=0, hi=1, flag=(v, sel, mode)))
    flag_first = len(P) - len(flags)
    ql_first = len(P)
    for n in range(1, M.QL_COUNT + 1):
        P.append({"key": "ql%d" % n, "name": "Q-Link %d" % n, "min": 0.0, "max": 1.0, "default": 0.0, "dynamic_name": True,
                  "dynamic_display": True})
        meta.append(dict(kind=M.K_QL, fmt=M.F_NONE, eng=-1, labels=None, lo=0, hi=1))
    # captions last: their list (art/captions.lst) is append-only, so editing the layout never moves a parameter
    cap_first = len(P)
    for t in texts:
        P.append({"key": cap_key(t), "name": t[:10], "min": 0.0, "max": 0.0, "default": 0.0, "display": "string", "poll": False})
        meta.append(dict(kind=M.K_CAP, fmt=M.F_NONE, eng=-1, labels=None, lo=0, hi=0, text=t))
    key_index = {p["key"]: i for i, p in enumerate(P)}
    assert len(key_index) == len(P), "duplicate keys"
    NP = len(P)
    json.dump({"name": "MANTRA", "params": P}, open(os.path.join(ROOT, "params.json"), "w"), indent=1)

    # ---- param_ids.h
    qtab = qlink_tables(layout, key_index, None)
    out = ["// GENERATED by tools/gen_force.py -- do not edit.", "#pragma once", "#include <stdint.h>", "namespace mxf {",
           "constexpr int N_PARAMS = %d;" % NP, "constexpr int N_ENG = %d;" % M.N_ENG, "constexpr int P_STEP0 = %d;" % M.STEP0,
           "constexpr int N_STEP = %d;" % (M.NMOD * M.NSTEP), "constexpr int N_VIEWS = %d;" % M.N_VIEWS,
           "constexpr int P_CAP0 = %d;" % cap_first, "constexpr int N_CAP = %d;" % len(texts),
           "constexpr int P_FLAG0 = %d;" % flag_first, "constexpr int N_FLAG = %d;" % len(flags), "constexpr int P_QL1 = %d;" % ql_first]
    for nm in ("g_hostsync", "view", "nav_zoom", "nav_prev", "nav_next", "stepbank", "in_sec", "page_title", "pr_style", "pr_sel", "pr_pg",
               "pr_prev", "pr_next", "pr_save", "pr_new", "pr_del", "pr_cur", "pr_info", "pr_sty_1", "pr_row_1"):
        out.append("constexpr int P_%s = %d;" % (nm.upper(), key_index[nm]))
    out.append("constexpr int N_PR_STY = %d, N_PR_ROW = %d, PR_COLS = %d, PR_ROWS = %d;" % (len(M.PR_STYLES), M.PR_PER_PAGE, M.PR_COLS, M.PR_ROWS))
    out.append("constexpr int P_RND0 = %d, N_RND = %d;   // rnd_<page> at P_RND0 + 2 j, its amount at + 1" % (key_index["rnd_" + M.RND_PAGES[0]], len(M.RND_PAGES)))
    out.append("static const char* const PR_STYLE_NAME[N_PR_STY] = {" + ",".join(cstr(x) for x in M.PR_STYLES) + "};")
    out.append("constexpr int P_PRESETS_VIEW = %d;" % M.PAGE_INDEX["presets"])
    out.append("constexpr int ST_VIEW1 = %d, ST_N = %d, ST_X0 = %d, ST_DX = %d, ST_Y0 = %d, ST_H = %d;   // the step pages and faders, for src/step_touch.cc"
               % (M.PAGE_INDEX["st1"], M.NSTEP, M.STEP_X0, M.STEP_DX, M.STEP_Y0, M.STEP_H))
    out.append("constexpr int P_GO1 = %d;  // go_<page>, page 1 .. N_VIEWS-1" % key_index["go_" + M.PAGES[1][0]])
    out += ["enum Kind { K_CONT, K_ENUM, K_INT, K_STEP, K_HOST, K_VIEW, K_TRIG, K_TITLE, K_CAP, K_QL, K_FLAG, K_HIDDEN, K_BANK, K_ROW, K_SEL };",
            "enum Fmt { F_NONE, F_PCT, F_PAN, F_HZ, F_DB, F_MS, F_NUM, F_INT, F_SPCT, F_BIT, F_MULT, F_STEP, F_DEST, F_BPM, F_HZLFO };"]
    out.append("static const char* const KEYS[N_PARAMS] = {" + ",".join(cstr(p["key"]) for p in P) + "};")
    out.append("static const char* const NAMES[N_PARAMS] = {" + ",".join(cstr(p["name"]) for p in P) + "};")
    out.append("static const uint8_t KIND[N_PARAMS] = {" + ",".join(str(m["kind"]) for m in meta) + "};")
    out.append("static const uint8_t FMT[N_PARAMS] = {" + ",".join(str(m["fmt"]) for m in meta) + "};")
    out.append("static const int16_t ENG[N_PARAMS] = {" + ",".join(str(m["eng"]) for m in meta) + "};")
    out.append("static const float LO[N_PARAMS] = {" + ",".join("%.9g" % m["lo"] for m in meta) + "};")
    out.append("static const float HI[N_PARAMS] = {" + ",".join("%.9g" % m["hi"] for m in meta) + "};")
    # enum labels
    out.append("struct EnumDef { const char* const* labels; int n; };")
    enum_defs, enum_of = [], {}
    for i, m in enumerate(meta):
        if m["labels"]:
            sig = tuple(m["labels"])
            if sig not in enum_of:
                enum_of[sig] = len(enum_defs)
                enum_defs.append(sig)
                out.append("static const char* const EL%d[] = {%s};" % (enum_of[sig], ",".join(cstr(x) for x in sig)))
    out.append("static const EnumDef ENUMS[] = {" + ",".join("{EL%d,%d}" % (i, len(s)) for i, s in enumerate(enum_defs)) + "};")
    out.append("static const int16_t ENUM_OF[N_PARAMS] = {" + ",".join(str(enum_of[tuple(m['labels'])]) if m["labels"] else "-1" for m in meta) + "};")
    out.append("static const char* const DEST_SHORT[%d] = {%s};" % (len(M.DEST_SHORT), ",".join(cstr(x) for x in M.DEST_SHORT)))
    out.append("static const char* const CAP_TEXT[N_CAP > 0 ? N_CAP : 1] = {" + ",".join(cstr(t) for t in texts) + "};")
    out.append("static const char* const VIEW_TITLE[N_VIEWS] = {" + ",".join(cstr(p[1]) for p in M.PAGES) + "};")
    # the randomiser: the parameters of each modulator / step / matrix page (the RND key and its amount are not in them)
    rl = []
    for pid in M.RND_PAGES:
        pg = next(q for q in layout["pages"] if q["id"] == pid)
        keys = []
        for it in pg["items"]:
            if it.get("hidden") or it.get("hdr") or it["kind"] not in ("knob", "slider_h", "slider_v", "toggle", "popup"):
                continue
            if it["key"] in ("stepbank",) or it["key"].endswith("_byp") and pid.startswith("mx"):
                continue
            keys.append(it["key"])
            if "alt" in it:
                keys.append(it["alt"]["key"])
        rl.append(keys)
    mx = max(len(k) for k in rl)
    out.append("constexpr int RND_MAX = %d;" % (mx + 1))
    out.append("static const int16_t RND_LIST[N_RND][RND_MAX] = {" + ",".join(
        "{" + ",".join([str(key_index[k]) for k in keys] + ["-1"] * (mx + 1 - len(keys))) + "}" for keys in rl) + "};")
    out.append("struct FlagDef { int16_t page; int16_t sel; int8_t mode; };  // sel -1: page flag")
    out.append("static const FlagDef FLAGS[N_FLAG] = {" + ",".join(
        "{%d,%d,%d}" % (v, key_index[sel] if sel else -1, mode if sel else 0) for (_k, v, sel, mode) in flags) + "};")
    out.append("struct QlSlot { int16_t a, b, sel; };  // the parameter a Q-Link turns; b instead while parameter `sel` is On")
    rows = []
    for banks in qtab:
        rows.append("{" + ",".join("{" + ",".join("{%d,%d,%d}" % s for s in bank) + "}" for bank in banks) + "}")
    out.append("static const QlSlot VIEW_QL[N_VIEWS][2][16] = {" + ",".join(rows) + "};")
    out.append("}  // namespace mxf")
    write("src/param_ids.h", "\n".join(out) + "\n")

    # ---- presets.json
    import presets as PR
    ps = PR.all_presets()
    plist = []
    seen = set()
    for pr in ps:
        nm = ("%s %s" % (pr.cat, pr.name))[:24] if False else pr.name
        if nm in seen:
            nm = (pr.cat[:6] + " " + pr.name)
        nm = nm[:24]
        assert nm not in seen, nm
        seen.add(nm)
        vals = {}
        for e in S.ALL:
            k = M.key_of(e)
            if e.name == "G_StepWrite":
                continue
            v = pr.p[e.name]
            kk = M.kind_of_entry(e)
            if kk == M.K_CONT:
                v = round(M.norm_of(e.p, v), 6)
            elif kk == M.K_ENUM:
                v = int(round(v))
            elif kk == M.K_INT:
                v = int(round(v))
            vals[k] = v
        for m in range(M.NMOD):
            for s in range(M.NSTEP):
                vals["m%d_s%02d" % (m + 1, s + 1)] = round(float(pr.packs[m][s]), 3)
        plist.append({"name": nm, "values": vals})
    json.dump({"presets": plist}, open(os.path.join(ROOT, "presets.json"), "w"), separators=(",", ":"))
    write_factory(ps, plist, key_index)

    # ---- vst.json
    vst = {"name": "MANTRA", "vendor": "L'Cronx", "uid": "Mntr", "version": 200, "so": "mantra.so", "params": "params.json",
           "effect": True, "build": {"root": ".", "sources": ["src/engine.cc", "src/step_touch.cc"], "cflags": ["-Isrc", "-O2"], "libs": ["-lm", "-ldl", "-lpthread"]},
           "layout": "layout.conf", "title_font": "art/fonts/TitilliumWeb-SemiBold.ttf",
           "art": "html",
           "defines": {"HAS_SONG_POS": 1, "HAS_DISPLAY_REV": 1, "DISPLAY_REV_NO_UPDATE": 1, "PARAM_TEXT_MAX": 64, "MODULE_SUBDIR": "\"presets\""},
           "presets": "presets.json"}
    if os.path.exists(os.path.join(ROOT, "art", "tile.png")):
        vst["tile"] = "art/tile.png"
    json.dump(vst, open(os.path.join(ROOT, "vst.json"), "w"), indent=2)

    # ---- layout.conf
    write("layout.conf", make_conf(layout, key_index, texts))
    json.dump(default_layout, open(os.path.join(ROOT, "art", "layout_default.json"), "w"), separators=(",", ":"))
    json.dump({k: [v[0], v[1]] for k, v in addable_keys().items()}, open(os.path.join(ROOT, "art", "addable_keys.json"), "w"), separators=(",", ":"))
    write_docs(layout, P, meta, key_index, qtab)
    print("params %d (engine %d, steps %d, captions %d, flags %d, Q-Links 16)  pages %d  presets %d" %
          (NP, M.N_ENG, M.NMOD * M.NSTEP, len(texts), len(flags), M.N_VIEWS, len(plist)))


# ------------------------------------------------------------------------------------------------ factory presets
STYLE_OF_CAT = {"Basic": "BASIC", "Gates": "GATES", "Filter Sequences": "FILTER SEQ", "Glitch": "GLITCH", "Movement": "MOVEMENT",
                "Delays": "DELAYS", "Lo-Fi": "LO-FI", "Space": "SPACE", "Layers": "LAYERS", "Destruction": "DESTRUCTION"}


def write_factory(ps, plist, key_index):
    """src/factory.h: the factory presets (tools/presets.py) for the engine's own preset manager (styles list, paged list, load by tap).
    Values are in the plugin's parameter units (as presets.json), one float per preset parameter."""
    pidx = [key_index[k] for k in plist[0]["values"].keys()]
    names, styles, rows = [], [], []
    for pr, pl in zip(ps, plist):
        names.append(pl["name"])
        styles.append(M.PR_STYLES.index(STYLE_OF_CAT[pr.cat]))
        rows.append("{" + ",".join("%.6g" % float(pl["values"][k]) for k in plist[0]["values"].keys()) + "}")
    out = ["// GENERATED by tools/gen_force.py -- do not edit.", "#pragma once", "#include <stdint.h>", "namespace mxf {",
           "constexpr int N_FACTORY = %d, N_FP = %d;" % (len(ps), len(pidx)),
           "static const int16_t FP_IDX[N_FP] = {" + ",".join(map(str, pidx)) + "};",
           "static const uint8_t FP_STYLE[N_FACTORY] = {" + ",".join(map(str, styles)) + "};",
           "static const char* const FP_NAME[N_FACTORY] = {" + ",".join(cstr(n) for n in names) + "};",
           "static const float FP_VAL[N_FACTORY][N_FP] = {" + ",".join(rows) + "};", "}  // namespace mxf"]
    write("src/factory.h", "\n".join(out) + "\n")


# ------------------------------------------------------------------------------------------------ layout.conf
THEME = """# MANTRA page layout. GENERATED by tools/gen_force.py -- positions come from art/userlayout.json (panel editor) or the default.
# Look: flat dark pages like the panel editor (art/panel.png), the knob / key / cap pictures in art/, one colour per page (tools/model.py page_colour).
# Text sizes: Meta4MPCXXL (label_scale 1.7: knob name 35.7 px in a 221 x 48 box, value 37.4 px).
scale_names=1
label_scale=1.7
theme_bg=141414
theme_panel=1c1c1c
theme_line=3a3a3a
theme_ink=f2f2f2
theme_ink_dim=a8a8a8
theme_ink_faint=6e6e6e
theme_accent=2ea150
theme_accent_hi=5ad17f
theme_btn_on=7fe0a0
theme_knob_face=1a1a1a
theme_knob_ring=3c3c3c
theme_knob_dot=2ea150
theme_bar=1c1c1c
theme_seg_active=2ea150
theme_seg_inactive=1a1a1a
theme_seg_active_tx=f2f2f2
theme_btn_text=141414
theme_tab_on=2ea150
theme_tab_on_tx=f2f2f2
theme_lcd=101010
theme_box=111111
qlinks_track = ql1,ql2,ql3,ql4,ql5,ql6,ql7,ql8,ql9,ql10,ql11,ql12,ql13,ql14,ql15,ql16

[tab MANTRA]
art file=art/panel.png x=0 y=86 w=1280 h=628 fit=stretch
"""

KEY_INK = {"red": "f4f4f4", "green": "f4f4f4", "blue": "f4f4f4", "brown": "f4f4f4", "black": "f4f4f4", "grey": "141414"}
POP_INK = {"red": "ff8a80", "green": "6ee09a", "blue": "7db4ff", "brown": "e6a56b", "black": "e4e4e6", "grey": "f0f0f2"}
USED_ART = set()


def colour_of(it):
    c = it.get("colour")
    return c if c in M.COLOURS else "grey"


def tile_art(colour, w, h, lit):
    """art/tiles/tile_<colour>_<w>x<h>_<off|on>.png: the user's key picture, nine-sliced to the size (made when first needed)."""
    rel = "art/tiles/tile_%s_%dx%d_%s.png" % (colour, w, h, "on" if lit else "off")
    if rel not in USED_ART:
        os.makedirs(os.path.join(ROOT, "art", "tiles"), exist_ok=True)
        ui_art.tile(colour, w, h, lit, os.path.join(ROOT, rel))
        USED_ART.add(rel)
    return rel


def slot_art(w, h, vert):
    rel = "art/slots/slot_%s_%dx%d.png" % ("v" if vert else "h", w, h)
    if rel not in USED_ART:
        os.makedirs(os.path.join(ROOT, "art", "slots"), exist_ok=True)
        ui_art.slot(w, h, vert, os.path.join(ROOT, rel))
        USED_ART.add(rel)
    return rel


def emit(it, when, name_of, lines):
    k = it["kind"]
    x, y = int(it["x"]), int(it["y"]) + Y_OFF
    w = (" when=" + when) if when else ""
    key = it["key"]
    lab = name_of(key)
    col = colour_of(it)
    if k == "knob":
        lines.append('knob cx=%d cy=%d r=%d label="%s" key=%s strip=art/knobs/knob_%s.png frames=128%s' % (x, y, it.get("r", 40), lab, key, col, w))
    elif k == "toggle":
        img = "img=art/keys/key_%s_off.png img_on=art/keys/key_%s_on.png" % (col, col)
        if it.get("noname"):
            lines.append('toggle cx=%d cy=%d label="%s" key=%s ns=0 %s w=40 h=56%s' % (x, y, lab, key, img, w))
        else:
            lines.append('toggle cx=%d cy=%d label="%s" key=%s bw=%d %s w=62 h=86%s' % (x, y, lab, key, min(it.get("w", 170), 236), img, w))
    elif k == "popup":
        lines.append('popup cx=%d cy=%d w=%d h=%d label="" key=%s accent=%s%s' % (x, y, it.get("w", 210), it.get("h", 52), key, POP_INK[col], w))
        cy = int(it.get("cap_y", it["y"] - 44)) + Y_OFF
        if not it.get("nocap"):
            lines.append('readout cx=%d cy=%d w=%d h=%d label="" key=%s box=0 tsize=26 tweight=600 talign=center%s' %
                         (x, cy, it.get("w", 210), 36, cap_key(it["label"]), w))
    elif k == "slider_h":
        sw, sh = int(it.get("w", 200)), int(it.get("h", 50))
        look = "img=art/caps/cap_%s_h.png base=%s" % (col, slot_art(sw, sh, False))
        if it["label"] in ("DEPTH", "DESTINATION"):      # matrix rows: no name, the value on the right
            lines.append('slider_h cx=%d cy=%d w=%d h=%d label="%s" key=%s ns=0 vs=37 bw=%d %s%s' % (x, y, sw, sh, lab, key, sw, look, w))
        else:
            lines.append('slider_h cx=%d cy=%d w=%d h=%d label="%s" key=%s ns=36 vs=37 bw=%d %s%s' % (x, y, sw, sh, lab, key, max(sw, 200), look, w))
    elif k == "slider_v":
        lines.append('slider_v cx=%d cy=%d w=%d h=%d label="%s" key=%s ns=0 vs=1 bw=%d%s' % (x, y, it.get("w", 34), it.get("h", 350), lab, key, it.get("w", 34) + 2, w))
    elif k in ("tile", "button"):
        # ONE widget: the button draws its own caption (get= + tsize=) over the key picture and keeps a transparent hit plate on top, so a tap fires the trigger.
        bw, bh = int(it.get("w", 150)), int(it.get("h", 60))
        lines.append('button cx=%d cy=%d w=%d h=%d label="" key=%s get=%s tsize=26 tweight=700 tcolor=%s talign=center img=%s img_on=%s%s' %
                     (x, y, bw, bh, key, cap_key(it["label"]), KEY_INK[col], tile_art(col, bw, bh, False), tile_art(col, bw, bh, True), w))
    elif k == "caption":
        lines.append('readout cx=%d cy=%d w=%d h=%d label="" key=%s box=0 tsize=28 tweight=700 talign=center%s' %
                     (x, y, it.get("w", 240), it.get("h", 36), cap_key(it["label"]), w))
    elif k == "readout":
        extra = (" tcolor=" + it["tcolor"]) if it.get("tcolor") else ""
        lines.append('readout cx=%d cy=%d w=%d h=%d label="" key=%s box=0 tsize=%d tweight=700 talign=%s%s%s' %
                     (x, y, it.get("w", 800), it.get("h", 50), key, it.get("tsize", 34), it.get("talign", "left"), extra, w))
    elif k == "list":
        tw = (int(it["w"]) - (it["cols"] - 1) * it["gap"]) // it["cols"]
        lines.append('list x=%d y=%d w=%d h=%d cols=%d rows=%d th=%d gap=%d key=%s%s img=%s img_on=%s tsize=26 tweight=700 tcolor=141414 talign=left%s' %
                     (int(it["x"]), int(it["y"]) + Y_OFF, it["w"], it["rows"] * it["th"] + (it["rows"] - 1) * it["gap"], it["cols"], it["rows"], it["th"], it["gap"], key,
                      (" order=" + it["order"]) if it.get("order") else "", tile_art("grey", tw, it["th"], False), tile_art("green", tw, it["th"], False), w))


def make_conf(layout, key_index, texts):
    names = {}

    def name_of(key):
        return names.get(key, key)
    pjson = json.load(open(os.path.join(ROOT, "params.json")))
    for p in pjson["params"]:
        names[p["key"]] = p["name"]
    L = [THEME]
    for it in layout["header"]:
        if not it.get("hidden"):
            emit(it, it.get("when"), name_of, L)
    for pg in layout["pages"]:
        pid = pg["id"]
        for it in pg["items"]:
            if it.get("hidden"):
                continue
            if "alt" in it:
                for mode, c in ((0, it), (1, it["alt"])):
                    cc = dict(c)
                    for kk in ("x", "y", "w", "h", "r", "cap_y"):
                        if kk in it and kk not in cc:
                            cc[kk] = it[kk]
                    emit(cc, "pm_%s_%s_%d:1" % (pid, it["alt_sel"].lower(), mode), name_of, L)
            else:
                emit(it, "pg_%s:1" % pid, name_of, L)
    L.append('qlinks "MANTRA" = ' + ",".join("ql%d" % n for n in range(1, 17)))
    return "\n".join(L) + "\n"


# ------------------------------------------------------------------------------------------------ docs
def write_docs(layout, P, meta, key_index, qtab):
    lines = ["# MANTRA control map (generated)", "", "One row per control. Positions are the panel editor's (page px, 1280 x 628, header 70 px).", "",
             "| page | key | kind | label | x | y | w | h | r | Q-Link |", "|---|---|---|---|---|---|---|---|---|---|"]
    csv = ["page,key,kind,label,x,y,w,h,r"]
    for i, pg in enumerate(layout["pages"]):
        ql = {}
        for n, (a, b, sel) in enumerate(qtab[i][0]):
            if a >= 0:
                ql[P[a]["key"]] = n + 1
        for it in pg["items"]:
            q = ql.get(it["key"], "")
            lines.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (pg["id"], it["key"], it["kind"], it.get("label", ""),
                                                                              it.get("x", ""), it.get("y", ""), it.get("w", ""), it.get("h", ""), it.get("r", ""), q))
            csv.append("%s,%s,%s,\"%s\",%s,%s,%s,%s,%s" % (pg["id"], it["key"], it["kind"], it.get("label", ""), it.get("x", ""), it.get("y", ""),
                                                          it.get("w", ""), it.get("h", ""), it.get("r", "")))
    write("docs/layout_map.md", "\n".join(lines) + "\n")
    write("docs/layout_map.csv", "\n".join(csv) + "\n")
    inv = ["# MANTRA parameter inventory (generated)", "", "%d parameters, in VST index order (append-only once released)." % len(P), "",
           "| # | key | name | kind |", "|---|---|---|---|"]
    kn = ["cont", "enum", "int", "step", "host", "view", "trig", "title", "cap", "ql", "flag", "hidden", "bank", "row", "sel"]
    for i, (p, m) in enumerate(zip(P, meta)):
        inv.append("| %d | %s | %s | %s |" % (i, p["key"], p["name"], kn[m["kind"]]))
    write("docs/INVENTORY.md", "\n".join(inv) + "\n")


if __name__ == "__main__":
    main()
