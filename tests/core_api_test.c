/* SPDX-License-Identifier: GPL-3.0-only */
/* The gb_* API (Task 2). The core is compiled in (not linked) so a test can set firmware state the API does not expose
 * (the MIDI route, the selected track); each test runs in a fork of a parent that never booted: pristine globals. */
#include "../core/ghoulbox_core.c"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define MS(ms) ((uint32_t)((ms) * 44.1 + 0.5))

static float bl[1u << 18], br[1u << 18];
static float peak(uint32_t n)                       /* render n frames, their peak */
{
    float p = 0;
    uint32_t i;
    gb_render(bl, br, n);
    for (i = 0; i < n; i++) {
        float a = fabsf(bl[i]), b = fabsf(br[i]);
        p = a > p ? a : p;
        p = b > p ? b : p;
    }
    return p;
}
static void fresh(void) { gb_nor_load(0, 0); gb_boot(); }

static uint8_t blob[3840], blob2[3840];
static uint8_t *shared;                             /* a page the forks share (test_state_roundtrip) */

static void test_boot_silent(void) { fresh(); CHECK(peak(4410) < 1e-4f); }

static void test_note_sounds(void)
{
    fresh();
    gb_midi(0x90, 60, 100);
    CHECK(peak(MS(100)) > 0.01f);
    gb_midi(0x80, 60, 0);
    peak(MS(5000));                                 /* track 1's default: REL 40, reverb 90, ~4 s of tail */
    CHECK(peak(MS(100)) < 1e-3f);
}

static void roundtrip_save(void)
{
    uint32_t n;
    fresh();
    gb_midi(0xB0, 21, 0);
    peak(MS(100));
    n = gb_state_save(shared + 4, 3840);
    CHECK(n == gb_state_size() && n > 0);
    memcpy(shared, &n, 4);
}
static void roundtrip_load(void)
{
    uint32_t n;
    memcpy(&n, shared, 4);
    fresh();
    CHECK(gb_state_load(shared + 4, n) == 1);
    CHECK(gb_state_save(blob, sizeof blob) == n);
    CHECK(memcmp(blob, shared + 4, n) == 0);
}

static void test_state_rejects_garbage(void)
{
    uint32_t n;
    fresh();
    gb_midi(0xB0, 21, 0);
    peak(MS(100));
    n = gb_state_save(blob, sizeof blob);
    CHECK(n > 0);
    gb_track_cc(0, 22, 99);                         /* move the music on, so a wrongly accepted load would show */
    peak(MS(100));
    CHECK(gb_state_save(blob2, sizeof blob2) == n);
    {
        uint8_t z[3840] = {0}, f[3840];
        CHECK(gb_state_load(z, n) == 0);
        CHECK(gb_state_load(blob, n - 1) == 0);
        memcpy(f, blob, n);
        f[n / 2] ^= 0x01;
        CHECK(gb_state_load(f, n) == 0);
    }
    CHECK(gb_state_save(blob, sizeof blob) == n);
    CHECK(memcmp(blob, blob2, n) == 0);
}

static void test_track_cc(void)
{
    int16_t p1[P_COUNT], p3[P_COUNT];
    uint32_t n;
    fresh();
    song.g[G_ROUTE] = 1;                            /* MIDI IN: SEL */
    song.sel = 0;                                   /* track 1 selected */
    peak(MS(100));
    n = gb_state_save(blob, sizeof blob);
    memcpy(p1, trk[0].p, sizeof p1);               /* the parameters: the rest of track_t is runtime state */
    memcpy(p3, trk[2].p, sizeof p3);
    CHECK(gb_track_cc(2, 21, 0) == 1);
    peak(MS(100));
    CHECK(gb_state_save(blob2, sizeof blob2) == n);
    CHECK(memcmp(blob, blob2, n) != 0);             /* the music changed: */
    CHECK(memcmp(p3, trk[2].p, sizeof p3) != 0);    /* track 3's */
    CHECK(memcmp(p1, trk[0].p, sizeof p1) == 0);    /* track 1 alone */
    CHECK(gb_track_cc(0, 3, 64) == 0);              /* CC3: not in the map */
}

static void test_clock_follows(void)
{
    double next = 0, pulse = 500.0 * 44.1 / 24.0;   /* 120 BPM: 24 pulses a quarter */
    uint32_t at = 0, end = MS(4000);
    fresh();
    gb_clock_external(1);
    peak(MS(20));
    CHECK(gb_midi(0xFA, 0, 0) == 1);
    while (at < end) {                              /* F8 on its frame, the audio between */
        uint32_t m = (uint32_t)next - at;
        if (m)
            peak(m);
        at += m;
        CHECK(gb_midi(0xF8, 0, 0) == 1);
        next += pulse;
        if (at > MS(100))
            CHECK(gb_playing() == 1);
    }
    CHECK(gb_midi(0xFC, 0, 0) == 1);
    peak(MS(50));
    CHECK(gb_playing() == 0);
}

/* the autosave (a change, then 10 s quiet) writes the flash only inside gb_nor_private's span */
static uint8_t before[0x100000];
static void test_autosave_within_private(void)
{
    uint32_t off = 0, len = 0, i, changed = 0;
    fresh();
    peak(MS(100));
    memcpy(before, gb_nor(), gb_nor_size());
    gb_track_cc(0, 23, 77);
    for (i = 0; i < 15; i++)                        /* (peak's buffer holds ~5 s) */
        peak(MS(1000));
    gb_nor_private(&off, &len);
    CHECK(len > 0 && off + len <= gb_nor_size());
    for (i = 0; i < gb_nor_size(); i++)
        if (gb_nor()[i] != before[i]) {
            changed++;
            CHECK(i >= off && i < off + len);
        }
    CHECK(changed > 0);
}

static int run(const char *name, void (*fn)(void))
{
    int st;
    pid_t p;
    fflush(stdout);
    p = fork();
    if (!p) {
        fn();
        exit(0);
    }
    waitpid(p, &st, 0);
    st = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
    printf("%s %s\n", st ? "FAIL" : "ok  ", name);
    return st != 0;
}

int main(void)
{
    int f = 0;
    shared = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    f += run("test_boot_silent", test_boot_silent);
    f += run("test_note_sounds", test_note_sounds);
    f += run("test_state_roundtrip (save)", roundtrip_save);
    f += run("test_state_roundtrip (fresh process: load, re-save)", roundtrip_load);
    f += run("test_state_rejects_garbage", test_state_rejects_garbage);
    f += run("test_track_cc", test_track_cc);
    f += run("test_clock_follows", test_clock_follows);
    f += run("test_autosave_within_private", test_autosave_within_private);
    printf(f ? "FAIL: %d\n" : "PASS: all\n", f);
    return f != 0;
}
