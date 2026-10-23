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

#ifdef __cplusplus
}
#endif
#endif
