/* SPDX-License-Identifier: GPL-3.0-only */
/* Task 5: the host's transport as MIDI clock (F8 FA FC), each pulse on its frame. */
#include "../host/clock_gen.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

/* run blocks of `frames` from ppq `from` at `bpm`; absolute frames of each byte `b` */
static std::vector<double> run(ClockGen& g, double from, double bpm, double rate, uint32_t frames, int blocks,
                               uint8_t b, uint64_t* t0)
{
    std::vector<double> at;
    double ppq = from;
    for (int k = 0; k < blocks; k++) {
        std::vector<MidiEv> out;
        g.block(HostTime{true, ppq, bpm}, frames, rate, out);
        for (auto& e : out)
            if (e.b[0] == b)
                at.push_back((double)*t0 + e.frame);
        *t0 += frames;
        ppq += frames * bpm / 60.0 / rate;
    }
    return at;
}

static void test_pulse_spacing()
{
    ClockGen g;
    uint64_t t = 0;
    auto p = run(g, 0, 120, 48000, 512, 4, 0xF8, &t);
    CHECK(p.size() == 3);                           /* 2048 frames: pulses at 0, 1000, 2000 */
    for (size_t k = 0; k < p.size(); k++)
        CHECK(p[k] == k * 1000.0);
}

static void test_start_stop()
{
    ClockGen g;
    std::vector<MidiEv> out;
    g.block(HostTime{false, 0, 120}, 512, 48000, out);
    CHECK(out.empty());                             /* stopped: nothing */
    int fa = 0, fc = 0;
    double ppq = 0;
    for (int k = 0; k < 8; k++) {
        out.clear();
        g.block(HostTime{true, ppq, 120}, 512, 48000, out);
        if (k == 0)
            CHECK(!out.empty() && out[0].b[0] == 0xFA && out[0].frame == 0);
        for (auto& e : out)
            fa += e.b[0] == 0xFA, fc += e.b[0] == 0xFC;
        ppq += 512 * 2.0 / 48000;
    }
    for (int k = 0; k < 3; k++) {
        out.clear();
        g.block(HostTime{false, ppq, 120}, 512, 48000, out);
        if (k == 0)
            CHECK(out.size() == 1 && out[0].b[0] == 0xFC && out[0].frame == 0);
        for (auto& e : out) {
            fa += e.b[0] == 0xFA, fc += e.b[0] == 0xFC;
            CHECK(e.b[0] != 0xF8);
        }
    }
    CHECK(fa == 1 && fc == 1);
}

static void test_loop_jump_restarts()
{
    ClockGen g;
    std::vector<MidiEv> out;
    g.block(HostTime{true, 15.99, 120}, 512, 48000, out);
    out.clear();
    g.block(HostTime{true, 0.0, 120}, 512, 48000, out);   /* the loop's start again */
    CHECK(out.size() >= 3);
    CHECK(out[0].b[0] == 0xFC && out[0].frame == 0);
    CHECK(out[1].b[0] == 0xFA && out[1].frame == 0);
    CHECK(out[2].b[0] == 0xF8 && out[2].frame == 0);     /* ppq 0: a pulse at once */
    out.clear();                                          /* and on in time, no second restart */
    g.block(HostTime{true, 512 * 2.0 / 48000, 120}, 1024, 48000, out);
    CHECK(out.size() == 1 && out[0].b[0] == 0xF8 && out[0].frame == 1000 - 512);
}

static void test_small_drift_no_restart()
{
    ClockGen g;
    std::vector<MidiEv> out;
    g.block(HostTime{true, 0, 120}, 512, 48000, out);
    out.clear();
    g.block(HostTime{true, 512 * 2.0 / 48000 + 0.01, 120}, 512, 48000, out);   /* under half a pulse off */
    for (auto& e : out)
        CHECK(e.b[0] == 0xF8);
}

static void test_tempo_change()
{
    ClockGen g;
    uint64_t t = 0;
    auto a = run(g, 0, 120, 48000, 512, 8, 0xF8, &t);
    double ppq = 8 * 512 * 2.0 / 48000;
    auto b = run(g, ppq, 90, 48000, 512, 16, 0xF8, &t);
    CHECK(a.size() >= 2 && b.size() >= 3);
    for (size_t k = 1; k < b.size(); k++)
        CHECK(std::fabs(b[k] - b[k - 1] - 48000.0 * 60 / 90 / 24) <= 1);
}

/* 3 quarter notes into a 6/8 song, as DPF's wrappers lay it out (ticksPerBeat 1920) */
static void test_quarters_clap_6_8()
{
    /* CLAP: song_pos_beats (quarters) split by the numerator alone: bar 1, beat 4 (a quarter each) */
    CHECK(std::fabs(quartersFromBbt(0, 4, 0, 1920, 8, true) - 3.0) < 1e-9);
}
static void test_quarters_vst3_6_8()
{
    /* VST3: beats of the denominator (eighths): bar 2, beat 1, the bar starting 6 eighths in */
    CHECK(std::fabs(quartersFromBbt(1920.0 * 6, 1, 0, 1920, 8, false) - 3.0) < 1e-9);
}
static void test_quarters_4_4_same_either_way()
{
    CHECK(std::fabs(quartersFromBbt(1920.0 * 4, 3, 960, 1920, 4, true) - 6.5) < 1e-9);
    CHECK(std::fabs(quartersFromBbt(1920.0 * 4, 3, 960, 1920, 4, false) - 6.5) < 1e-9);
}

int main()
{
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_pulse_spacing", test_pulse_spacing},
        {"test_start_stop", test_start_stop},
        {"test_loop_jump_restarts", test_loop_jump_restarts},
        {"test_small_drift_no_restart", test_small_drift_no_restart},
        {"test_tempo_change", test_tempo_change},
        {"test_quarters_clap_6_8", test_quarters_clap_6_8},
        {"test_quarters_vst3_6_8", test_quarters_vst3_6_8},
        {"test_quarters_4_4_same_either_way", test_quarters_4_4_same_either_way},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    return fails != 0;
}
