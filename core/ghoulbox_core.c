/* SPDX-License-Identifier: GPL-3.0-only */
/* The GHOULBOX firmware as a native library: web/emu/felucca_web.c (the unchanged firmware against a simulated FM-1),
 * as web/emu/native_check.c builds it, plus the gb_* API (ghoulbox_core.h). Plugin-only behaviour lives here, never in
 * the firmware's files. Built with -fvisibility=hidden: only gb_* and felucca_web.c's web_* exports are visible. */
#include "ghoulbox_core.h"                  /* first: hostsim.c blanks __attribute__ for the firmware's sources */

/* MENU > CALIBRATION (ui_input.c panel_setup) waits in a loop for presses that, in here, could only come between
 * renders: it would hang the host's audio thread. Its call sites are redirected to a refusal; its definition and
 * prototype stay as they are (unused). `panel_setup(void)` -> GB_PS_void, `panel_setup()` -> GB_PS_ */
static void gb_calibration_refused(void);
#define GB_PS_void gb_fw_panel_setup(void)
#define GB_PS_ gb_calibration_refused()
#define panel_setup(...) GB_PS_ ## __VA_ARGS__
#include "web/emu/felucca_web.c"
#undef panel_setup
#undef GB_PS_
#undef GB_PS_void

typedef char gb_chk_buttons[GB_NBTN == NB ? 1 : -1];
typedef char gb_chk_encoders[GB_NENC == NE ? 1 : -1];

static void gb_calibration_refused(void)
{
    ui_message("NOT IN PLUGIN");
    ui.force = 1;
}

/* A button or key released before the firmware's next millisecond (ui_input) saw it held would be no tap at all
 * (ui_input.c btn_hold reads fm1_in.buttons, a key its note's state): such a release waits, and gb_render steps a
 * frame at a time until the firmware has ticked past the press, then applies it. The output depends only on the frames
 * rendered, so stepping doesn't change it. */
static uint32_t gb_btn_ms[GB_NBTN], gb_key_ms[27];
static uint32_t gb_late_btn, gb_late_keys, gb_keys_want, gb_late_ms;   /* waiting releases; their press's ms */
static void gb_late_apply(void);

uint32_t gb_sample_rate(void) { return web_sample_rate(); }

void gb_render(float *l, float *r, uint32_t n)
{
    while (n) {
        uint32_t m = n < 1024u ? n : 1024u;
        if (gb_late_btn | gb_late_keys)
            m = 1;                                  /* (until the firmware ticks past the press) */
        web_render(m);
        if ((gb_late_btn | gb_late_keys) && web_now_ms() != gb_late_ms)
            gb_late_apply();
        memcpy(l, web_out_l(), m * sizeof *l);
        memcpy(r, web_out_r(), m * sizeof *r);
        l += m;
        r += m;
        n -= m;
    }
}

void gb_nor_load(const uint8_t *img, uint32_t n)
{
    if (img && n == sizeof nor)
        memcpy(nor, img, n);
    else
        web_nor_erase();
}
const uint8_t *gb_nor(void) { return nor; }
uint32_t gb_nor_size(void) { return sizeof nor; }
uint32_t gb_flash_writes(void) { return web_flash_writes_count(); }
void gb_nor_private(uint32_t *off, uint32_t *len)       /* storage.c: the autosave's A / B sectors, side by side */
{
    *off = st_sector(OBJ_AUTOSAVE, 0);
    *len = st_sector(OBJ_AUTOSAVE, 1) + ST_SECTOR - *off;
}

void gb_boot(void)
{
    uint32_t t;
    web_boot();
    for (t = 0; t < NTRK; t++)
        track_defaults_steps(&trk[t]);
    song.playing = 0;
    song.rec = 0;
    transport_req = 0;
    while (web_now_ms() < GB_BOOT_MS)
        web_render(64);
    for (t = 0; t < NTRK; t++)
        track_defaults_steps(&trk[t]);
    song.playing = 0;
    song.rec = 0;
    transport_req = 0;
    usb.config = 1;                             /* the header's battery: external power (ui_draw.c batt_shown) */
}

uint32_t gb_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (status == 0xF8u)
        return (uint32_t)midi_enqueue(0xFu | (uint32_t)status << 8, 1u);     /* usb.c's path for a real-time byte */
    if (status < 0x80u || status >= 0xF0u)
        return 1;
    return web_midi(status, d1, d2);
}

