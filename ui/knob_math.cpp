/* SPDX-License-Identifier: GPL-3.0-only */
#include "knob_math.h"
#include <algorithm>
#include <cmath>

static float clampRound(float v, float min, float max) { return std::min(max, std::max(min, std::round(v))); }

float dragValue(float startValue, float dyPx, float min, float max)
{
    return clampRound(startValue - dyPx * (max - min) / 200.f, min, max);
}

float wheelValue(float value, float notches, float min, float max) { return clampRound(value + notches, min, max); }

int listTop(int current, int count, int rows, int top)
{
    if (count <= rows)
        return 0;
    if (current < count) {
        if (current < top)
            top = current;
        else if (current >= top + rows)
            top = current - rows + 1;
    }
    return std::min(count - rows, std::max(0, top));
}
