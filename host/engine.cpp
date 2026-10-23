/* SPDX-License-Identifier: GPL-3.0-only */
#include "engine.h"
#include "speex_resampler.h"
#include <algorithm>
#include <cmath>

static constexpr uint32_t CHUNK = 1024;    // host frames per resampler call; core frames per pull at most

Engine::Engine(std::unique_ptr<CoreInstance> core, double hostRate) : core_(std::move(core))
{
    coreL_.resize(CHUNK);
    coreR_.resize(CHUNK);
    in_.resize(2 * CHUNK);
    out_.resize(2 * CHUNK);
    setHostRate(hostRate);
}

Engine::~Engine()
{
    if (rs_)
        speex_resampler_destroy(reinterpret_cast<SpeexResamplerState*>(rs_));
}

void Engine::setHostRate(double hostRate)
{
    const uint32_t coreRate = core_->api().sample_rate(), host = (uint32_t)std::lround(hostRate);
    if (rs_)
        speex_resampler_destroy(reinterpret_cast<SpeexResamplerState*>(rs_));
    rs_ = nullptr;
    inPos_ = inLen_ = 0;
    ratio_ = (double)coreRate / host;
    if (host != coreRate) {
        int err = 0;
        rs_ = reinterpret_cast<SpeexResamplerState_*>(speex_resampler_init(2, coreRate, host, 7, &err));
    }
}

/* n host frames: the core rendered as they need it */
void Engine::produce(float* l, float* r, uint32_t n)
{
    if (!rs_) {
        core_->api().render(l, r, n);
        return;
    }
    auto* rs = reinterpret_cast<SpeexResamplerState*>(rs_);
    while (n) {
        if (inPos_ == inLen_) {                    // pull only what this stretch needs: an event may follow it
            uint32_t m = std::clamp<uint32_t>((uint32_t)std::ceil(n * ratio_), 1, CHUNK);
            core_->api().render(coreL_.data(), coreR_.data(), m);
            for (uint32_t i = 0; i < m; i++) {
                in_[2 * i] = coreL_[i];
                in_[2 * i + 1] = coreR_[i];
            }
            inPos_ = 0;
            inLen_ = m;
        }
        spx_uint32_t inN = inLen_ - inPos_, outN = std::min(n, CHUNK);
        speex_resampler_process_interleaved_float(rs, &in_[2 * inPos_], &inN, out_.data(), &outN);
        for (uint32_t i = 0; i < outN; i++) {
            l[i] = out_[2 * i];
            r[i] = out_[2 * i + 1];
        }
        inPos_ += inN;
        l += outN;
        r += outN;
        n -= outN;
    }
}

void Engine::process(float* outL, float* outR, uint32_t frames, const MidiEv* ev, uint32_t nev)
{
    uint32_t at = 0, i = 0;
    while (at < frames) {
        for (; i < nev && ev[i].frame <= at; i++)
            core_->api().midi(ev[i].b[0], ev[i].b[1], ev[i].b[2]);
        uint32_t end = i < nev ? std::min(ev[i].frame, frames) : frames;
        produce(outL + at, outR + at, end - at);
        at = end;
    }
    for (; i < nev; i++)                           // (past the block's end: now, not lost)
        core_->api().midi(ev[i].b[0], ev[i].b[1], ev[i].b[2]);
}
