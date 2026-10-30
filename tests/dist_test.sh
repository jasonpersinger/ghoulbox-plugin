#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The release package (make dist): its layout, the oldest Linux it runs on, its licences, its source.
set -e
cd "$(dirname "$0")/.."
V=$(sed -n 's/^VERSION := //p' Makefile)
Z=build/dist/ghoulbox-$V-linux-x86_64.zip
D=build/dist-check
fail() { echo "FAIL: $1"; exit 1; }
[ -f "$Z" ] || fail "no $Z"
rm -rf $D && mkdir -p $D && unzip -q "$Z" -d $D
R=$D/ghoulbox-$V-linux-x86_64
for f in CLAP/ghoulbox.clap CLAP/libghoulbox_core.so \
         VST3/ghoulbox.vst3/Contents/x86_64-linux/ghoulbox.so VST3/ghoulbox.vst3/Contents/x86_64-linux/libghoulbox_core.so \
         LV2/ghoulbox.lv2/ghoulbox.so LV2/ghoulbox.lv2/manifest.ttl LV2/ghoulbox.lv2/libghoulbox_core.so \
         install.sh README.txt ghoulbox-$V-source.tar.gz \
         LICENSES/GPL-3.0.txt LICENSES/OFL-VT323.txt LICENSES/OFL-PirataOne.txt LICENSES/MIT-Fukiai.txt \
         LICENSES/MIT-DaisySP.txt LICENSES/MIT-Rings.txt LICENSES/Apache-2.0-msfa.txt LICENSES/ISC-DPF.txt \
         LICENSES/ISC-pugl.txt LICENSES/Zlib-NanoVG.txt LICENSES/MIT-libsofd.txt LICENSES/BSD-speexdsp.txt \
         LICENSES/CC0-samples-ATTRIBUTION.txt; do
    [ -e "$R/$f" ] || fail "missing $f"
done
[ -x "$R/install.sh" ] || fail "install.sh is not executable"
grep -q "support@messy.quest" "$R/README.txt" || fail "README has no support address"
# every binary runs on glibc 2.34 (Ubuntu 22.04, Debian 12) or newer
for b in $(find $R -name '*.so' -o -name '*.clap'); do
    top=$(objdump -T "$b" | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sed 's/GLIBC_//' | sort -V | tail -1)
    [ "$(printf '%s\n2.34\n' "$top" | sort -V | tail -1)" = "2.34" ] || fail "$b needs glibc $top"
done
# the source: complete (the submodules' files too), no git metadata
S=$(tar -tzf "$R/ghoulbox-$V-source.tar.gz")
for f in Makefile core/ghoulbox_core.c plugin/GhoulboxUI.cpp external/Felucca/firmware/src/main.c \
         external/Felucca/tools/build.py external/DPF/Makefile.plugins.mk external/DPF/dgl/src/pugl-upstream/src/x11.c \
         external/speex/resample.c; do
    echo "$S" | grep -qx "ghoulbox-$V-source/$f" || fail "the source lacks $f"
done
echo "$S" | grep -q '/\.git\(/\|$\)' && fail "the source carries .git"
echo "PASS: dist ($Z)"
