/* SPDX-License-Identifier: GPL-3.0-only */
/* The GHOULBOX firmware as a native library: web/emu/felucca_web.c (the unchanged firmware against a simulated FM-1),
 * as web/emu/native_check.c builds it, plus the gb_* API (ghoulbox_core.h). Plugin-only behaviour lives here, never in
 * the firmware's files. Built with -fvisibility=hidden: only gb_* and felucca_web.c's web_* exports are visible. */
#include "ghoulbox_core.h"                  /* first: hostsim.c blanks __attribute__ for the firmware's sources */
#include "web/emu/felucca_web.c"

uint32_t gb_sample_rate(void) { return web_sample_rate(); }

void gb_render(float *l, float *r, uint32_t n)
{
    while (n) {
        uint32_t m = n < 1024u ? n : 1024u;
        web_render(m);
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
    web_boot();
    while (web_now_ms() < GB_BOOT_MS)
        web_render(64);
}

uint32_t gb_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu)
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
    if (n > web_project_import_capacity())
        return 0;
    memcpy(web_project_import_buffer(), in, n);
    return web_project_restore(n) != 0;
}
uint32_t gb_playing(void) { return web_playing(); }
