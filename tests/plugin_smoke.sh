#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The plugin builds as CLAP, VST3 and LV2, each with its core library beside its binary.
set -e
cd "$(dirname "$0")/.."
P=build/plugin
fail() { echo "FAIL: $1"; exit 1; }
[ -f $P/ghoulbox.clap ] || fail "no CLAP"                       # (one file on Linux)
[ -f $P/libghoulbox_core.so ] || fail "no core beside the CLAP"
[ -d $P/ghoulbox.vst3 ] && [ -f $P/ghoulbox.vst3/Contents/x86_64-linux/ghoulbox.so ] || fail "no VST3"
[ -f $P/ghoulbox.vst3/Contents/x86_64-linux/libghoulbox_core.so ] || fail "no core beside the VST3"
[ -d $P/ghoulbox.lv2 ] && [ -f $P/ghoulbox.lv2/ghoulbox.so ] && [ -f $P/ghoulbox.lv2/manifest.ttl ] || fail "no LV2"
[ -f $P/ghoulbox.lv2/libghoulbox_core.so ] || fail "no core beside the LV2"
# Plan 2: the window lives in the same binary; nothing of it is exported
[ "$(nm -D --defined-only $P/ghoulbox.clap | awk '{print $3}')" = "clap_entry" ] || fail "the CLAP exports more than clap_entry"
if command -v clap-validator >/dev/null 2>&1; then
    clap-validator validate $P/ghoulbox.clap || fail "clap-validator"
else
    echo "(clap-validator not on PATH: skipped)"
fi
echo "PASS: CLAP, VST3, LV2 bundles"
