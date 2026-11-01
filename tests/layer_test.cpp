/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI layering: the host's notes onto every enabled slot's channel (0..3). */
#include "../host/midi_layer.h"
#include <cstdio>

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static const MidiEv NOTE16{5, {0x9F, 60, 100}};          // a note on channel 16, at frame 5

static void test_layer_slot1_default()
{
    std::vector<MidiEv> out;
    layerEvents(&NOTE16, 1, 1, 0, out, 1024);            // slot 1 only (default)
    CHECK(out.size() == 1);
    CHECK(out[0].frame == 5 && out[0].b[0] == 0x90 && out[0].b[1] == 60 && out[0].b[2] == 100);
}

static void test_layer_slots_1_and_3()
{
    std::vector<MidiEv> out;
    layerEvents(&NOTE16, 1, 0x5, 0, out, 1024);          // slots 1 and 3
    CHECK(out.size() == 2);
    CHECK(out[0].frame == 5 && out[0].b[0] == 0x90 && out[0].b[1] == 60 && out[0].b[2] == 100);
    CHECK(out[1].frame == 5 && out[1].b[0] == 0x92 && out[1].b[1] == 60 && out[1].b[2] == 100);
}

static void test_layer_off_silent()
{
    std::vector<MidiEv> out;
    layerEvents(&NOTE16, 1, 0, 0, out, 1024);            // no slots enabled
    CHECK(out.empty());
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

static void test_overlapping_note_ownership()
{
    MidiLayer layer;
    std::vector<MidiEv> out;
    const MidiEv onCh0{0, {0x90, 60, 100}};
    const MidiEv onCh1{1, {0x91, 60, 100}};
    const MidiEv offCh0{2, {0x80, 60, 0}};
    const MidiEv offCh1{3, {0x81, 60, 0}};

    // Slot 1 only (mask = 1)
    layer.layer(&onCh0, 1, 1, 0, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0x90 && out[0].b[1] == 60);

    out.clear();
    layer.layer(&onCh1, 1, 1, 0, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0x90 && out[0].b[1] == 60);

    // Release ch 0: note C4 should NOT be released because ch 1 is still holding it!
    out.clear();
    layer.layer(&offCh0, 1, 1, 0, out, 1024);
    CHECK(out.empty());

    // Release ch 1: note C4 should now be released
    out.clear();
    layer.layer(&offCh1, 1, 1, 0, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0x80 && out[0].b[1] == 60);
}

static void test_overlapping_sustain_ownership()
{
    MidiLayer layer;
    std::vector<MidiEv> out;
    const MidiEv susOnCh0{0, {0xB0, 64, 127}};
    const MidiEv susOnCh1{1, {0xB1, 64, 127}};
    const MidiEv susOffCh0{2, {0xB0, 64, 0}};
    const MidiEv susOffCh1{3, {0xB1, 64, 0}};

    // Sustain pressed on ch 0: emitted
    layer.layer(&susOnCh0, 1, 1, 0, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0xB0 && out[0].b[1] == 64 && out[0].b[2] == 127);

    // Sustain pressed on ch 1: already active on slot 1, ownership tracked
    out.clear();
    layer.layer(&susOnCh1, 1, 1, 0, out, 1024);
    CHECK(out.empty());

    // Release sustain on ch 0: still held by ch 1, NOT emitted
    out.clear();
    layer.layer(&susOffCh0, 1, 1, 0, out, 1024);
    CHECK(out.empty());

    // Release sustain on ch 1: all owners released, emitted
    out.clear();
    layer.layer(&susOffCh1, 1, 1, 0, out, 1024);
    CHECK(out.size() == 1 && out[0].b[0] == 0xB0 && out[0].b[1] == 64 && out[0].b[2] == 0);
}

static void test_release_priority_truncation()
{
    MidiLayer layer;
    std::vector<MidiEv> in;
    in.reserve(257);
    for (uint32_t i = 0; i < 256; i++)
        in.push_back(MidiEv{i, {0xB0, 21, (uint8_t)(i & 127)}});
    // At the end of block, note off:
    in.push_back(MidiEv{256, {0x80, 60, 0}});

    std::vector<MidiEv> out;
    // With 4 slots enabled, 256 CCs produce 1024 events (capacity).
    // The note-off MUST be preserved and delivered for all 4 slots.
    layer.layer(in.data(), (uint32_t)in.size(), 0xF, 0, out, 1024);
    CHECK(out.size() == 1024);

    int noteOffCount = 0;
    for (const auto& e : out) {
        if ((e.b[0] & 0xF0) == 0x80 && e.b[1] == 60)
            noteOffCount++;
    }
    CHECK(noteOffCount == 4);
}

static void test_slot_reset()
{
    MidiLayer layer;
    std::vector<MidiEv> out;
    const MidiEv on{0, {0x90, 60, 100}};
    layer.layer(&on, 1, 1, 0, out, 1024);
    CHECK(layer.noteOwners[0][60] == 1);

    layer.resetSlot(0);
    CHECK(layer.noteOwners[0][60] == 0);
    CHECK(layer.sustainOwners[0] == 0);
}

int main()
{
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_layer_slot1_default", test_layer_slot1_default},
        {"test_layer_slots_1_and_3", test_layer_slots_1_and_3},
        {"test_layer_off_silent", test_layer_off_silent},
        {"test_layer_leaves_clock", test_layer_leaves_clock},
        {"test_layer_cap", test_layer_cap},
        {"test_overlapping_note_ownership", test_overlapping_note_ownership},
        {"test_overlapping_sustain_ownership", test_overlapping_sustain_ownership},
        {"test_release_priority_truncation", test_release_priority_truncation},
        {"test_slot_reset", test_slot_reset},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    return fails != 0;
}
