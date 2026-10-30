/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel's arithmetic: knob drags and wheels, the sound list's scrolling. Pure. */
#pragma once

float dragValue(float startValue, float dyPx, float min, float max);   // 200 px up = the whole range; clamped; rounded
float wheelValue(float value, float notches, float min, float max);   // one notch = one step; clamped
int listTop(int current, int count, int rows, int top);               // keeps `current` visible; top in [0, count-rows]
int scrollTop(int top, int notchesDown, int count, int rows);       // the list's wheel: 3 rows a notch; top clamped
int cycleValue(int value, int min, int max);                          // a pill's click: the next value, wrapping
/* a parameter the window set (>= 0) that no longer says what the device plays: the device moved on (a sound load, a
 * hardware knob, a scene, undo). CC-scaled params (0..127 -> range): stale when its CC lands elsewhere than the live
 * value's pos; value params (DREAM, TYPE, TAPE): stale when it differs from the live value. -1 (unset): never */
bool ccParamStale(float param, int livePos, int range);
bool valueParamStale(float param, int live);
