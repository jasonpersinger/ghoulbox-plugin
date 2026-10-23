# SPDX-License-Identifier: GPL-3.0-only
FEL := external/Felucca
GEN := $(FEL)/build/gen
# as native_check (the Global Constraints): the firmware compiled unmodified
SPEEX_DEFS := -DOUTSIDE_SPEEX -DFLOATING_POINT -DRANDOM_PREFIX=ghoulbox_speex
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Ihost -Icore $(SPEEX_DEFS) -Iexternal/speex
CORE_CFLAGS := -O2 -ffp-contract=off -std=gnu99 -w -fPIC -I$(GEN) -I$(FEL)/firmware/src -I$(FEL)

.PHONY: gen core test test-identical test-core test-loader test-engine test-clock test-library
gen: $(GEN)
$(GEN):
	cd $(FEL) && python3 tools/build.py --gen-only

core: build/libghoulbox_core.so
build/libghoulbox_core.so: core/ghoulbox_core.c core/ghoulbox_core.h | $(GEN)
	@mkdir -p build
	cc $(CORE_CFLAGS) -shared -fvisibility=hidden -o $@ $< -lm

test: test-identical test-core test-loader test-engine test-clock test-library

test-identical: core
	sh tests/identical_test.sh

build/core_api_test: tests/core_api_test.c core/ghoulbox_core.c core/ghoulbox_core.h | $(GEN)
	@mkdir -p build
	cc $(CORE_CFLAGS) -o $@ $< -lm

test-core: build/core_api_test
	build/core_api_test

build/loader_test: tests/loader_test.cpp host/core_loader.cpp host/core_loader.h core/ghoulbox_core.h
	@mkdir -p build
	c++ $(CXXFLAGS) -o $@ tests/loader_test.cpp host/core_loader.cpp -ldl

test-loader: build/loader_test core
	build/loader_test

build/resample.o: external/speex/resample.c
	@mkdir -p build
	cc -O2 -fPIC -w $(SPEEX_DEFS) -Iexternal/speex -c -o $@ $<

ENGINE_SRC := host/engine.cpp host/core_loader.cpp
build/engine_test: tests/engine_test.cpp $(ENGINE_SRC) host/engine.h host/core_loader.h build/resample.o
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
