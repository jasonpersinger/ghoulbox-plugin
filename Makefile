# SPDX-License-Identifier: GPL-3.0-only
FEL := external/Felucca
GEN := $(FEL)/build/gen
# as native_check (the Global Constraints): the firmware compiled unmodified
CORE_CFLAGS := -O2 -ffp-contract=off -std=gnu99 -w -fPIC -I$(GEN) -I$(FEL)/firmware/src -I$(FEL)

.PHONY: gen core test test-identical
gen: $(GEN)
$(GEN):
	cd $(FEL) && python3 tools/build.py --gen-only

core: build/libghoulbox_core.so
build/libghoulbox_core.so: core/ghoulbox_core.c core/ghoulbox_core.h | $(GEN)
	@mkdir -p build
	cc $(CORE_CFLAGS) -shared -fvisibility=hidden -o $@ $< -lm

test: test-identical

test-identical: core
	sh tests/identical_test.sh
