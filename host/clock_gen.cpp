/* SPDX-License-Identifier: GPL-3.0-only */
#include "clock_gen.h"
#include <cmath>

static constexpr double PPQN = 24;

void ClockGen::block(const HostTime& t, uint32_t frames, double rate, std::vector<MidiEv>& out)
{
    if (!t.playing) {
        if (playing_)
            out.push_back(MidiEv{0, {0xFC, 0, 0}});
        playing_ = false;
        return;
    }
    const bool jumped = playing_ && std::fabs(t.ppqPos - expect_) > 0.5 / PPQN;
    if (jumped)
        out.push_back(MidiEv{0, {0xFC, 0, 0}});
    if (!playing_ || jumped) {
        out.push_back(MidiEv{0, {0xFA, 0, 0}});
        next_ = (int64_t)std::ceil(t.ppqPos * PPQN - 1e-9);   // the first pulse at or after the start
    }
    playing_ = true;
    const double ppqPerFrame = t.bpm / 60.0 / rate;
    for (;;) {                                    // each pulse once: the counter carries across blocks
        double f = std::round((next_ / PPQN - t.ppqPos) / ppqPerFrame);
        if (f >= frames)
            break;
        out.push_back(MidiEv{(uint32_t)std::max(f, 0.0), {0xF8, 0, 0}});
        next_++;
    }
    expect_ = t.ppqPos + frames * ppqPerFrame;
}
