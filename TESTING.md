# Testing MANTRA

## Done offline (no Force involved), 0.2.0
Run on a PC build of the engine (a development check only, not what ships):
- all 869 parameters answer; every settable parameter round-trips at minimum, middle and maximum, and has display text
- 38 pages: exactly one page flag on per page, titles correct, Q-Links named on every page, Prev/Next debounce
- Q-Links follow the free / synced switch; step banks 1-16 and 17-32
- tempo lock: with the transport playing, a synced modulator's value matches the song position (6 positions, exact)
- randomiser: every page's RND button changes only that page's parameters, by at most the chosen amount (100 % anywhere), choices change with that chance, RND / amount / matrix bypass stay put
- preset manager: 419 factory presets over 10 styles, user presets saved / overwritten / deleted as files with auto-numbered names (301 of them over 19 pages), the file work on a worker thread
- step touch: the mailbox, the finger -> step / height mapping and the slide-draw interpolation (tools/test/test_step.cc)
- all 419 presets load and render 4 s of drums at 120 bpm: no NaN, no silence, nothing above full scale
- ARM build: `mantra.so` is 32-bit ARM v7 hard-float, needs only glibc 2.29, exports only `VSTPluginMain`
- skin build: no overlapping touch boxes, nothing off the screen (the checker's "Q-Link on a parameter no control shows" notes are expected: Q-Links are virtual and re-aimed per page, as in PhyXXL)

## NOT verified (needs a real Force)
Nothing has run on the device. In particular:
- how the new panel looks on the real screen (flat tiles, knob strips with the bezel, key pictures, step faders). The page pictures from `tools/preview_pages.py` show layout and text sizes only
- **step faders jumping to the touch**: the plugin finds MPC's page in the display picture (marker pixels in the background), reads the touchscreen from `/dev/input` and sets the steps itself. Written from CSXXL's XYZ pad recipe, not run. Does MPC's own slider drag fight with it? Look at `presets/touch.log` after touching a steps page. `presets/touch.off` disables it
- the data wheel on the preset lists
- memory and load time of the six 128-frame knob strips, and the size of the skin (about 33 MB)
- CPU load of the engine on the ARM CPU (target: `docs/BENCH.md`, 15 % average at most)
- the randomiser's meaning per page: RND on a modulator page randomises that page's settings, RND on a steps page its 32 steps

## Device checklist
| # | Check | Pass |
|---|---|---|
| 1 | Plugin appears in the plugin list with the MANTRA tile, loads as an effect without crashing | |
| 2 | Overview shows 28 tiles with readable text; tapping each opens its page | |
| 3 | OVERVIEW, < and > in the header work; page title text shows | |
| 4 | Knob / switch / menu text is large and readable on every page | |
| 5 | Sound passes through; Master > Mix and Volume behave | |
| 6 | Turning a Q-Link moves the knob on screen; touching a knob moves its Q-Link screen | |
| 7 | Delay page: Sync switch swaps free time and division, Q-Link 1 follows | |
| 8 | Modulator page + matrix: M1 to a filter cutoff is audible and moves with the beat | |
| 9 | Press play: modulator pattern restarts on bar 1; relocate the playhead: it stays in time | |
| 10 | Change project tempo: synced modulators and delay follow | |
| 11 | Step page: all 32 faders draw and edit; Q 17-32 switch moves the Q-Links | |
| 12 | PRESET menu lists 419 presets; picking one changes the sound and the screen | |
| 13 | Save a project, reopen: values, steps and matrix come back | |
| 14 | CPU stays under 15 % with both layers busy (BENCH.md) | |
| 15 | Data wheel changes the selected parameter | |
| 16 | New look: flat dark pages, flat coloured tiles, knobs with bezel and shadow, readable names on every page (knob names 35.7 px, values 37.4 px), six colours right (modulators green, matrix blue, master black) | |
| 17 | Knobs turn in place and show the colour of their page; faders (Mix, Level, Volume ...) show the CSXXL caps | |
| 18 | Buttons / switches show their LED: off, lit on tap, correct colour | |
| 19 | Step page: touching a fader jumps it to the touched height; dragging across faders draws a curve; the host's own drag does not fight it | |
| 20 | RND with each amount on a modulator, a steps and a matrix page changes only that page; different result each press | |
| 21 | PRESETS page: styles and presets list, load, SAVE NEW "User 001", SAVE overwrites, DELETE removes (tap twice); user presets survive a restart | |
| 22 | Q-Link 1 / 2 on the PRESETS page pick style and preset | |
| 23 | Skin loads in reasonable time; no memory trouble with 4 instances (419 presets in the .so) | |
| 24 | Load a few Tantra-pattern presets (e.g. from FILTER SEQ, GATES): several modulators move several things, nothing runs away or goes silent | |