void gb_master(uint32_t v) { web_master(v); }
void gb_clock_external(int on) { song.g[G_CLOCK] = on ? 1 : 0; }   /* 1: USB, the host's clock */

int gb_track_cc(uint32_t track, uint8_t cc, uint8_t value)
{
    uint32_t i;
    if (track >= NTRK)
        return 0;
    for (i = 0; i < NMIDI_CC; i++)
        if (MIDI_CC_MAP[i].cc == cc) {
            midi_cc_q[track][i] = (uint8_t)((value & 127u) + 1u);   /* applied by the main loop as a real CC */
            return 1;
        }
    return 0;
}

uint32_t gb_state_size(void) { return web_project_size(); }
uint32_t gb_state_save(uint8_t *out, uint32_t cap)
{
    uint32_t n = web_project_size();
    const uint8_t *p;
    if (cap < n || !(p = web_project_capture()))
        return 0;
    memcpy(out, p, n);
    return n;
}
int gb_state_load(const uint8_t *in, uint32_t n)
{
    uint32_t t;
    if (n > web_project_import_capacity())
        return 0;
    memcpy(web_project_import_buffer(), in, n);
    if (!web_project_restore(n))
        return 0;
    for (t = 0; t < NTRK; t++)
        track_defaults_steps(&trk[t]);
    song.playing = 0;
    song.rec = 0;
    song.g[G_DREAM] = 0;
    transport_req = 0;
    return 1;
}
uint32_t gb_playing(void) { return 0; }

/* ------------------------------------------------- the window (Plan 2) --- */
static const uint8_t GB_G_ID[GB_NG] = {G_DREAM, G_DDENS, G_DDRFT, G_DMOOD, G_RTYPE, G_TAPE};

