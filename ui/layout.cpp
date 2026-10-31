/* SPDX-License-Identifier: GPL-3.0-only */
/* The regions: top bar y 0-48; sounds x 12-252, y 56-700; the screen 480 x 480 at (280, 64), the two button rows and
 * the active slot's 4 knobs under it; FX, MASTER and the hardware encoders x 788-1028; the 4 sound slots across the
 * bottom x 10-1034 at y 708-854. Headings sit in the gaps above each group. */
#include "layout.h"
#include "../core/ghoulbox_core.h"

static std::vector<std::pair<Hit, Rect>> build()
{
    std::vector<std::pair<Hit, Rect>> c;
    auto add = [&](Ctl k, int i, Rect r) { c.push_back({Hit{k, i}, r}); };
    add(Ctl::SoundList, 0, {12, 76, 240, kRowH * kSoundRows});
    add(Ctl::Screen, 0, {280, 64, 480, 480});
    static const int ROW1[5] = {GB_BTN_FX, GB_BTN_SCL, GB_BTN_ENV, GB_BTN_LFO, GB_BTN_EDIT};
    static const int ROW2[5] = {GB_BTN_HOME, GB_BTN_SAVE, GB_BTN_ARP, GB_BTN_OCTDN, GB_BTN_OCTUP};
    for (int i = 0; i < 5; i++)
        add(Ctl::Button, ROW1[i], {280 + i * 98.f, 552, 88, 32});
    for (int i = 0; i < 5; i++)
        add(Ctl::Button, ROW2[i], {280 + i * 98.f, 592, 88, 32});
    for (int k = 0; k < 4; k++)
        add(Ctl::Knob, k, {310 + k * 120.f, 644, 60, 56});
    add(Ctl::Global, 0, {800, 234, 96, 28});                   // FX: TYPE a pill, TAPE
    add(Ctl::Global, 1, {938, 220, 60, 56});
    add(Ctl::Master, 0, {818, 304, 60, 56});
    for (int i = 0; i < 3; i++)                                          // HARDWARE: SELECT ALGORITHM PRESETS
        add(Ctl::Enc, GB_ENC_SELECT + i, {812 + i * 72.f, 392, 48, 48});
    for (int i = 0; i < 4; i++)                                          // .. KNOB 1..4
        add(Ctl::Enc, GB_ENC_K1 + i, {800 + i * 60.f, 452, 48, 48});
    for (int t = 0; t < 4; t++) {
        const float x0 = 10 + t * 256.f;
        add(Ctl::TrackSel, t, {x0 + 16, 736, 110, 26});                  // Slot select pill
        add(Ctl::Layer, t, {x0 + 138, 736, 96, 26});                     // Slot on/off toggle
        add(Ctl::Fader, t, {x0 + 114, 786, 24, 60});                     // Slot volume fader
    }
    return c;
}

const std::vector<std::pair<Hit, Rect>>& controls()
{
    static const std::vector<std::pair<Hit, Rect>> c = build();
    return c;
}

Rect rectOf(Hit h)
{
    for (auto& c : controls())
        if (c.first == h)
            return c.second;
    return Rect{0, 0, 0, 0};
}

Hit hitTest(float x, float y, float scale)
{
    const float px = x / scale, py = y / scale;
    for (auto& c : controls())
        if (c.second.contains(px, py))
            return c.first.ctl == Ctl::SoundList ? Hit{Ctl::SoundRow, (int)((py - c.second.y) / kRowH)} : c.first;
    return Hit{Ctl::None, 0};
}
