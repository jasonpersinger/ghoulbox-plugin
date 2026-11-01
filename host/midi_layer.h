/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI layering (the plugin's "Track N MIDI layer" toggles): with any track lit, every channel message from the host
 * plays each lit track at once, sent on that track's channel in GHOULBOX's MIDI IN block. The firmware is untouched:
 * it gets what a controller sending on several channels would send. */
#pragma once
#include "engine.h"
#include <cstdint>
#include <vector>

struct MidiLayer {
    // noteOwners[slot][note]: bitmask of source channels (0..15) holding note on slot
    uint16_t noteOwners[4][128] = {};
    // sustainOwners[slot]: bitmask of source channels holding sustain pedal (CC 64) on slot
    uint16_t sustainOwners[4] = {};

    void resetSlot(uint32_t t);
    void reset();

    // in: one block's host MIDI, by frame. mask: bit t = track t lit. base: the channel of track 1 (gb_midi_base; -1 =
    // SEL, which already plays every channel on the selected track). Appends to out, never past cap.
    // Guarantees delivery of release messages even when capacity is reached.
    void layer(const MidiEv* in, uint32_t n, uint32_t mask, int32_t base, std::vector<MidiEv>& out, size_t cap);
};

void layerEvents(const MidiEv* in, uint32_t n, uint32_t mask, int32_t base, std::vector<MidiEv>& out, size_t cap);
