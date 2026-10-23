/* SPDX-License-Identifier: GPL-3.0-only */
/* Task 4: the engine: events at their host frame, the core's 44.1 kHz at the host's rate. */
#include "../host/engine.h"
#include "speex_resampler.h"
#include <cmath>
#include <cstdio>
#include <vector>

static const char* LIB = "build/libghoulbox_core.so";
static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static std::unique_ptr<CoreInstance> bootCore()
{
    std::string err;
    auto c = CoreInstance::open(LIB, &err);
    if (c) {
        c->api().nor_load(nullptr, 0);
        c->api().boot();
    }
    return c;
}

/* frames of output at rate, in blocks of `block`; the note (if any) on at frame `on`, off at `off` */
static std::vector<float> play(double rate, uint32_t block, uint32_t frames, uint8_t note, uint32_t on, uint32_t off,
                               std::vector<float>* right = nullptr)
{
    Engine e(bootCore(), rate);
    std::vector<float> l(frames), r(frames);
    for (uint32_t at = 0; at < frames; at += block) {
        uint32_t n = std::min(block, frames - at);
        MidiEv ev[2];
        uint32_t k = 0;
        if (note && on >= at && on < at + n)
            ev[k++] = MidiEv{on - at, {0x90, note, 100}};
        if (note && off >= at && off < at + n)
            ev[k++] = MidiEv{off - at, {0x80, note, 0}};
        e.process(&l[at], &r[at], n, ev, k);
    }
    if (right)
        *right = r;
    return l;
}

/* the signal's period over [a, b) by autocorrelation (the strongest peak, lags of 50 Hz .. 2 kHz, interpolated), in Hz.
 * (Zero crossings swing ~1 % on the default sound's drone and buzz with no change of pitch.) */
static double pitch(const std::vector<float>& x, double rate, uint32_t a, uint32_t b)
{
    uint32_t lo = (uint32_t)(rate / 2000), hi = (uint32_t)(rate / 50), best = 0;
    std::vector<double> c(hi + 2);
    for (uint32_t L = lo; L <= hi + 1; L++)
        for (uint32_t i = a; i < b; i++)
            c[L] += (double)x[i] * x[i + L];
    for (uint32_t L = lo + 1; L <= hi; L++)
        if (c[L] >= c[L - 1] && c[L] >= c[L + 1] && (!best || c[L] > c[best]))
            best = L;
    double y0 = c[best - 1], y1 = c[best], y2 = c[best + 1];
    return rate / (best + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2));
}
static uint32_t firstSound(const std::vector<float>& x, uint32_t from)
{
    while (from < x.size() && std::fabs(x[from]) <= 1e-3f)
        from++;
    return from;
}

static void test_44k_bitexact()
{
    auto twin = bootCore();
    CHECK(twin);
    std::vector<float> r;
    auto l = play(44100, 512, 88200, 0, 0, 0, &r);
    std::vector<float> tl(88200), tr(88200);
    twin->api().render(tl.data(), tr.data(), 88200);
    CHECK(l == tl && r == tr);
}

static void test_rates_and_blocks()
{
    auto ref = play(44100, 512, 44100, 69, 0, 44100);
    double refPitch = pitch(ref, 44100, 44100 / 4, 44100 / 2);
    CHECK(refPitch > 50);
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (uint32_t block : {1u, 37u, 4096u}) {
            uint32_t n = (uint32_t)rate;
            std::vector<float> r;
            auto l = play(rate, block, n, 69, 0, n, &r);
            double p = pitch(l, rate, n / 4, n / 2);
            float big = 0, jump = 0;
            for (uint32_t i = 0; i < n; i++) {
                big = std::max(big, std::max(std::fabs(l[i]), std::fabs(r[i])));
                if (i)
                    jump = std::max(jump, std::max(std::fabs(l[i] - l[i - 1]), std::fabs(r[i] - r[i - 1])));
            }
            std::fprintf(stderr, "    %6.0f Hz x %4u: pitch %.3f Hz (44.1k: %.3f), peak %.3f, max step %.3f\n", rate, block,
                         p, refPitch, big, jump);
            CHECK(std::fabs(p / refPitch - 1) <= 0.005);
            CHECK(big <= 1.5f);
            CHECK(jump <= 0.5f);
        }
}

/* A note at host frame 3000 (48 kHz) reaches the core when it has rendered the matching 44.1 kHz frame (2756), so it
 * sounds where the core alone sounds it (its 128-frame half and the sound's attack: the hardware's timing), plus the
 * resampler's fixed latency, and no later */
static void test_event_position()
{
    const double ratio = 44100.0 / 48000.0;
    const uint32_t at = 3000, coreAt = (uint32_t)std::lround(at * ratio);
    auto core = play(44100, 4096, 8192, 60, coreAt, 99999);
    uint32_t coreDelay = firstSound(core, 0) - coreAt;
    int err = 0;
    auto* rs = speex_resampler_init(2, 44100, 48000, 7, &err);
    double lat = speex_resampler_get_output_latency(rs);
    speex_resampler_destroy(rs);
    double expect = at + coreDelay / ratio + lat;
    auto l = play(48000, 4096, 8192, 60, at, 99999);
    uint32_t first = firstSound(l, 0);
    std::fprintf(stderr, "    core delay %u (44.1k frames), resampler latency %.0f: expected ~%.0f, first sound at %u\n",
                 coreDelay, lat, expect, first);
    CHECK(first >= at && std::fabs(first - expect) <= 8);
}

int main()
{
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_44k_bitexact", test_44k_bitexact},
        {"test_rates_and_blocks", test_rates_and_blocks},
        {"test_event_position", test_event_position},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    return fails != 0;
}
