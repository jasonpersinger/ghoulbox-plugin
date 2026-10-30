/* SPDX-License-Identifier: GPL-3.0-only */
/* The window's controls: each one's rectangle at scale 1 (kW x kH logical px), and which control is under the mouse at
 * any scale. Pure: no drawing, no DPF. */
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
enum class Ctl : uint8_t { None, Screen, Button /*GB_BTN_*/, Enc /*GB_ENC_*/, Knob /*0..3*/, Global /*GB_G_*/,
                           Master, Fader /*track*/, TrackSel /*track*/, SoundRow /*visible row*/, SoundList, Step /*0..15*/,
                           Layer /*track: its MIDI layer toggle*/ };
struct Hit {
    Ctl ctl;
    int idx;
    bool operator==(const Hit& o) const { return ctl == o.ctl && idx == o.idx; }
};
constexpr float kW = 1040, kH = 860;
constexpr float kRowH = 26;                              // the sound list's rows
constexpr int kSoundRows = 24;                           // rows the list shows

const std::vector<std::pair<Hit, Rect>>& controls();     // every control at scale 1
Rect rectOf(Hit h);                                      // a control's rectangle at scale 1 (an empty one: none)
Hit hitTest(float x, float y, float scale);              // window px at `scale` -> the control under it; inside the
                                                         // SoundList rect: SoundRow, idx = the visible row
