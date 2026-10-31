/* SPDX-License-Identifier: GPL-3.0-only */
/* The plugin's parameters 34..39: the panel's DREAM, reverb TYPE and TAPE (GHOULBOX's globals). Their ranges and value
 * names are the firmware's (params.c GP[]); tests/loader_test.cpp checks them against the core, so a firmware bump that
 * changes them fails there instead of drifting. Each is a DPF parameter -1..max, -1 = unset. */
#pragma once
#include "ghoulbox_core.h"
#include <cstdint>

struct GlobalParam {
    const char* name;
    const char* sym;
    uint32_t gb;                 // GB_G_*
    int16_t min, max;
    const char* const* names;    // enum value names, or nullptr
};

static const char* const GP_RTYPE[] = {"ROOM", "SPRING", "HALL"};

static const GlobalParam GLOBAL_PARAMS[2] = {
    {"Reverb Type", "reverb_type", GB_G_RTYPE, 0, 2, GP_RTYPE},
    {"Tape", "tape", GB_G_TAPE, 0, 127, nullptr},
};
