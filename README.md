# MANTRA

**AUDIO MANGLER MULTIFX** for the Akai Force and MPC (MPC OS Gen1, ARMv7). A rhythmic multi-effect: you draw a pattern, and the pattern chops, filters, crushes, delays and glitches whatever you send through it, in time with the song.

Free, made by L'Cronx. Not for sale (see Licence).

> Status: 0.2.0. Built and checked offline; **not yet run on a real Force** (see `TESTING.md`). The step-fader touch and the data wheel on the preset list especially need a device test.

## What it does

Two effect layers, **A** and **B**. Each layer has six effects: **Filter, Distortion, Delay, Lo-Fi, Flanger, Glitch**. Per layer you choose the order of the six (six slot menus), bypass any of them, set each one's mix, and run the two layers **in series** or **in parallel**.

Eight **modulators** (M1-M8) are 32-step sequencers. Each can play as stepped levels or as a smooth curve, with step count 2-32, gate, smoothing, shuffle, tension, shift, invert, and a speed that is either free (Hz) or a note division. A modulator can also act as a **tremolo** or a **pan flip** on a layer.

A 24-slot **matrix** sends any modulator to any of 84 destinations (cutoffs, mixes, delay times, feedback, glitch size, reverb and so on) with a bipolar depth. The master section has a 3-band EQ, a reverb, output volume / pan / mix and a soft clipper.

**Tempo lock.** With *Host Sync* on (default), synced modulators lock to the song position the Force reports: the pattern restarts on the bar, jumps with the playhead, and follows tempo changes. Delay and flanger divisions follow the host tempo as well. With the transport stopped, or with Host Sync off, everything free-runs from the BPM control.

**Presets.** 419 factory presets (all with several modulators and a dozen routings) sorted into ten styles (Basic, Gates, Filter Seq, Glitch, Movement, Delays, Lo-Fi, Space, Layers, Destruction). The **PRESETS** page lists the styles on the left and the presets of the chosen style on the right (paged); tap one to load it. **SAVE** overwrites your own current preset, **SAVE NEW** makes "User 001", "User 002" ... with no limit, **DELETE** (tap twice) removes one of your own. They live in MY PRESETS and as files in the plugin's `presets/user/` folder, so you can back them up or share them. Q-Link 1 picks the style and Q-Link 2 the preset.

**Randomiser.** Every modulator page, steps page and matrix page has a small **RND** button and an amount menu (10 / 30 / 50 / 70 / 90 / 100 %). It randomises that page's own parameters by that amount, every time differently; there is no undo (save a preset first if you like what you have).

**Step faders.** On a steps page, touch a fader and it jumps to the height you touched; drag a finger across several to draw a shape.

## Using it on the Force

The plugin starts on the **overview**: a tile for every page. Tap a tile to open that page. In the header, **OVERVIEW** goes back and **<** **>** step through every page in order; the title of the page is shown at the top.

Pages (38): overview; Layer A and Layer B with Mix, Filter, Distortion, Delay, Lo-Fi, Flanger, Glitch each; Modulator 1-8 (shape) and 1-8 steps; Matrix slots 1-6, 7-12, 13-18, 19-24; Master EQ + Reverb; Master Output; Presets.

**Q-Links** follow the page: they always turn the controls of the page on screen, and their little screens show the name and value. On a Delay or Flanger or Modulator page the first control is a *pair* (free time / synced division); the Q-Link and the touch control both switch with the Sync switch. On a step page the 16 Q-Links edit steps 1-16; the **Q 17-32** switch moves them to steps 17-32.

## Arranging the panel (your layout)

Open `panel_editor/MANTRA-panel-editor.html` in any browser (no install, no network). Drag controls, resize, align, add or hide controls, give a control another **colour** (red, green, blue, brown, black, grey), swap a knob for a **fader** or back, edit tile text, then **Save layout** and put the file in `art/userlayout.json`, then run `./build.sh`. Controls you did not touch keep the default places. Red outlines show touch boxes that overlap or leave the screen. Parameter order never changes when you edit the layout, so saved projects keep working.

## Building from source

Needs Python 3 with Pillow, Zig (`pip install ziglang`) and, for the skin, Playwright with Chromium (the browser renderer). No Docker.

```
./build.sh          # -> build/package/L'Cronx - VST - MANTRA/   (skin + mantra.so, armv7 hard-float, glibc <= 2.31)
```

`tools/model.py` is the single description of the parameters, pages and default layout; `tools/gen_force.py` writes `params.json`, `layout.conf`, `presets.json`, `src/param_ids.h` and the docs from it. The sound engine is `src/mantra_*.h`; `src/engine.cc` is the MPC glue. `third_party/mpc-vst-plugins` is a pinned copy of the plugin wrapper and skin tools (MIT, see its `LICENSE`; commit in `PINNED_COMMIT.txt`).

## Licence

MANTRA's own code and art: **PolyForm Noncommercial License 1.0.0** (see `LICENSE`): source is open, free to use, not for sale. Bundled third-party parts keep their own licences: the wrapper and skin tools in `third_party/` (MIT, sd88me and contributors) and the Titillium Web font (SIL OFL 1.1, `art/fonts/OFL.txt`).

MANTRA is an independent design. It is not made by, endorsed by or derived from DS Audio or its products.
