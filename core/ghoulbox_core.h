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

#ifdef __cplusplus
}
#endif
#endif
