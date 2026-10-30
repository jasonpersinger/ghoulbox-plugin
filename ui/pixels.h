/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <cstdint>

void screenToRgba(const uint16_t* panel, uint8_t* rgba);   // 240*240 RGB565 in the panel's byte order (high byte first in
                                                           // memory) -> 240*240*4 RGBA, opaque
