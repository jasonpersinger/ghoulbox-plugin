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
         LICENSES/CC0-samples-ATTRIBUTION.txt LICENSES/MIT-CLAP.txt LICENSES/ISC-LV2.txt LICENSES/DejaVuSans-LICENSE.txt; do
    [ -e "$R/$f" ] || fail "missing $f"
done
[ -x "$R/install.sh" ] || fail "install.sh is not executable"
grep -q "support@messy.quest" "$R/README.txt" || fail "README has no support address"
grep -q "Alexandre BIQUE" "$R/LICENSES/MIT-CLAP.txt" || fail "the CLAP licence is not CLAP's"
grep -q "Steve Harris, David Robillard" "$R/LICENSES/ISC-LV2.txt" || fail "the LV2 licence is not LV2's"
grep -q "Bitstream" "$R/LICENSES/DejaVuSans-LICENSE.txt" || fail "the DejaVu licence is not DejaVu's"
# every binary runs on glibc 2.34 (Ubuntu 22.04, Debian 12) or newer
for b in $(find $R -name '*.so' -o -name '*.clap'); do
    top=$(objdump -T "$b" | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sed 's/GLIBC_//' | sort -V | tail -1)
    [ "$(printf '%s\n2.34\n' "$top" | sort -V | tail -1)" = "2.34" ] || fail "$b needs glibc $top"
done
# GB-004: and on their C++ runtime: none needed (libstdc++ is linked in), libgcc_s no newer than GCC 7's
# (Ubuntu 22.04 / Debian 12 ship GCC 12's, Fedora 35 GCC 11's); every exported symbol is the format's entry point
ceiling() {   # file, version prefix, highest allowed
    top=$(objdump -T "$1" | grep -oE "$2_[0-9]+(\.[0-9]+)*" | sed "s/$2_//" | sort -V | tail -1)
    [ -z "$top" ] || [ "$(printf '%s\n%s\n' "$top" "$3" | sort -V | tail -1)" = "$3" ] || fail "$1 needs $2_$top"
}
for b in $(find $R -name '*.so' -o -name '*.clap'); do
    readelf -d "$b" | grep -q 'NEEDED.*libstdc++' && fail "$b needs the system's libstdc++"
    ceiling "$b" GLIBCXX 3.4.29
    ceiling "$b" CXXABI 1.3.13
    ceiling "$b" GCC 7.0.0
done
[ "$(nm -D --defined-only $R/CLAP/ghoulbox.clap | awk '{print $3}')" = "clap_entry" ] || fail "the CLAP exports more"
[ "$(nm -D --defined-only $R/VST3/ghoulbox.vst3/Contents/x86_64-linux/ghoulbox.so | awk '{print $3}' | tr '\n' ' ')" = \
  "GetPluginFactory ModuleEntry ModuleExit " ] || fail "the VST3 exports more"
[ "$(nm -D --defined-only $R/LV2/ghoulbox.lv2/ghoulbox.so | awk '{print $3}' | tr '\n' ' ')" = \
  "lv2_descriptor lv2_generate_ttl lv2ui_descriptor " ] || fail "the LV2 exports more"
# the source: complete (the submodules' files too), no git metadata
S=$(tar -tzf "$R/ghoulbox-$V-source.tar.gz")
for f in Makefile core/ghoulbox_core.c plugin/GhoulboxUI.cpp external/Felucca/firmware/src/main.c \
         external/Felucca/tools/build.py external/DPF/Makefile.plugins.mk external/DPF/dgl/src/pugl-upstream/src/x11.c \
         external/speex/resample.c; do
    echo "$S" | grep -qx "ghoulbox-$V-source/$f" || fail "the source lacks $f"
done
echo "$S" | grep -q '/\.git\(/\|$\)' && fail "the source carries .git"
echo "PASS: dist ($Z)"
