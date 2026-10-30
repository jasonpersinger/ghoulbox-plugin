/* SPDX-License-Identifier: GPL-3.0-only */
/* One plugin instance's private copy of libghoulbox_core.so: the firmware keeps its state in globals, so each instance
 * dlopens its own copy (a temp file, unlinked once mapped) and never shares them. */
#pragma once
#include "ghoulbox_core.h"
#include <cstdint>
#include <memory>
#include <string>

/* every gb_* symbol (core/ghoulbox_core.h): X(name, return type, parameters) */
#define GB_API_LIST(X)                                                    \
    X(nor_load, void, (const uint8_t*, uint32_t))                         \
    X(nor, const uint8_t*, (void))                                        \
    X(nor_size, uint32_t, (void))                                         \
    X(flash_writes, uint32_t, (void))                                     \
    X(nor_private, void, (uint32_t*, uint32_t*))                          \
    X(boot, void, (void))                                                 \
    X(render, void, (float*, float*, uint32_t))                           \
    X(midi, uint32_t, (uint8_t, uint8_t, uint8_t))                        \
    X(master, void, (uint32_t))                                           \
    X(clock_external, void, (int))                                        \
    X(track_cc, int, (uint32_t, uint8_t, uint8_t))                        \
    X(state_size, uint32_t, (void))                                       \
    X(state_save, uint32_t, (uint8_t*, uint32_t))                         \
    X(state_load, int, (const uint8_t*, uint32_t))                        \
    X(playing, uint32_t, (void))                                          \
    X(sample_rate, uint32_t, (void))                                      \
    X(view_read, void, (gb_view*))                                        \
    X(screen, const uint16_t*, (void))                                    \
    X(button, void, (uint32_t, int))                                      \
    X(enc, void, (uint32_t, int32_t))                                     \
    X(keys, void, (uint32_t))                                             \
    X(track_select, void, (uint32_t))                                     \
    X(sound_load, void, (uint32_t))                                       \
    X(midi_base, int32_t, (void))                                         \
    X(global_set, int, (uint32_t, int32_t))                               \
    X(global_desc, int, (uint32_t, char*, int16_t*, int16_t*, const char* const**))

struct GbApi {
#define GB_FIELD(name, ret, args) ret(*name) args;
    GB_API_LIST(GB_FIELD)
#undef GB_FIELD
};

class CoreInstance {
public:
    static std::unique_ptr<CoreInstance> open(const std::string& libPath, std::string* err);
    const GbApi& api() const { return api_; }
    ~CoreInstance();
    CoreInstance(const CoreInstance&) = delete;
    CoreInstance& operator=(const CoreInstance&) = delete;

private:
    CoreInstance() = default;
    void* handle_ = nullptr;
    GbApi api_{};
};
