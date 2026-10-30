/* SPDX-License-Identifier: GPL-3.0-only */
/* Plan 2, Tasks 4 and 6: the window's pure logic (layout, key map, screen pixels, knob and list arithmetic). */
#include "../core/ghoulbox_core.h"
#include "../ui/keymap.h"
#include "../ui/knob_math.h"
#include "../ui/layout.h"
#include "../ui/pixels.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static void test_controls_complete()
{
    std::map<Ctl, int> n;
    for (auto& c : controls())
        n[c.first.ctl]++;
    CHECK(n[Ctl::Button] == 14 && n[Ctl::Enc] == 7 && n[Ctl::Knob] == 4 && n[Ctl::Global] == 6);
    CHECK(n[Ctl::Master] == 1 && n[Ctl::Fader] == 4 && n[Ctl::TrackSel] == 4 && n[Ctl::Step] == 16);
    CHECK(n[Ctl::Screen] == 1 && n[Ctl::SoundList] == 1);
}

static void test_no_overlaps()
{
    const auto& c = controls();
    for (size_t i = 0; i < c.size(); i++) {
        const Rect& a = c[i].second;
        CHECK(a.w > 0 && a.h > 0 && a.x >= 0 && a.y >= 0 && a.x + a.w <= kW && a.y + a.h <= kH);
        for (size_t j = i + 1; j < c.size(); j++) {
            const Rect& b = c[j].second;
            const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
            if (!apart)
                std::fprintf(stderr, "  overlap: %d/%d and %d/%d\n", (int)c[i].first.ctl, c[i].first.idx,
                             (int)c[j].first.ctl, c[j].first.idx);
            CHECK(apart);
        }
    }
}

static void test_hit_scaled()
{
    for (float s : {0.6f, 0.75f, 1.f, 1.5f, 2.f})
        for (auto& c : controls()) {
            const float cx = c.second.x + c.second.w / 2, cy = c.second.y + c.second.h / 2;
            Hit want = c.first;
            if (want.ctl == Ctl::SoundList)                 // (inside the list: the row under the mouse)
                want = Hit{Ctl::SoundRow, (int)((cy - c.second.y) / kRowH)};
            CHECK(hitTest(cx * s, cy * s, s) == want);
        }
    CHECK(hitTest(1, 1, 1).ctl == Ctl::None);
}

static void test_keymap()
{
    auto is = [](KeyAction a, KeyAction::Kind k, int idx) { return a.kind == k && a.idx == idx; };
    CHECK(is(keyAction('z'), KeyAction::Key, 0));
    CHECK(is(keyAction('Z'), KeyAction::Key, 0));
    CHECK(is(keyAction('/'), KeyAction::Key, 16));
    CHECK(is(keyAction('s'), KeyAction::Key, 1));
    CHECK(is(keyAction('\''), KeyAction::Key, 17));
    CHECK(is(keyAction('o'), KeyAction::Key, 26));
    CHECK(is(keyAction('1'), KeyAction::Button, GB_BTN_FX));
    CHECK(is(keyAction('7'), KeyAction::Button, GB_BTN_HOME));
    CHECK(is(keyAction('='), KeyAction::Button, GB_BTN_REC));
    CHECK(is(keyAction('['), KeyAction::Button, GB_BTN_OCTDN));
    CHECK(is(keyAction(']'), KeyAction::Button, GB_BTN_OCTUP));
    CHECK(keyAction('p').kind == KeyAction::None);
    KeyAction up = arrowAction(Arrow::Up), left = arrowAction(Arrow::Left);
    CHECK(up.kind == KeyAction::Enc && up.idx == GB_ENC_SELECT && up.steps == 1);
    CHECK(left.kind == KeyAction::Enc && left.idx == GB_ENC_ALGO && left.steps == -1);
}

static void test_pixels()
{
    const uint8_t in[4][2] = {{0xF8, 0x00}, {0x07, 0xE0}, {0x00, 0x1F}, {0xFF, 0xFF}};
    const uint8_t want[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}};
    std::vector<uint16_t> panel(240 * 240);
    std::vector<uint8_t> rgba(240 * 240 * 4);
    std::memcpy(panel.data(), in, sizeof in);           // the first 4 pixels, as the panel's bytes lie in memory
    screenToRgba(panel.data(), rgba.data());
    for (int i = 0; i < 4; i++)
        CHECK(std::memcmp(&rgba[i * 4], want[i], 4) == 0);
}

static void test_drag_from_live()
{
    CHECK(dragValue(64, 0, -1, 127) == 64);              // starting from the live value: no jump
    CHECK(dragValue(64, -100, -1, 127) == 127);           // 100 px up = half the range: clamped
    CHECK(dragValue(64, 50, -1, 127) == 32);              // 50 px down = a quarter
    CHECK(wheelValue(3, 1, 1, 8) == 4 && wheelValue(8, 1, 1, 8) == 8 && wheelValue(1, -1, 1, 8) == 1);
}

static void test_list_top()
{
    CHECK(listTop(40, 91, 24, 0) == 17);
    CHECK(listTop(3, 91, 24, 17) == 3);
    CHECK(listTop(90, 91, 24, 0) == 67);
    CHECK(listTop(5, 10, 24, 3) == 0);                    // a list shorter than the window: from the top
    CHECK(listTop(91, 91, 24, 80) == 67);                 // no current sound (== count): only clamped
}

static void test_sound_wheel_scroll()
{
    CHECK(scrollTop(0, 3, 91, 24) == 9);
    CHECK(scrollTop(60, 5, 91, 24) == 67);               // clamped at the end
    CHECK(scrollTop(2, -1, 91, 24) == 0);                // .. and at the top
    CHECK(scrollTop(0, 3, 10, 24) == 0);                 // a short list doesn't scroll
}

static void test_pill_cycle()
{
    CHECK(cycleValue(2, 0, 2) == 0);
    CHECK(cycleValue(0, 0, 2) == 1);
    CHECK(cycleValue(-1, 0, 1) == 0);                    // unset: the first value
}

int main()
{
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_controls_complete", test_controls_complete},
        {"test_no_overlaps", test_no_overlaps},
        {"test_hit_scaled", test_hit_scaled},
        {"test_keymap", test_keymap},
        {"test_pixels", test_pixels},
        {"test_drag_from_live", test_drag_from_live},
        {"test_list_top", test_list_top},
        {"test_sound_wheel_scroll", test_sound_wheel_scroll},
        {"test_pill_cycle", test_pill_cycle},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    return fails != 0;
}
