# Changelog

## 0.2.0 (not yet run on a real Force; rebuilt 2026-11-14: flat look, more modulation)
New look, faders, buttons, randomiser, a real preset manager and touch-to-value on the step faders. One release; the parameter list was extended and **re-ordered once** (projects saved with 0.1.x may not restore correctly in 0.2.0). From here on the order is locked (append only).
- **Flat dark panel**, like the panel editor: near-black pages, flat coloured tiles (red, green, blue, brown, black, grey) with a small shadow, flat preset lists; drawn by the browser renderer. Text keeps the Meta4MPCXXL sizes (knob and fader names 35.7 px, values 37.4 px, tiles and lists 26 px bold).
- **Knobs** are your render (`knob_render.py`): black scalloped ring, one 128-frame strip per colour (red, green, blue, brown, black, grey), turning in place. Each sits on a lit bezel with a soft shadow, so it is not just lying on the panel.
- **Faders** use the CSXXL caps and dark slots (name 36 px, value 37 px), and replace the knobs where a fader makes more sense: Mix, Level, Volume, Depth, Amount, Feedback and the EQ gains. Frequencies, times, rates and shapes stay knobs. The 256 step faders keep their look (dark well, fill, grey thumb); the fill is the modulator colour.
- **Buttons**: your key pictures, six colours, LED off / lit, for every switch and trigger. Overview tiles, the header buttons (OVERVIEW, <, >) and the preset lists are flat tiles like the editor's.
- **Colours** (never yellow): Modulators green, Matrix blue, Master black; the effect pages follow their control colours. One layout file sets it; the panel editor changes it per control.
- **Randomiser**: a small RND button and an amount menu (10 / 30 / 50 / 70 / 90 / 100 %) on every modulator page, every steps page and every matrix page, per page. Each press gives a new result for every parameter of that page: continuous values move up or down by at most the chosen share of their range (100 % = anywhere), choices change to a random option with that chance. The RND button and amount, and the matrix bypass switches, are left alone. No undo.
- **Preset manager** (page 37, PRESETS): a list of 10 styles plus MY PRESETS, a paged list of the presets of the style, SAVE (overwrite your own), SAVE NEW (auto-numbered "User 001", no limit), DELETE (your own only, tap twice). 419 factory presets are sorted into the styles. User presets are plain files in `<plugin folder>/presets/user/`. Q-Links 1 / 2 pick the style and the preset. File access runs on a worker thread, never in the audio call.
- **Step faders jump to the touch**: touching a step fader sets the height you touched, and a finger drawn across the faders sets each one it crosses (draw a curve). The plugin reads the touchscreen itself while a steps page is on screen. Needs the Force; see TESTING.md. A file `touch.off` in the `presets` folder disables it; `touch.log` there says what it saw.
- **More modulation in the presets**: every factory preset now runs 5-7 modulators and 10-18 routings (before: 1-3 modulators and 0-3 routings). The 107 original presets keep their names and sound and get extra modulators and routings; 312 new presets are built on the modulator patterns of the DS Tantra 2 factory presets (their 8 gate and 8 sequence curves per preset, plus the Gates and Sequences modulator presets), with MANTRA's own effects, routings and names. `tools/t2_extract.py` reads the Tantra files, `tools/t2_patterns.json` is the pattern library, `tools/presets.py` builds the presets. Tantra's other parameters are stored under hashed ids and are not used.
- Panel editor: colour per control, knob <-> fader swap, preset lists and header items; flat tiles and the Meta4 text sizes like the panel.
- `tools/preview_pages.py`: page pictures with the live text (names, values, captions, lists) at the real sizes.
- Build: `html` skin renderer, `src/step_touch.cc`.
- Fix (2026-11-14, same 0.2.0): the list `<` `>` and SAVE / SAVE NEW / DELETE on the PRESETS page counted every write of one tap, so Next jumped from page 1 to 3 or 4 (and DELETE could fire its "tap again" step by itself). They now take one step per tap (250 ms guard, like the header `<` `>`). `tools/test/test_engine.cc` taps with a burst of writes on every style and checks one page per tap.

## 0.1.2
- Fix: tapping an overview tile (or the header OVERVIEW / < / > buttons) did nothing. The name was a separate text box laid over the button and took the tap. The button now draws its own name and the tap reaches it.

## 0.1.1 (superseded by 0.1.2)
- Tile and button names were amber on the amber tile (invisible); now dark.

## 0.1.0 (first Force build, not yet device-tested)
- Two layers x six effects (Filter, Distortion, Delay, Lo-Fi, Flanger, Glitch), any order, series or parallel.
- Eight 32-step modulators, 24-slot matrix over 84 destinations, master EQ / reverb / output.
- Modulators, delay and flanger lock to the Force's song position and tempo (Host Sync).
- 37 touch pages with an overview of tiles, OVERVIEW and < > in the header, Q-Links that follow the page.
- 107 factory presets.
- Panel editor (`panel_editor/MANTRA-panel-editor.html`) to arrange the panel before building.
