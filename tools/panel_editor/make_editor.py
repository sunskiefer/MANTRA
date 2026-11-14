#!/usr/bin/env python3
"""Builds panel_editor/MANTRA-panel-editor.html (one self-contained page, no network) from art/layout_default.json,
art/addable_keys.json, params.json and the current art/userlayout.json (if any).   Run after tools/gen_force.py."""
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
art = lambda n: os.path.join(ROOT, "art", n)
default = json.load(open(art("layout_default.json")))
addable = json.load(open(art("addable_keys.json")))
names = {p["key"]: p["name"] for p in json.load(open(os.path.join(ROOT, "params.json")))["params"]}
initial = json.load(open(art("userlayout.json"))) if os.path.exists(art("userlayout.json")) else None
html = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "editor_template.html")).read()
for k, v in (("__DEFAULT__", default), ("__NAMES__", names), ("__ADDABLE__", addable), ("__INITIAL__", initial)):
    html = html.replace(k, json.dumps(v, separators=(",", ":")).replace("</", "<\\/"))
out = os.path.join(ROOT, "panel_editor", "MANTRA-panel-editor.html")
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, "w", encoding="utf-8").write(html)
print("panel_editor/MANTRA-panel-editor.html  %d KB" % (len(html) // 1024))
