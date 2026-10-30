/* SPDX-License-Identifier: GPL-3.0-only */
#include "pixels.h"
#include <cstring>

void screenToRgba(const uint16_t* panel, uint8_t* rgba)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(panel);
    for (int i = 0; i < 240 * 240; i++, b += 2, rgba += 4) {
        const uint32_t c = (uint32_t)b[0] << 8 | b[1];        // (the high byte first: whatever the CPU's order)
        const uint32_t r = c >> 11, g = (c >> 5) & 63, bl = c & 31;
        rgba[0] = (uint8_t)(r << 3 | r >> 2);
        rgba[1] = (uint8_t)(g << 2 | g >> 4);
        rgba[2] = (uint8_t)(bl << 3 | bl >> 2);
        rgba[3] = 255;
    }
}
