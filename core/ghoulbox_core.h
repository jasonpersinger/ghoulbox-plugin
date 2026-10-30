/* SPDX-License-Identifier: GPL-3.0-only */
/* The gb_* API: the only boundary between the plugin and the GHOULBOX firmware core (libghoulbox_core.so). */
#ifndef GHOULBOX_CORE_H
#define GHOULBOX_CORE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_API __attribute__((visibility("default")))

GB_API uint32_t gb_sample_rate(void);                       /* the core's rate (44100) */
GB_API void gb_render(float *l, float *r, uint32_t n);      /* n frames, any n */

/* the flash (1 MiB NOR): its image before gb_boot (n != gb_nor_size(): erased), read back, and its write count */
GB_API void gb_nor_load(const uint8_t *img, uint32_t n);
GB_API const uint8_t *gb_nor(void);
GB_API uint32_t gb_nor_size(void);
GB_API uint32_t gb_flash_writes(void);
GB_API void gb_nor_private(uint32_t *off, uint32_t *len);   /* the instance's own flash, never shared: the autosave
                                                             * (its current music, which the host's project keeps) */
#define GB_BOOT_MS 4000u
GB_API void gb_boot(void);           /* power on, then render-and-discard to GB_BOOT_MS: past the splash and the intro */
GB_API uint32_t gb_midi(uint8_t status, uint8_t d1, uint8_t d2);   /* channel messages; F8 FA FB FC as USB clock;
                                                                    * others ignored. 0 = the ring was full */
GB_API void gb_master(uint32_t v);   /* MASTER, 0..1023 */
GB_API void gb_clock_external(int on);                       /* GLO CLOCK: USB (the host's clock) or INT */
GB_API int gb_track_cc(uint32_t track, uint8_t cc, uint8_t value);  /* a MIDI_CC_MAP CC on track 0..3, whatever the
                                                                    * MIDI route; 0 = not in the map */
GB_API uint32_t gb_state_size(void);
GB_API uint32_t gb_state_save(uint8_t *out, uint32_t cap);   /* bytes written, 0 = failed */
GB_API int gb_state_load(const uint8_t *in, uint32_t n);     /* 1 ok; 0 rejected, the running music untouched */
GB_API uint32_t gb_playing(void);

/* ------------------------------------------------- the window (Plan 2) --- */
enum { GB_BTN_FX, GB_BTN_SCL, GB_BTN_ENV, GB_BTN_LFO, GB_BTN_EDIT, GB_BTN_GLO, GB_BTN_HOME, GB_BTN_SAVE, GB_BTN_ARP,
       GB_BTN_SEQ, GB_BTN_PLAY, GB_BTN_REC, GB_BTN_OCTDN, GB_BTN_OCTUP, GB_NBTN };          /* = panel.c B_* */
enum { GB_ENC_SELECT, GB_ENC_ALGO, GB_ENC_PRESETS, GB_ENC_K1, GB_ENC_K2, GB_ENC_K3, GB_ENC_K4, GB_NENC };   /* = EN_* */
enum { GB_G_EVOL, GB_G_DENS, GB_G_DRFT, GB_G_MOOD, GB_G_RTYPE, GB_G_TAPE, GB_NG };   /* -> G_DREAM G_DDENS G_DDRFT
                                                                                      * G_DMOOD G_RTYPE G_TAPE */
#define GB_NAME 14
#define GB_SOUNDS_MAX 96          /* 59 shown factory sounds + UP_SLOTS (32) user presets */
typedef struct { char label[8], text[8]; int16_t value, min, max; uint8_t pos; } gb_ctl;
    /* text: params.c param_format's value + unit; pos: 0..127, the CC value that lands on `value` through
     * MIDI_CC_MAP's scaling in the shown (enum_order) order: midi_cc_apply's inverse */
typedef struct {
    uint32_t draws;               /* web_screen_draws(): the screen changed when this did */
    uint32_t lit, dim;            /* web_lit_buttons(), web_dim_buttons(): bit = GB_BTN_*, bit 14 = PLAY's green */
    uint8_t sel, playing;         /* song.sel, song.playing */
    int16_t bpm;                  /* song.g[G_BPM] */
    struct { char sound[GB_NAME]; gb_ctl level; uint8_t mute; } track[4];   /* sound: the user preset's or factory
                                                                            * preset's name; level: P_LEVEL */
    gb_ctl knob[4];               /* the selected track's KNOB 1..4 */
    gb_ctl global[GB_NG];
    struct { uint8_t on, tie; } step[16];   /* on: time != ST_REST && (n || hit); tie: time == ST_TIE */
    uint8_t step_page, step_now;  /* the 16 shown: page seq_idx / 16 while playing, else 0; step_now 0..15, 255 = none */
    uint16_t nsounds, sound_now;  /* the PRESETS list (ALL mode); sound_now: the selected track's index, nsounds if none */
    char sound[GB_SOUNDS_MAX][GB_NAME];
} gb_view;
GB_API void gb_view_read(gb_view *v);
GB_API const uint16_t *gb_screen(void);          /* 240*240 RGB565 as the panel gets it (high byte first) */
GB_API void gb_button(uint32_t b, int down);     /* by label; keeps the other buttons' state */
GB_API void gb_enc(uint32_t role, int32_t steps);
GB_API void gb_keys(uint32_t mask);              /* the 27 keys, bit k = F3 + k semitones */
GB_API void gb_track_select(uint32_t t);
GB_API void gb_sound_load(uint32_t i);           /* list index i into the selected track, as the PRESETS knob; i >= the
                                                  * list's length: nothing */
GB_API int gb_global_set(uint32_t g, int32_t v); /* into its range; 0 = no such g */
GB_API int gb_global_desc(uint32_t g, char label[8], int16_t *min, int16_t *max, const char *const **names);

#ifdef __cplusplus
}
#endif
#endif
