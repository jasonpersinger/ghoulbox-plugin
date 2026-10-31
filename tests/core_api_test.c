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

static void test_boot_clean_patterns(void)
{
    gb_view v;
    uint32_t t, k;
    fresh();
    for (t = 0; t < 4; t++) {
        gb_track_select(t);
        gb_view_read(&v);
        for (k = 0; k < 16; k++)
            CHECK(!v.step[k].on);
    }
}

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
        CHECK(gb_playing() == 0);                   /* transport start FA does not start sequencer */
    }
    CHECK(gb_midi(0xFC, 0, 0) == 1);
    peak(MS(50));
    CHECK(gb_playing() == 0);
}

static void test_no_sequencer(void)
{
    gb_view v;
    uint32_t i;
    fresh();
    gb_clock_external(1);
    gb_button(GB_BTN_PLAY, 1);
    peak(MS(20));
    CHECK(fm1_in.buttons == 0);
    gb_button(GB_BTN_PLAY, 0);
    peak(MS(20));
    CHECK(gb_playing() == 0);

    gb_button(GB_BTN_REC, 1);
    peak(MS(20));
    CHECK(fm1_in.buttons == 0);
    gb_button(GB_BTN_REC, 0);
    CHECK(song.rec == 0);

    gb_button(GB_BTN_SEQ, 1);
    peak(MS(20));
    CHECK(fm1_in.buttons == 0);
    gb_button(GB_BTN_SEQ, 0);

    /* Transport running and notes held: gb_view.playing stays 0 and no steps fire */
    gb_midi(0xFA, 0, 0);
    gb_midi(0x90, 60, 100);
    for (i = 0; i < 48; i++) {
        gb_midi(0xF8, 0, 0);
        peak(MS(10));
    }
    gb_view_read(&v);
    CHECK(v.playing == 0);
    CHECK(gb_playing() == 0);
    for (i = 0; i < 16; i++)
        CHECK(!v.step[i].on);
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

/* ------------------------------------------------------ Plan 2: the window's view and inputs --- */
static gb_view v;

static void test_calibration_refused(void)
{
    fresh();
    ui.menu = 1;
    ui.menu_sel = MI_PANEL - 1;                     /* RESTORE LAST, the row above CALIBRATION (SYSTEM) */
    gb_enc(GB_ENC_PRESETS, 1);                      /* onto CALIBRATION and OCT+ in the same millisecond */
    gb_button(GB_BTN_OCTUP, 1);
    peak(MS(100));                                  /* (a hang: the alarm in run() fails it) */
    gb_button(GB_BTN_OCTUP, 0);
    peak(MS(20));
    CHECK(ui.menu_sel == MI_PANEL);
    CHECK(memcmp(&panel, &PANEL_DEFAULT, sizeof panel) == 0);
    CHECK(strcmp(ui.msg, "NOT IN PLUGIN") == 0);
}

static void test_battery_external(void) { fresh(); CHECK(batt_shown() == 4); }

static void test_view_basics(void)
{
    uint32_t i, k;
    fresh();
    gb_view_read(&v);
    CHECK(v.sel == 0 && v.playing == 0 && v.bpm == song.g[G_BPM]);
    CHECK(v.nsounds >= 59 && v.nsounds <= GB_SOUNDS_MAX);
    for (i = 0; i < v.nsounds; i++)
        CHECK(v.sound[i][0] && memchr(v.sound[i], 0, GB_NAME));
    CHECK(v.sound_now < v.nsounds);
    CHECK(strcmp(v.sound[v.sound_now], v.track[0].sound) == 0);
    for (k = 0; k < 4u; k++)
        CHECK(strncmp(v.knob[k].label, track_desc(&trk[0], ENGINES[trk[0].eng_req]->knob[k])->label, 7) == 0);
}

static int32_t sound_index(const char *name)
{
    uint32_t i;
    for (i = 0; i < v.nsounds; i++)
        if (strcmp(v.sound[i], name) == 0)
            return (int32_t)i;
    return -1;
}

static void test_sound_load(void)
{
    int32_t i;
    fresh();
    gb_view_read(&v);
    i = sound_index("HURDY GURDY");
    CHECK(i >= 0);
    gb_sound_load((uint32_t)i);
    peak(MS(50));
    gb_view_read(&v);
    CHECK(v.sound_now == (uint32_t)i);
    CHECK(strcmp(v.track[0].sound, "HURDY GURDY") == 0);
    CHECK(gb_state_save(blob, sizeof blob));
    gb_sound_load(v.nsounds);
    CHECK(gb_state_save(blob2, sizeof blob2));
    CHECK(memcmp(blob, blob2, sizeof blob) == 0);
}

static void test_knob_pos_roundtrip(void)
{
    uint32_t k, j;
    fresh();
    for (k = 0; k < 4u; k++) {
        const uint32_t id = ENGINES[trk[0].eng_req]->knob[k];
        const param_desc_t *d = track_desc(&trk[0], id);
        for (j = 0; j <= 8u; j++) {
            const int16_t want = (int16_t)(d->min + (d->max - d->min) * (int32_t)j / 8);
            trk[0].p[id] = want;
            gb_view_read(&v);
            gb_track_cc(0, (uint8_t)(21 + k), v.knob[k].pos);
            peak(MS(20));
            if (trk[0].p[id] != want)
                fprintf(stderr, "  knob %u (%s) %d: pos %u -> %d\n", k, d->label, want, v.knob[k].pos, trk[0].p[id]);
            CHECK(trk[0].p[id] == want);
        }
    }
}

static void test_global_set(void)
{
    fresh();
    CHECK(gb_global_set(GB_G_RTYPE, 1) == 1);
    gb_view_read(&v);
    CHECK(v.global[GB_G_RTYPE].value == 1);
    CHECK(gb_global_set(GB_G_TAPE, 80) == 1);
    gb_view_read(&v);
    CHECK(v.global[GB_G_TAPE].value == 80);
    CHECK(gb_global_set(GB_NG, 0) == 0);
}

static void test_no_dream(void)
{
    fresh();
    /* Cannot turn on through gb_global_set */
    CHECK(gb_global_set(GB_G_EVOL, 1) == 0);
    CHECK(gb_global_set(GB_G_DENS, 4) == 0);
    CHECK(gb_global_set(GB_G_DRFT, 4) == 0);
    CHECK(gb_global_set(GB_G_MOOD, 2) == 0);
    CHECK(song.g[G_DREAM] == 0);

    /* Restoring state never leaves DREAM on */
    song.g[G_DREAM] = 1;
    uint32_t n = gb_state_save(blob, sizeof blob);
    fresh();
    CHECK(gb_state_load(blob, n) == 1);
    CHECK(song.g[G_DREAM] == 0);
}

static void test_buttons_and_track_select(void)
{
    fresh();
    gb_button(GB_BTN_FX, 1);
    peak(MS(20));
    CHECK(fm1_in.buttons == 1u << panel.btn[B_FX]);
    gb_button(GB_BTN_FX, 0);
    peak(MS(20));
    CHECK(fm1_in.buttons == 0);
    gb_track_select(2);
    peak(MS(20));
    gb_view_read(&v);
    CHECK(v.sel == 2);
}

/* Review I1: a press and its release in one host block of 32 frames (a touchpad tap, a stalled host) still reach the
 * firmware as a tap, whatever the block's phase against the firmware's millisecond */
static void test_quick_tap_reaches_firmware(void)
{
    uint32_t off;
    for (off = 0; off < 128u; off += 4u) {
        fresh();
        if (off)
            gb_render(bl, br, off);
        ui.menu = 1;
        peak(MS(30));
        gb_button(GB_BTN_HOME, 1);
        gb_render(bl, br, 32);
        gb_button(GB_BTN_HOME, 0);
        gb_render(bl, br, 32);
        peak(MS(100));
        if (ui.menu)
            fprintf(stderr, "  offset %u: the HOME tap was lost\n", off);
        CHECK(ui.menu == 0);
    }
}
static void test_quick_key_plays(void)
{
    uint32_t off;
    for (off = 0; off < 128u; off += 8u) {
        fresh();
        if (off)
            gb_render(bl, br, off);
        gb_keys(1u << 7);                           /* C4 */
        gb_render(bl, br, 32);
        gb_keys(0);
        gb_render(bl, br, 32);
        {
            const float pk = peak(MS(100));             /* the note sounded (its release tail, at least) */
            if (pk <= 0.01f)
                fprintf(stderr, "  offset %u: the key tap was silent (peak %g)\n", off, pk);
            CHECK(pk > 0.01f);
        }
    }
}

/* MIDI layering (plugin): the channels that reach the tracks follow MIDI IN (G_ROUTE) */
static void test_midi_base(void)
{
    fresh();
    CHECK(gb_midi_base() == 0);                     /* CH1-4, the default */
    song.g[G_ROUTE] = 2;
    CHECK(gb_midi_base() == 4);                     /* CH5-8 */
    song.g[G_ROUTE] = 1;
    CHECK(gb_midi_base() == -1);                    /* SEL */
}

static int run(const char *name, void (*fn)(void))
{
    int st;
    pid_t p;
    fflush(stdout);
    p = fork();
    if (!p) {
        alarm(60);                                  /* a hang fails (SIGALRM: not a normal exit) */
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
    f += run("test_boot_clean_patterns", test_boot_clean_patterns);
    f += run("test_note_sounds", test_note_sounds);
    f += run("test_state_roundtrip (save)", roundtrip_save);
    f += run("test_state_roundtrip (fresh process: load, re-save)", roundtrip_load);
    f += run("test_state_rejects_garbage", test_state_rejects_garbage);
    f += run("test_track_cc", test_track_cc);
    f += run("test_clock_follows", test_clock_follows);
    f += run("test_no_sequencer", test_no_sequencer);
    f += run("test_autosave_within_private", test_autosave_within_private);
    f += run("test_calibration_refused", test_calibration_refused);
    f += run("test_battery_external", test_battery_external);
    f += run("test_view_basics", test_view_basics);
    f += run("test_sound_load", test_sound_load);
    f += run("test_knob_pos_roundtrip", test_knob_pos_roundtrip);
    f += run("test_global_set", test_global_set);
    f += run("test_no_dream", test_no_dream);
    f += run("test_buttons_and_track_select", test_buttons_and_track_select);
    f += run("test_quick_tap_reaches_firmware", test_quick_tap_reaches_firmware);
    f += run("test_quick_key_plays", test_quick_key_plays);
    f += run("test_midi_base", test_midi_base);
    printf(f ? "FAIL: %d\n" : "PASS: all\n", f);
    return f != 0;
}
