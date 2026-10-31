/* SPDX-License-Identifier: GPL-3.0-only */
/* The regions (plan 2, Task 4): top bar y 0-48; sounds x 12-252, y 56-700; the screen 480 x 480 at (280, 64), the two
 * button rows and the track's 4 knobs under it; DREAM, FX, MASTER and the hardware encoders x 788-1028; the tracks
 * x 12-400 and the steps x 412-1028 at y 712-848. Headings sit in the gaps above each group. */
#include "layout.h"
#include "../core/ghoulbox_core.h"

static std::vector<std::pair<Hit, Rect>> build()
{
    std::vector<std::pair<Hit, Rect>> c;
    auto add = [&](Ctl k, int i, Rect r) { c.push_back({Hit{k, i}, r}); };
    add(Ctl::SoundList, 0, {12, 76, 240, kRowH * kSoundRows});
    add(Ctl::Screen, 0, {280, 64, 480, 480});
    static const int ROW1[6] = {GB_BTN_HOME, GB_BTN_SEQ, GB_BTN_GLO, GB_BTN_FX, GB_BTN_EDIT, GB_BTN_SAVE};
    static const int ROW2[8] = {GB_BTN_SCL, GB_BTN_ENV, GB_BTN_LFO, GB_BTN_ARP, GB_BTN_PLAY, GB_BTN_REC, GB_BTN_OCTDN,
                                GB_BTN_OCTUP};
    for (int i = 0; i < 6; i++)
        add(Ctl::Button, ROW1[i], {280 + i * (74 + 7.2f), 552, 74, 34});
    for (int i = 0; i < 8; i++)
        add(Ctl::Button, ROW2[i], {280 + i * (54 + 48.f / 7), 592, 54, 28});
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
        const float x0 = 12 + t * 97.f;
        add(Ctl::TrackSel, t, {x0 + 8, 732, 81, 22});
        add(Ctl::Fader, t, {x0 + 38, 778, 20, 66});
        add(Ctl::Layer, t, {x0 + 64, 800, 30, 20});                      // beside the fader: "MIDI"
    }
    for (int i = 0; i < 16; i++)
        add(Ctl::Step, i, {412 + i * 38.75f, 740, 34.75f, 80});
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