static void gb_str(char *dst, const char *src, uint32_t cap)          /* cut to cap - 1, always ended */
{
    uint32_t i;
    for (i = 0; src && src[i] && i + 1u < cap; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

/* the CC value (0..127) that midi_cc_apply turns into v: its place in the shown order through the CC's scaling;
 * the nearest place when the range is wider than 128 */
static uint8_t gb_pos(const param_desc_t *d, int32_t v)
{
    const int32_t range = d->max - d->min, want = enum_rank(d, v) - d->min;
    int32_t q, best = 0, gap = 0x7FFFFFFF;
    if (range <= 0)
        return 0;
    for (q = 0; q < 128; q++) {
        int32_t at = (range * q + 63) / 127, g = at > want ? at - want : want - at;
        if (g < gap)
            gap = g, best = q;
        if (!g)
            break;
    }
    return (uint8_t)best;
}

static void gb_ctl_fill(gb_ctl *c, const param_desc_t *d, int32_t v)
{
    char val[16], text[24];
    const char *unit;
    param_format(d, v, val, &unit);
    gb_str(text, val, sizeof text);
    gb_str(text + str_len(text), unit, sizeof text - str_len(text));
    gb_str(c->label, d->label, sizeof c->label);
    gb_str(c->text, text, sizeof c->text);
    c->value = (int16_t)v;
    c->min = d->min;
    c->max = d->max;
    c->pos = gb_pos(d, v);
}

static void gb_sound_name(uint32_t e, uint32_t k, char *out)        /* list entry e, k (NENGINES: user preset k) */
{
    char b[16];
    if (e == NENGINES)
        up_name(k, b);
    else
        gb_str(b, ENGINES[e]->npresets ? ENGINES[e]->presets[k % ENGINES[e]->npresets].name : ENGINES[e]->name, sizeof b);
    gb_str(out, b, GB_NAME);
}

void gb_view_read(gb_view *v)
{
    uint32_t t, k, total, cur;
    const track_t *s = &trk[song.sel % NTRK];
    v->draws = web_screen_draws();
    v->lit = web_lit_buttons();
    v->dim = web_dim_buttons();
    v->sel = song.sel;
    v->playing = 0;
    v->bpm = song.g[G_BPM];
    for (t = 0; t < NTRK; t++) {
        const track_t *x = &trk[t];
        const uint32_t u = user_of(x);
        gb_sound_name(u < UP_SLOTS ? NENGINES : x->eng_req % NENGINES, u < UP_SLOTS ? u : x->preset, v->track[t].sound);
        gb_ctl_fill(&v->track[t].level, track_desc(x, P_LEVEL), x->p[P_LEVEL]);
        v->track[t].mute = x->p[P_MUTE] != 0;
    }
    for (k = 0; k < 4u; k++) {
        const uint32_t id = ENGINES[s->eng_req % NENGINES]->knob[k];
        gb_ctl_fill(&v->knob[k], track_desc(s, id), s->p[id]);
    }
    for (k = 0; k < GB_NG; k++)
        gb_ctl_fill(&v->global[k], &GP[GB_G_ID[k]], song.g[GB_G_ID[k]]);
    v->step_page = 0;
    v->step_now = 255;
    for (k = 0; k < 16u; k++) {
        v->step[k].on = 0;
        v->step[k].tie = 0;
    }
    cur = preset_all_pos(&total);
    if (total > GB_SOUNDS_MAX)
        total = GB_SOUNDS_MAX;
    for (k = 0; k < total; k++) {
        uint32_t p, e = preset_all_at(k, &p);
        gb_sound_name(e, p, v->sound[k]);
    }
    v->nsounds = (uint16_t)total;
    v->sound_now = (uint16_t)(cur < total ? cur : total);
}

const uint16_t *gb_screen(void) { return web_screen(); }

void gb_button(uint32_t b, int down)
{
    if (b >= GB_NBTN || b == GB_BTN_PLAY || b == GB_BTN_REC || b == GB_BTN_SEQ)
        return;
    gb_late_btn &= ~(1u << b);
    if (down) {
        gb_btn_ms[b] = web_now_ms();
        web_buttons(web_btn_labels | 1u << b);
    } else if ((web_btn_labels >> b & 1u) && gb_btn_ms[b] == web_now_ms()) {
        gb_late_btn |= 1u << b;                     /* pressed this millisecond: released after the next */
        gb_late_ms = web_now_ms();
    } else {
        web_buttons(web_btn_labels & ~(1u << b));
    }
}
void gb_enc(uint32_t role, int32_t steps) { web_enc(role, steps); }
void gb_keys(uint32_t mask)
{
    const uint32_t now = web_now_ms(), added = mask & ~web_key_mask, removed = web_key_mask & ~mask;
    uint32_t k, late = 0;
    for (k = 0; k < 27u; k++) {
        if (added >> k & 1u)
            gb_key_ms[k] = now;
        if ((removed >> k & 1u) && gb_key_ms[k] == now)
            late |= 1u << k;                        /* pressed this millisecond: released after the next */
    }
    gb_late_keys = late;
    gb_keys_want = mask;
    if (late)
        gb_late_ms = now;
    web_keys(mask | late);
}

static void gb_late_apply(void)
{
    if (gb_late_btn) {
        web_buttons(web_btn_labels & ~gb_late_btn);
        gb_late_btn = 0;
    }
    if (gb_late_keys) {
        gb_late_keys = 0;
        web_keys(gb_keys_want);
    }
}
void gb_track_select(uint32_t t) { track_select(t); }

void gb_sound_load(uint32_t i)                      /* ui.c preset_go, over the ALL list */
{
    uint32_t total, k, e;
    preset_all_pos(&total);
    if (i >= total)
        return;
    e = preset_all_at(i, &k);
    if (e == NENGINES) {
        up_load(k);
    } else {
        if (e != TSEL->eng_req)
            select_engine(e);
        apply_preset(k);
    }
    preset_hinted();
}

int32_t gb_midi_base(void) { return midi_base(); }   /* seq.c: G_ROUTE's block */

int gb_global_set(uint32_t g, int32_t v)            /* midi_cc_apply's MC_G, with the value itself */
{
    const param_desc_t *d;
    if (g >= GB_NG)
        return 0;
    d = &GP[GB_G_ID[g]];
    song.g[GB_G_ID[g]] = (int16_t)(v < d->min ? d->min : v > d->max ? d->max : v);
    ui.force = 1;
    return 1;
}
int gb_global_desc(uint32_t g, char label[8], int16_t *min, int16_t *max, const char *const **names)
{
    const param_desc_t *d;
    if (g >= GB_NG)
        return 0;
    d = &GP[GB_G_ID[g]];
    gb_str(label, d->label, 8);
    *min = d->min;
    *max = d->max;
    *names = d->fmt == F_ENUM ? d->names : 0;
    return 1;
}
