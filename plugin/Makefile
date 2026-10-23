#!/usr/bin/make -f
# SPDX-License-Identifier: GPL-3.0-only
# GHOULBOX as CLAP, VST3 and LV2 (DPF). Built from the top level: make plugin
NAME = ghoulbox
FILES_DSP = GhoulboxPlugin.cpp ../host/engine.cpp ../host/core_loader.cpp ../host/clock_gen.cpp ../host/library.cpp
DPF_TARGET_DIR = ../build/plugin
DPF_BUILD_DIR = ../build/plugin-obj

include ../external/DPF/Makefile.plugins.mk

BUILD_CXX_FLAGS += -std=gnu++17 -I../host -I../core -I../external/speex -DOUTSIDE_SPEEX -DFLOATING_POINT -DRANDOM_PREFIX=ghoulbox_speex
LINK_FLAGS += ../build/resample.o -ldl -lpthread

TARGETS = clap vst3 lv2_dsp

all: $(TARGETS) ttl

ttl: lv2_dsp
	$(MAKE) -C ../external/DPF/utils/lv2-ttl-generator
	../external/DPF/utils/generate-ttl.sh ../build/plugin
