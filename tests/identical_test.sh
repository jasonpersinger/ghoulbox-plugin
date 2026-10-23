#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The core library renders byte-identically to the firmware's own native_check (same gestures, same samples).
set -e
cd "$(dirname "$0")/.."
FEL=external/Felucca
mkdir -p build
# native_check, as its header comment builds it (from the firmware's root)
(cd $FEL && cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o ../../build/native_check web/emu/native_check.c -lm)
cc -O2 -Wall -o build/render_ref tests/render_ref.c -Lbuild -lghoulbox_core -Wl,-rpath,'$ORIGIN'
build/native_check build/ref_native.f32
build/render_ref build/ref_lib.f32
size=$(stat -c %s build/ref_lib.f32)
[ "$size" -eq $((88200 * 8)) ] || { echo "FAIL: ref_lib.f32 is $size bytes, want $((88200 * 8))"; exit 1; }
cmp build/ref_native.f32 build/ref_lib.f32
echo "PASS: identical ($size bytes)"
