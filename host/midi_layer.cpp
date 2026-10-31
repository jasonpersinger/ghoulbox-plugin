/* SPDX-License-Identifier: GPL-3.0-only */
#include "midi_layer.h"

void layerEvents(const MidiEv* in, uint32_t n, uint32_t mask, int32_t base, std::vector<MidiEv>& out, size_t cap)
{
    (void)base;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t st = in[i].b[0];
        if (st < 0x80 || st >= 0xF0) {                 // real-time and system messages: copied as they came
            if (out.size() >= cap)
                return;
            out.push_back(in[i]);
            continue;
        }
        for (uint32_t t = 0; t < 4; t++) {
            if (!(mask >> t & 1u))
                continue;
            if (out.size() >= cap)
                return;
            MidiEv e = in[i];
            e.b[0] = (uint8_t)((st & 0xF0u) | t);      // slot t plays on channel t
            out.push_back(e);
        }
    }
}
