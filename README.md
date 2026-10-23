# GHOULBOX plugin (private until 1.0)

GHOULBOX as a CLAP / VST3 / LV2 instrument. GPL-3.0-only. **Local git only, no remote, until the 1.0 release.**

Design: `~/projects/ghoulbox-plugin-design/2026-10-22-ghoulbox-plugin-design.md`. Plans: `~/projects/ghoulbox-plugin-design/plans/`.

## Pins
- `external/Felucca`: https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth.git at tag `ghoulbox-v2.3.2` (71ac3f8). Never edited.
- `external/DPF`: https://github.com/DISTRHO/DPF.git at `main` 4238e1c (2025-10-23). DPF publishes no release tags.

## Build
    git submodule update --init external/Felucca external/DPF
    make gen     # the firmware's generated headers (python3 tools/build.py --gen-only)
    make core    # build/libghoulbox_core.so
    make test
