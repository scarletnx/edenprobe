// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cpuref.h"

const uint8_t CPU_CLZ8_INPUT[16] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40,
                                    0x80, 0xff, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f};
const uint16_t CPU_CLZ16_INPUT[8] = {0x0000, 0x0001, 0x0080, 0x7fff,
                                     0x8000, 0x0f00, 0x0003, 0x00ff};
const uint32_t CPU_CLZ32_INPUT[4] = {0x00000000u, 0x00010000u, 0x80000000u, 0x00000007u};

uint8_t CpuRefClz8(uint8_t value) {
    uint8_t count = 0;
    for (uint8_t bit = 0x80; bit != 0 && (value & bit) == 0; bit >>= 1) {
        ++count;
    }
    return count;
}

uint16_t CpuRefClz16(uint16_t value) {
    uint16_t count = 0;
    for (uint16_t bit = 0x8000; bit != 0 && (value & bit) == 0; bit >>= 1) {
        ++count;
    }
    return count;
}

uint32_t CpuRefClz32(uint32_t value) {
    uint32_t count = 0;
    for (uint32_t bit = 0x80000000u; bit != 0 && (value & bit) == 0; bit >>= 1) {
        ++count;
    }
    return count;
}
