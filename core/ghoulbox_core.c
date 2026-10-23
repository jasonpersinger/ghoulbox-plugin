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
