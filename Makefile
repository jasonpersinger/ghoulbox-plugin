# SPDX-License-Identifier: GPL-3.0-only
FEL := external/Felucca
GEN := $(FEL)/build/gen
# as native_check (the Global Constraints): the firmware compiled unmodified
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Ihost -Icore
CORE_CFLAGS := -O2 -ffp-contract=off -std=gnu99 -w -fPIC -I$(GEN) -I$(FEL)/firmware/src -I$(FEL)

.PHONY: gen core test test-identical test-core test-loader
gen: $(GEN)
$(GEN):
	cd $(FEL) && python3 tools/build.py --gen-only

core: build/libghoulbox_core.so
build/libghoulbox_core.so: core/ghoulbox_core.c core/ghoulbox_core.h | $(GEN)
	@mkdir -p build
	cc $(CORE_CFLAGS) -shared -fvisibility=hidden -o $@ $< -lm

test: test-identical test-core test-loader

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
