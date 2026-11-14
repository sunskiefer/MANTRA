#!/usr/bin/env python3
"""Pattern extractor: reads DS Tantra 2 plug-in presets (.prg) and modulator presets (.mprg) and writes tools/t2_patterns.json,
the pattern library the MANTRA factory presets are built from (tools/presets.py).

  python3 tools/t2_extract.py "<...>/DS Tantra 2/Plug-In Presets" "<...>/DS Tantra 2/Mod Presets"

What is read (only the modulation curves; the parameter ids in a .prg are hashes, so the rest is not used):
  .prg  = 'VC2!' + zlib stream; an XML part (parameters, blocks); blocks are JUCE-base64 ('<size>.<6-bit chars>', LSB first).
          8 gate blocks (256 B = 32 x (x,y) float pairs) and 8 sequence blocks (768 B = 64 x (x,y,c) float triples, x < 0 = unused).
  .mprg = 12 byte header (magic c3 67 ed 34, type 0 gate / 1 sequence, 32), then the same data (268 B gate, 780 B sequence).
Output: {"presets":[{"cat":"SEQ","gates":[[16 values]..],"seqs":[{"n":8,"v":[..]}..]}..], "mod_gates":[..], "mod_seqs":[..]}
"""
import glob, json, os, re, struct, sys, zlib

ALPHA = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def b64(s):
    m = re.match(rb"(\d+)\.(.*)", s, re.S)
    n = int(m.group(1)); data = m.group(2).decode("latin1")
    out = bytearray(n); bits = 0
    for ch in data:
        v = ALPHA.index(ch)
        for k in range(6):
            if bits // 8 >= n:
                break
            if (v >> k) & 1:
                out[bits // 8] |= 1 << (bits % 8)
            bits += 1
    return bytes(out)


def gate_steps(f):
    """f: 64 floats = 32 (x,y). The gate runs 16 steps; step k is the level at pair 2k (pair 2k+1 is the off gap)."""
    ys = [f[2 * i + 1] for i in range(32)]
    v = [round(max(0.0, min(1.0, ys[2 * k])), 2) for k in range(16)]
    return v if (any(x > 0 for x in v) and any(x < 1 for x in v)) else None


def seq_steps(f):
    pts = [(f[i], f[i + 1]) for i in range(0, len(f) - 2, 3) if f[i] >= 0]
    if len(pts) <= 3 and (len(pts) < 3 or pts[1][1] == 1.0):
        return None                       # the factory default saw
    span = max(p[0] for p in pts)
    if span <= 0:
        return None
    n = 2
    for c in (2, 4, 8, 16, 32):
        n = c
        if span <= c + 1e-3:
            break
    idx = sorted(range(len(pts)), key=lambda i: (pts[i][0], i))
    xs = [pts[i][0] for i in idx]; ysv = [pts[i][1] for i in idx]
    out = []
    for k in range(n):
        p = (k + 0.5) * span / n
        j = max([i for i in range(len(xs)) if xs[i] <= p] or [0])
        if j + 1 < len(xs) and xs[j + 1] > xs[j]:
            t = (p - xs[j]) / (xs[j + 1] - xs[j]); y = ysv[j] + (ysv[j + 1] - ysv[j]) * t
        else:
            y = ysv[j]
        out.append(round(max(0.0, min(1.0, y)), 2))
    if max(out) - min(out) < 0.05:
        return None
    return {"n": n, "v": out}


def main(prg_dir, mprg_dir):
    lib = {"presets": [], "mod_gates": [], "mod_seqs": []}
    seen_g, seen_s = set(), set()
    for f in sorted(glob.glob(os.path.join(prg_dir, "**", "*.prg"), recursive=True)):
        name = os.path.basename(f)[:-4]
        cat = name.split(" ")[0].upper()
        cat = cat[3:] if cat.startswith("SUN") else cat
        d = zlib.decompress(open(f, "rb").read())
        i = d.find(b"<blocks>")
        gates, seqs = [], []
        for _id, size, data in re.findall(rb'<block id="([0-9a-f]+)" size="(\d+)" data="(.*?)"/>', d[i:], re.S):
            b = b64(size + b"." + data[data.index(b".") + 1:])
            if len(b) == 256:
                g = gate_steps(struct.unpack("<64f", b)); gates.append(g)
            elif len(b) == 768:
                s = seq_steps(struct.unpack("<192f", b)); seqs.append(s)
        gates = [g for g in gates if g]; seqs = [s for s in seqs if s]
        if gates or seqs:
            lib["presets"].append({"cat": cat, "gates": gates, "seqs": seqs})
    for f in sorted(glob.glob(os.path.join(mprg_dir, "Gates", "*.mprg"))):
        b = open(f, "rb").read(); g = gate_steps(struct.unpack("<64f", b[12:12 + 256]))
        if g and tuple(g) not in seen_g:
            seen_g.add(tuple(g)); lib["mod_gates"].append(g)
    for f in sorted(glob.glob(os.path.join(mprg_dir, "Sequences", "*.mprg"))):
        b = open(f, "rb").read(); s = seq_steps(struct.unpack("<192f", b[12:12 + 768]))
        if s and tuple(s["v"]) not in seen_s:
            seen_s.add(tuple(s["v"])); lib["mod_seqs"].append(s)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "t2_patterns.json")
    json.dump(lib, open(out, "w"), separators=(",", ":"))
    print("presets %d  gates %d  seqs %d  | mod presets: gates %d seqs %d -> %s" % (
        len(lib["presets"]), sum(len(p["gates"]) for p in lib["presets"]), sum(len(p["seqs"]) for p in lib["presets"]),
        len(lib["mod_gates"]), len(lib["mod_seqs"]), out))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
