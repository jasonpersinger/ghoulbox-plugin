/* SPDX-License-Identifier: GPL-3.0-only */
/* The host's transport as MIDI clock for the core (GLO CLOCK = USB): FA at a start, FC at a stop, F8 at every 1/24 of a
 * quarter note on its frame; a loop or a jump while playing restarts (FC, FA). No Song Position Pointer: the firmware
 * takes none, so a restart begins at the pattern's step 1. */
#pragma once
#include "engine.h"
#include <vector>

struct HostTime { bool playing; double ppqPos; double bpm; };   // at the block's first frame

/* the host's bar / beat / tick as quarter notes from the song's start. beatsAreQuarters: the host's beat already is a
 * quarter note (DPF's CLAP wrapper splits song_pos_beats, in quarters, by the numerator alone); else a beat is a
 * 1/beatType note (DPF's VST3 wrapper) */
double quartersFromBbt(double barStartTick, int beat, double tick, double ticksPerBeat, double beatType,
                       bool beatsAreQuarters);

class ClockGen {
public:
    void reset() { playing_ = false; }
    // appends F8/FA/FB/FC events for one block; frames = block length; rate = host rate
    void block(const HostTime& t, uint32_t frames, double rate, std::vector<MidiEv>& out);

private:
    bool playing_ = false;
    double expect_ = 0;   // the ppq this block should start at, if the host played on
    int64_t next_ = 0;    // the next pulse: at ppq next_ / 24
};
