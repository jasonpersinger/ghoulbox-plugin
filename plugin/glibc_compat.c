/* SPDX-License-Identifier: GPL-3.0-only */
/* Older Linux: the plugin runs on glibc 2.34 (Ubuntu 22.04, Debian 12) though it is built on a newer one. Four
 * functions a new toolchain binds to new symbol versions are sent here (the linker's --wrap, plugin/Makefile) and
 * on to their original versions, which behave the same for every input the plugin gives them:
 *   atan2f, acosf: glibc 2.43 re-versioned them (NanoVG's drawing uses them);
 *   __isoc23_strtol, __isoc23_strtoul: C23's strtol / strtoul (glibc 2.38), which only add 0b binary prefixes to the
 *   plain ones (strtoul: the statically linked libstdc++'s, GB-004). */
#undef _GNU_SOURCE
#include <math.h>
#include <stdlib.h>

__asm__(".symver atan2f, atan2f@GLIBC_2.2.5");
__asm__(".symver acosf, acosf@GLIBC_2.2.5");
__asm__(".symver strtol, strtol@GLIBC_2.2.5");
__asm__(".symver strtoul, strtoul@GLIBC_2.2.5");

float __wrap_atan2f(float y, float x) { return atan2f(y, x); }
float __wrap_acosf(float x) { return acosf(x); }
long __wrap___isoc23_strtol(const char *s, char **end, int base) { return strtol(s, end, base); }
unsigned long __wrap___isoc23_strtoul(const char *s, char **end, int base) { return strtoul(s, end, base); }
