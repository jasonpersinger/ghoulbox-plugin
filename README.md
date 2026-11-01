# GHOULBOX plugin

GHOULBOX as a CLAP / VST3 / LV2 DAW instrument (v0.2.0). GPL-3.0-only. Source: https://github.com/jasonpersinger/ghoulbox-plugin. Releases before 1.0 are pre-releases.
![The GHOULBOX plugin window: the sound browser, the live firmware screen, the four sound slots and the master and FX controls](docs/screenshot.png)

## What the plugin brings to your DAW

The [GHOULBOX firmware](https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth) for the M-Vave FM-1, as an
instrument you play from a MIDI track. No FM-1 needed. On top of the firmware's sounds:

- **The firmware itself, not a copy.** The plugin runs GHOULBOX 2.3.2's own audio code, built for the desktop; a
  test checks its output byte for byte against the firmware's. HALL reverb, TAPE and CRSH, the GURDY hurdy-gurdy
  engine, the medieval tunings, the dungeon scales and the real recordings all sound as they do on the device.
- **Four layered sound slots.** Each note plays every enabled slot at once, so one keyboard plays a stack of up
  to four sounds: a choir over a drone over a harp. Slot 1 is on when you insert it; turn the others on with
  their ENABLED/OFF buttons. Each slot has its own volume fader.
- **A sound browser.** All 59 sounds in a list: click one to load it into the selected slot.
- **The live FM-1 screen.** The device's own display, torches and all, with its buttons and encoders beside it.
  Every page works: FX, SCL, ENV, LFO, EDIT, ARP, HOME and SAVE. Scroll or drag a knob to turn it; right-click a
  button to hold it down for combinations.
- **Play from the computer keyboard.** Click the screen, then Z–/, S–' and Q–O play notes, 1–0 press the
  buttons, [ and ] shift the octave, and the arrow keys turn SELECT and ALGO.
- **Automation.** Master volume, each slot's on/off, level, pan, reverb and delay sends and its four engine
  knobs, the reverb type (ROOM, SPRING, HALL) and TAPE are parameters your DAW can automate.
- **It fits the DAW.** It follows your project's tempo, saves its sounds and settings with your project, and
  stays silent until it gets MIDI: no built-in sequencer or autonomous playback. Sounds you SAVE go to a
  library shared by every GHOULBOX on your computer (`~/.local/share/GHOULBOX`).
- **Tooltips.** Hover anything for what it does; switch them off at the top of the window.

CLAP, VST3 and LV2 for 64-bit Linux (glibc 2.34 or newer, X11 or XWayland). Tested in REAPER. A beta: expect
rough edges, and [open an issue](https://github.com/jasonpersinger/ghoulbox-plugin/issues/new) if you find one.

## Install

Download the zip from [Releases](https://github.com/jasonpersinger/ghoulbox-plugin/releases), unpack it, and
run `./install.sh` in that folder. Rescan plugins in your DAW and insert **GHOULBOX (MESSYQUEST)**. The zip's
README has the details and the known issues (MIDI timing and latency).

## Pins
- `external/Felucca`: https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth.git at tag `ghoulbox-v2.3.2` (71ac3f8). Never edited.
- `external/DPF`: https://github.com/DISTRHO/DPF.git at `main` 4238e1c (2025-10-23). DPF publishes no release tags.
- `external/DPF/dgl/src/pugl-upstream` (DPF's own submodule, for the window): https://github.com/DISTRHO/pugl.git at
  5e2621d, the commit DPF records.
- Fonts, compiled into the window: VT323 and Pirata One from `external/Felucca/assets/fonts/`, both under the SIL Open
  Font License (`external/Felucca/LICENSES/OFL-VT323.txt`, `OFL-PirataOne.txt`). The OFL asks that their licence
  travel with any distributed binary; the release zip ships both in `LICENSES/`.

## Build
    git submodule update --init external/Felucca external/DPF
    git -C external/DPF submodule update --init dgl/src/pugl-upstream
    make gen     # the firmware's generated headers (python3 tools/build.py --gen-only)
    make core    # build/libghoulbox_core.so
    make test
    make install # CLAP, VST3, LV2 into ~/.clap ~/.vst3 ~/.lv2
    make gui-snapshot   # by hand, with an X display: the window in a bare host, pictured into build/gui-snapshot.png
