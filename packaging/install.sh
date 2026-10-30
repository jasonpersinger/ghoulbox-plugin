#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# GHOULBOX for Linux: installs the CLAP, VST3 and LV2 versions into your own plugin folders (no sudo).
#   ./install.sh               install (or update)
#   ./install.sh --uninstall   remove
set -e
cd "$(dirname "$0")"
if [ "$1" = "--uninstall" ]; then
    rm -f "$HOME/.clap/ghoulbox.clap" "$HOME/.clap/libghoulbox_core.so"
    rm -rf "$HOME/.vst3/ghoulbox.vst3" "$HOME/.lv2/ghoulbox.lv2"
    echo "GHOULBOX removed. (Your saved projects stay in ~/.local/share/GHOULBOX.)"
    exit 0
fi
mkdir -p "$HOME/.clap" "$HOME/.vst3" "$HOME/.lv2"
cp CLAP/ghoulbox.clap CLAP/libghoulbox_core.so "$HOME/.clap/"
rm -rf "$HOME/.vst3/ghoulbox.vst3" "$HOME/.lv2/ghoulbox.lv2"
cp -r VST3/ghoulbox.vst3 "$HOME/.vst3/"
cp -r LV2/ghoulbox.lv2 "$HOME/.lv2/"
echo "GHOULBOX installed: ~/.clap, ~/.vst3, ~/.lv2. Rescan plugins in your DAW."
