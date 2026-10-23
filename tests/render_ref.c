/* SPDX-License-Identifier: GPL-3.0-only */
/* native_check.c's gesture script, through libghoulbox_core.so: its 2000 ms recording, float32 L R interleaved, to OUT.
 * tests/identical_test.sh compares it with the firmware's own native_check. */
#include <stdio.h>
#include "../core/ghoulbox_core.h"

void web_nor_erase(void);
void web_boot(void);
void web_test_heavy(void);
void web_buttons(uint32_t labels);
#define B_PLAY 10u                                   /* panel.c's button labels */

static FILE *out;
static int rec;
static float l[1u << 17], r[1u << 17];
static void render(double ms)
{
    uint32_t n = (uint32_t)(ms * 44.1 + 0.5), i;
    gb_render(l, r, n);
    for (i = 0; rec && i < n; i++) {
        float s[2] = {l[i], r[i]};
        fwrite(s, sizeof s, 1, out);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || !(out = fopen(argv[1], "wb")))
        return 2;
    web_nor_erase();
    web_boot();
    render(500);
    web_test_heavy();
    web_buttons(1u << B_PLAY);
    render(60);
    web_buttons(0);
    render(60);
    rec = 1;
    render(2000);
    fclose(out);
    return 0;
}
