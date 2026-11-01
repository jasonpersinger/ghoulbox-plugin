/* SPDX-License-Identifier: GPL-3.0-only */
#include "midi_layer.h"
#include <algorithm>
#include <cstring>

void MidiLayer::resetSlot(uint32_t t)
{
    if (t < 4) {
        std::memset(noteOwners[t], 0, sizeof(noteOwners[t]));
        sustainOwners[t] = 0;
    }
}

void MidiLayer::reset()
{
    std::memset(noteOwners, 0, sizeof(noteOwners));
    std::memset(sustainOwners, 0, sizeof(sustainOwners));
}

static bool isRelease(const MidiEv& e)
{
    const uint8_t st = e.b[0] & 0xF0u;
    if (st == 0x80u)
        return true;
    if (st == 0x90u && (e.b[2] & 0x7Fu) == 0)
        return true;
    if (st == 0xB0u) {
        const uint8_t cc = e.b[1] & 0x7Fu;
        const uint8_t val = e.b[2] & 0x7Fu;
        if (cc == 64 && val < 64)
            return true;
        if (cc == 120 || cc == 123)
            return true;
    }
    return false;
}

static void pushReleasePriority(std::vector<MidiEv>& out, const MidiEv& ev, size_t cap, bool isRel, bool& replaced)
{
    if (out.size() < cap) {
        out.push_back(ev);
        return;
    }
    if (!isRel)
        return;
    for (size_t i = out.size(); i > 0; i--) {
        if (!isRelease(out[i - 1])) {
            out[i - 1] = ev;
            replaced = true;
            return;
        }
    }
}

void MidiLayer::layer(const MidiEv* in, uint32_t n, uint32_t mask, int32_t base, std::vector<MidiEv>& out, size_t cap)
{
    (void)base;
    bool replaced = false;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t st = in[i].b[0];
        if (st < 0x80 || st >= 0xF0) {                 // real-time and system messages: copied as they came
            pushReleasePriority(out, in[i], cap, isRelease(in[i]), replaced);
            continue;
        }
        const uint8_t type = st & 0xF0u;
        const uint8_t srcCh = st & 0x0Fu;
        const uint8_t d1 = in[i].b[1] & 0x7Fu;
        const uint8_t d2 = in[i].b[2] & 0x7Fu;

        for (uint32_t t = 0; t < 4; t++) {
            if (!(mask >> t & 1u))
                continue;

            if (type == 0x90u && d2 > 0) {
                noteOwners[t][d1] |= (uint16_t)(1u << srcCh);
                MidiEv e = in[i];
                e.b[0] = (uint8_t)(0x90u | t);
                pushReleasePriority(out, e, cap, false, replaced);
            } else if (type == 0x80u || (type == 0x90u && d2 == 0)) {
                noteOwners[t][d1] &= (uint16_t)~(1u << srcCh);
                if (noteOwners[t][d1] == 0) {
                    MidiEv e = in[i];
                    e.b[0] = (uint8_t)(0x80u | t);
                    e.b[2] = 0;
                    pushReleasePriority(out, e, cap, true, replaced);
                }
            } else if (type == 0xB0u && d1 == 64) {
                if (d2 >= 64) {
                    bool wasSustained = (sustainOwners[t] != 0);
                    sustainOwners[t] |= (uint16_t)(1u << srcCh);
                    if (!wasSustained) {
                        MidiEv e = in[i];
                        e.b[0] = (uint8_t)(0xB0u | t);
                        pushReleasePriority(out, e, cap, false, replaced);
                    }
                } else {
                    sustainOwners[t] &= (uint16_t)~(1u << srcCh);
                    if (sustainOwners[t] == 0) {
                        MidiEv e = in[i];
                        e.b[0] = (uint8_t)(0xB0u | t);
                        pushReleasePriority(out, e, cap, true, replaced);
                    }
                }
            } else if (type == 0xB0u && (d1 == 120 || d1 == 123)) {
                std::memset(noteOwners[t], 0, sizeof(noteOwners[t]));
                sustainOwners[t] = 0;
                MidiEv e = in[i];
                e.b[0] = (uint8_t)(0xB0u | t);
                pushReleasePriority(out, e, cap, true, replaced);
            } else {
                MidiEv e = in[i];
                e.b[0] = (uint8_t)(type | t);
                pushReleasePriority(out, e, cap, isRelease(e), replaced);
            }
        }
    }
    if (replaced) {
        std::stable_sort(out.begin(), out.end(), [](const MidiEv& a, const MidiEv& b) {
            return a.frame < b.frame;
        });
    }
}

void layerEvents(const MidiEv* in, uint32_t n, uint32_t mask, int32_t base, std::vector<MidiEv>& out, size_t cap)
{
    static thread_local MidiLayer defaultLayer;
    defaultLayer.layer(in, n, mask, base, out, cap);
}
