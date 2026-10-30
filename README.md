# GHOULBOX plugin (private until 1.0)

GHOULBOX as a CLAP / VST3 / LV2 instrument. GPL-3.0-only. **Local git only, no remote, until the 1.0 release.**

Design: `~/projects/ghoulbox-plugin-design/2026-10-22-ghoulbox-plugin-design.md`. Plans: `~/projects/ghoulbox-plugin-design/plans/`.

## Pins
- `external/Felucca`: https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth.git at tag `ghoulbox-v2.3.2` (71ac3f8). Never edited.
- `external/DPF`: https://github.com/DISTRHO/DPF.git at `main` 4238e1c (2025-10-23). DPF publishes no release tags.
- `external/DPF/dgl/src/pugl-upstream` (DPF's own submodule, for the window): https://github.com/DISTRHO/pugl.git at
  5e2621d, the commit DPF records.
- Fonts, compiled into the window: VT323 and Pirata One from `external/Felucca/assets/fonts/`, both under the SIL Open
  Font License (`external/Felucca/LICENSES/OFL-VT323.txt`, `OFL-PirataOne.txt`). The OFL asks that their licence
  travel with any distributed binary: Plan 3's packaging must ship those two files.

## Build
    git submodule update --init external/Felucca external/DPF
    git -C external/DPF submodule update --init dgl/src/pugl-upstream
    make gen     # the firmware's generated headers (python3 tools/build.py --gen-only)
    make core    # build/libghoulbox_core.so
    make test
    make install # CLAP, VST3, LV2 into ~/.clap ~/.vst3 ~/.lv2
    make gui-snapshot   # by hand, with an X display: the window in a bare host, pictured into build/gui-snapshot.png
