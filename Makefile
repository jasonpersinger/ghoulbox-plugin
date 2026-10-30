# SPDX-License-Identifier: GPL-3.0-only
FEL := external/Felucca
GEN := $(FEL)/build/gen
# as native_check (the Global Constraints): the firmware compiled unmodified
SPEEX_DEFS := -DOUTSIDE_SPEEX -DFLOATING_POINT -DRANDOM_PREFIX=ghoulbox_speex
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Ihost -Icore $(SPEEX_DEFS) -Iexternal/speex
CORE_CFLAGS := -O2 -ffp-contract=off -std=gnu99 -w -fPIC -I$(GEN) -I$(FEL)/firmware/src -I$(FEL)

.PHONY: gen core plugin install test test-identical test-core test-loader test-engine test-clock test-library test-bridge test-layer test-ui test-plugin
gen: $(GEN)
$(GEN):
	cd $(FEL) && python3 tools/build.py --gen-only

core: build/libghoulbox_core.so
build/libghoulbox_core.so: core/ghoulbox_core.c core/ghoulbox_core.h | $(GEN)
	@mkdir -p build
	cc $(CORE_CFLAGS) -shared -fvisibility=hidden -o $@ $< -lm

test: test-identical test-core test-loader test-engine test-clock test-library test-bridge test-layer test-ui test-plugin

test-identical: core
	sh tests/identical_test.sh

build/core_api_test: tests/core_api_test.c core/ghoulbox_core.c core/ghoulbox_core.h | $(GEN)
	@mkdir -p build
	cc $(CORE_CFLAGS) -o $@ $< -lm

test-core: build/core_api_test
	build/core_api_test

build/loader_test: tests/loader_test.cpp tests/private_runtime_dir.h host/core_loader.cpp host/core_loader.h core/ghoulbox_core.h
	@mkdir -p build
	c++ $(CXXFLAGS) -o $@ tests/loader_test.cpp host/core_loader.cpp -ldl

test-loader: build/loader_test core
	build/loader_test

build/resample.o: external/speex/resample.c
	@mkdir -p build
	cc -O2 -fPIC -w $(SPEEX_DEFS) -Iexternal/speex -c -o $@ $<

ENGINE_SRC := host/engine.cpp host/core_loader.cpp
build/engine_test: tests/engine_test.cpp tests/private_runtime_dir.h $(ENGINE_SRC) host/engine.h host/core_loader.h build/resample.o
	c++ $(CXXFLAGS) -o $@ tests/engine_test.cpp $(ENGINE_SRC) build/resample.o -ldl

test-engine: build/engine_test core
	build/engine_test

build/clock_test: tests/clock_test.cpp host/clock_gen.cpp host/clock_gen.h host/engine.h
	@mkdir -p build
	c++ $(CXXFLAGS) -o $@ tests/clock_test.cpp host/clock_gen.cpp

test-clock: build/clock_test
	build/clock_test

build/library_test: tests/library_test.cpp host/library.cpp host/library.h
	@mkdir -p build
	c++ $(CXXFLAGS) -o $@ tests/library_test.cpp host/library.cpp

test-library: build/library_test
	build/library_test

build/layer_test: tests/layer_test.cpp host/midi_layer.cpp host/midi_layer.h host/engine.h
	@mkdir -p build
	c++ $(CXXFLAGS) -o $@ tests/layer_test.cpp host/midi_layer.cpp

test-layer: build/layer_test
	build/layer_test

build/bridge_test: tests/bridge_test.cpp host/ui_bridge.cpp host/ui_bridge.h core/ghoulbox_core.h
	@mkdir -p build
	c++ $(CXXFLAGS) -o $@ tests/bridge_test.cpp host/ui_bridge.cpp -lpthread

test-bridge: build/bridge_test
	build/bridge_test

# by hand, once per change to the bridge: the same tests under ThreadSanitizer
test-bridge-tsan:
	@mkdir -p build
	c++ $(CXXFLAGS) -g -fsanitize=thread -o build/bridge_test_tsan tests/bridge_test.cpp host/ui_bridge.cpp -lpthread
	build/bridge_test_tsan

UI_SRC := ui/layout.cpp ui/keymap.cpp ui/pixels.cpp ui/knob_math.cpp
build/ui_logic_test: tests/ui_logic_test.cpp $(UI_SRC) ui/layout.h ui/keymap.h ui/pixels.h ui/knob_math.h
	@mkdir -p build
	c++ $(CXXFLAGS) -o $@ tests/ui_logic_test.cpp $(UI_SRC)

test-ui: build/ui_logic_test
	build/ui_logic_test

# the window's fonts (OFL, from the firmware: external/Felucca/LICENSES), as C++ data (DPF's res2c)
FONTS := $(FEL)/assets/fonts/VT323-Regular.ttf $(FEL)/assets/fonts/PirataOne-Regular.ttf
build/gen-ui/fonts.cpp: $(FONTS)
	@rm -rf build/gen-ui && mkdir -p build/gen-ui/res
	cp $(FONTS) build/gen-ui/res/
	python3 external/DPF/utils/res2c.py fonts build/gen-ui/res build/gen-ui

plugin: core build/resample.o build/gen-ui/fonts.cpp
	rm -f build/plugin/ghoulbox.lv2/ghoulbox_dsp.*   # (Plan 1 built a window-less LV2 under that name)
	$(MAKE) -C plugin
	for d in build/plugin build/plugin/ghoulbox.vst3/Contents/x86_64-linux build/plugin/ghoulbox.lv2; do \
		cp build/libghoulbox_core.so $$d/ || exit 1; done

build/clap_host_test: tests/clap_host_test.cpp tests/private_runtime_dir.h
	@mkdir -p build
	c++ $(CXXFLAGS) -Iexternal/DPF/distrho/src -o $@ $< -ldl -lpthread

test-plugin: plugin build/clap_host_test
	sh tests/plugin_smoke.sh
	build/clap_host_test

# by hand, with an X display: the real window in a bare host, pictured into build/gui-snapshot.png
build/gui_snapshot: tests/gui_snapshot.cpp
	@mkdir -p build
	c++ $(CXXFLAGS) -Iexternal/DPF/distrho/src -o $@ $< -ldl -lpthread -lX11

gui-snapshot: plugin build/gui_snapshot
	build/gui_snapshot

# the user's sudo-free plugin folders (the CLAP's core goes beside it in ~/.clap)
install: plugin
	cp build/plugin/ghoulbox.clap build/plugin/libghoulbox_core.so ~/.clap/
	rm -rf ~/.vst3/ghoulbox.vst3 ~/.lv2/ghoulbox.lv2
	cp -r build/plugin/ghoulbox.vst3 ~/.vst3/
	cp -r build/plugin/ghoulbox.lv2 ~/.lv2/
