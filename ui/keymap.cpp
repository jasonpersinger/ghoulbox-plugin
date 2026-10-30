/* SPDX-License-Identifier: GPL-3.0-only */
#include "keymap.h"
#include "../core/ghoulbox_core.h"
#include <cstring>

KeyAction keyAction(uint32_t ch)
{
    static const char WHITE_KEYS[] = "zxcvbnm,./";             // F3 G3 A3 B3 C4 D4 E4 F4 G4 A4
    static const int WHITE[10] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16};
    static const char BLACK_KEYS[] = "sdfhjl;'";
    static const int BLACK[8] = {1, 3, 5, 8, 10, 13, 15, 17};
    static const char UPPER[] = "qwertyuio";                   // 18..26 in semitones
    static const char BUTTON_KEYS[] = "1234567890-=";          // the panel's order: FX SCL ENV LFO EDIT GLO, HOME SAVE ARP
    static const int BUTTONS[12] = {GB_BTN_FX, GB_BTN_SCL, GB_BTN_ENV, GB_BTN_LFO, GB_BTN_EDIT, GB_BTN_GLO,
                                    GB_BTN_HOME, GB_BTN_SAVE, GB_BTN_ARP, GB_BTN_SEQ, GB_BTN_PLAY, GB_BTN_REC};
    if (ch >= 'A' && ch <= 'Z')
        ch += 'a' - 'A';
    if (!ch || ch > 127)
        return KeyAction{KeyAction::None, 0, 0};
    const char c = (char)ch;
    if (const char* p = std::strchr(WHITE_KEYS, c))
        return KeyAction{KeyAction::Key, WHITE[p - WHITE_KEYS], 0};
    if (const char* p = std::strchr(BLACK_KEYS, c))
        return KeyAction{KeyAction::Key, BLACK[p - BLACK_KEYS], 0};
    if (const char* p = std::strchr(UPPER, c))
        return KeyAction{KeyAction::Key, 18 + (int)(p - UPPER), 0};
    if (const char* p = std::strchr(BUTTON_KEYS, c))
        return KeyAction{KeyAction::Button, BUTTONS[p - BUTTON_KEYS], 0};
    if (c == '[' || c == ']')
        return KeyAction{KeyAction::Button, c == '[' ? GB_BTN_OCTDN : GB_BTN_OCTUP, 0};
    return KeyAction{KeyAction::None, 0, 0};
}

KeyAction arrowAction(Arrow a)
{
    switch (a) {
    case Arrow::Up: return KeyAction{KeyAction::Enc, GB_ENC_SELECT, 1};
    case Arrow::Down: return KeyAction{KeyAction::Enc, GB_ENC_SELECT, -1};
    case Arrow::Right: return KeyAction{KeyAction::Enc, GB_ENC_ALGO, 1};
    case Arrow::Left: return KeyAction{KeyAction::Enc, GB_ENC_ALGO, -1};
    }
    return KeyAction{KeyAction::None, 0, 0};
}
