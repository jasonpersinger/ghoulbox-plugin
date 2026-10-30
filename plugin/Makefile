#!/usr/bin/make -f
# SPDX-License-Identifier: GPL-3.0-only
# GHOULBOX as CLAP, VST3 and LV2 (DPF). Built from the top level: make plugin
NAME = ghoulbox
FILES_DSP = GhoulboxPlugin.cpp ../host/engine.cpp ../host/core_loader.cpp ../host/clock_gen.cpp ../host/library.cpp \
            ../host/ui_bridge.cpp ../host/midi_layer.cpp
FILES_UI = GhoulboxUI.cpp ../ui/layout.cpp ../ui/keymap.cpp ../ui/pixels.cpp ../ui/knob_math.cpp ../build/gen-ui/fonts.cpp
DPF_TARGET_DIR = ../build/plugin
DPF_BUILD_DIR = ../build/plugin-obj

include ../external/DPF/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17 -I../host -I../core -I../ui -I../build/gen-ui -I../external/speex -DOUTSIDE_SPEEX -DFLOATING_POINT -DRANDOM_PREFIX=ghoulbox_speex
LINK_FLAGS += ../build/resample.o -ldl -lpthread

TARGETS = clap vst3 lv2                    # (lv2: the DSP and the window in one binary, for direct access)

all: $(TARGETS) ttl

ttl: lv2
	$(MAKE) -C ../external/DPF/utils/lv2-ttl-generator
	../external/DPF/utils/generate-ttl.sh ../build/plugin
