/* SPDX-License-Identifier: GPL-3.0-only */
/* One instance's audio: MIDI into the core at its host frame, the core's 44.1 kHz out at the host's rate. */
#pragma once
#include "core_loader.h"
#include <vector>

struct MidiEv { uint32_t frame; uint8_t b[3]; };   // frame: offset in the current host block

struct SpeexResamplerState_;
class Engine {
public:
    Engine(std::unique_ptr<CoreInstance> core, double hostRate);
    ~Engine();
    void setHostRate(double hostRate);
    void process(float* outL, float* outR, uint32_t frames, const MidiEv* ev, uint32_t nev);  // ev sorted by frame
    CoreInstance& core() { return *core_; }

private:
    void produce(float* l, float* r, uint32_t n);
    std::unique_ptr<CoreInstance> core_;
    SpeexResamplerState_* rs_ = nullptr;   // null at 44.1 kHz: straight through, bit-exact
    double ratio_ = 1;                     // core frames per host frame
    std::vector<float> coreL_, coreR_, in_, out_;
    uint32_t inPos_ = 0, inLen_ = 0;       // core frames in in_ (interleaved) not yet taken by the resampler
};
