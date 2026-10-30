/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI layering: the host's notes onto every lit track's channel. */
#include "../host/midi_layer.h"
#include <cstdio>

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static const MidiEv NOTE16{5, {0x9F, 60, 100}};          // a note on channel 16, at frame 5

static void test_layer_off_copies()
{
    std::vector<MidiEv> out;
    layerEvents(&NOTE16, 1, 0, 0, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0x9F);
}

static void test_layer_sel_copies()
{
    std::vector<MidiEv> out;
    layerEvents(&NOTE16, 1, 0xF, -1, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0x9F);
}

static void test_layer_fans_out()
{
    std::vector<MidiEv> out;
    layerEvents(&NOTE16, 1, 0x5, 0, out, 1024);          // tracks 1 and 3
    CHECK(out.size() == 2);
    CHECK(out[0].frame == 5 && out[0].b[0] == 0x90 && out[0].b[1] == 60 && out[0].b[2] == 100);
    CHECK(out[1].frame == 5 && out[1].b[0] == 0x92 && out[1].b[1] == 60 && out[1].b[2] == 100);
}

static void test_layer_follows_midi_in()
{
    std::vector<MidiEv> out;
    const MidiEv bend{0, {0xE0, 0, 64}};
    layerEvents(&bend, 1, 0x2, 4, out, 1024);            // track 2 in CH5-8: channel 6
    CHECK(out.size() == 1 && out[0].b[0] == 0xE5);
}

static void test_layer_leaves_clock()
{
    std::vector<MidiEv> out;
    const MidiEv clk{3, {0xF8, 0, 0}};
    layerEvents(&clk, 1, 0xF, 0, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0xF8);
}

static void test_layer_cap()
{
    std::vector<MidiEv> in(600, NOTE16), out;
    layerEvents(in.data(), (uint32_t)in.size(), 0xF, 0, out, 1024);
    CHECK(out.size() == 1024);
}

int main()
{
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_layer_off_copies", test_layer_off_copies},
        {"test_layer_sel_copies", test_layer_sel_copies},
        {"test_layer_fans_out", test_layer_fans_out},
        {"test_layer_follows_midi_in", test_layer_follows_midi_in},
        {"test_layer_leaves_clock", test_layer_leaves_clock},
        {"test_layer_cap", test_layer_cap},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    return fails != 0;
}
