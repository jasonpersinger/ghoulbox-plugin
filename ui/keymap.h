/* SPDX-License-Identifier: GPL-3.0-only */
/* The computer keyboard as the device's controls: the browser emulator's map (web/emu/index.html KEYMAP, ARROWS), by
 * character, case-insensitive. Pure. */
#pragma once
#include <cstdint>

struct KeyAction {
    enum Kind { None, Key, Button, Enc } kind;
    int idx, steps;     // Key: 0..26 (F3 + idx semitones); Button: GB_BTN_*; Enc: GB_ENC_* and steps
};
enum class Arrow { Up, Down, Left, Right };

KeyAction keyAction(uint32_t ch);   // z x c v b n m , . / -> white keys 0 2 4 6 7 9 11 12 14 16; s d f h j l ; ' ->
                                    // 1 3 5 8 10 13 15 17; q..o -> 18..26; 1..0 - = -> FX SCL ENV LFO EDIT GLO HOME SAVE ARP
                                    // SEQ PLAY REC; [ ] -> OCT- OCT+
KeyAction arrowAction(Arrow a);     // Up / Down: SELECT +1 / -1; Right / Left: ALGORITHM +1 / -1
