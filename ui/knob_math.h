/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel's arithmetic: knob drags and wheels, the sound list's scrolling. Pure. */
#pragma once

float dragValue(float startValue, float dyPx, float min, float max);   // 200 px up = the whole range; clamped; rounded
float wheelValue(float value, float notches, float min, float max);   // one notch = one step; clamped
int listTop(int current, int count, int rows, int top);               // keeps `current` visible; top in [0, count-rows]
