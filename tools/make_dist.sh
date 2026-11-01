#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The release package: build/dist/ghoulbox-VERSION-linux-x86_64.zip, the plugin as built (make plugin), with its
# installer, README, licences and its complete source (GPL-3.0: the source of exactly these binaries, so only from a
# committed tree).
set -e
cd "$(dirname "$0")/.."
V=$1
[ -n "$V" ] || { echo "usage: make_dist.sh VERSION"; exit 1; }
if [ -n "$(git status --porcelain --ignore-submodules=untracked)" ]; then
    echo "dist: commit your changes first (the source archive must match the binaries)"
    exit 1
fi
N=ghoulbox-$V-linux-x86_64
SRC=ghoulbox-$V-source
OUT=build/dist
ST=$OUT/$N
FEL=external/Felucca
rm -rf $OUT
mkdir -p $ST/CLAP $ST/VST3 $ST/LV2 $ST/LICENSES

cp build/plugin/ghoulbox.clap build/plugin/libghoulbox_core.so $ST/CLAP/
cp -r build/plugin/ghoulbox.vst3 $ST/VST3/
cp -r build/plugin/ghoulbox.lv2 $ST/LV2/
cp packaging/install.sh $ST/ && chmod 755 $ST/install.sh
sed "s/@VERSION@/$V/g" packaging/README.txt > $ST/README.txt

L=$ST/LICENSES
cp $FEL/LICENSE $L/GPL-3.0.txt
cp $FEL/LICENSING.md $L/Felucca-LICENSING.md
for f in OFL-VT323 OFL-PirataOne MIT-Fukiai MIT-DaisySP MIT-Rings Apache-2.0-msfa; do cp $FEL/LICENSES/$f.txt $L/; done
cp $FEL/assets/samples-cc0/ATTRIBUTION.txt $L/CC0-samples-ATTRIBUTION.txt
cp external/DPF/LICENSE $L/ISC-DPF.txt
cp external/DPF/dgl/src/pugl-upstream/COPYING $L/ISC-pugl.txt
cp external/DPF/dgl/src/nanovg/LICENSE.txt $L/Zlib-NanoVG.txt
cp external/speex/COPYING $L/BSD-speexdsp.txt
sed -n '1,/\*\//p' external/DPF/distrho/extra/sofd/libsofd.c > $L/MIT-libsofd.txt
cp packaging/licenses/MIT-CLAP.txt $L/MIT-CLAP.txt                 # CLAP 1.1.1's headers, in DPF (GB-019)
sed -n '1,/\*\//p' external/DPF/distrho/src/lv2/lv2.h > $L/ISC-LV2.txt  # LV2's headers, in DPF
cp external/DPF/dgl/src/resources/LICENSE-DejaVuSans.ttf.txt $L/DejaVuSans-LICENSE.txt   # DPF's built-in UI font

# the source: this repo and its submodules at their commits, without git metadata
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
git archive --prefix=$SRC/ HEAD | tar -x -C "$T"
for sm in external/Felucca external/DPF external/DPF/dgl/src/pugl-upstream; do
    git -C $sm archive --prefix=$SRC/$sm/ HEAD | tar -x -C "$T"
done
tar -czf $ST/$SRC.tar.gz -C "$T" --sort=name --owner=0 --group=0 --numeric-owner $SRC

python3 -I - "$OUT" "$N" <<'PY'
import os, sys, zipfile
out, name = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(os.path.join(out, name + ".zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(os.path.join(out, name)):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, out))           # (keeps each file's mode: install.sh stays executable)
PY
echo "dist: $OUT/$N.zip"
