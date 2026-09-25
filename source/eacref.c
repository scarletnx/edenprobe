// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include "eacref.h"

// Khronos Data Format Specification 1.3, table "Intensity modifier sets for alpha component"
// (the ETC2 alpha table, which R11 EAC shares).
static const int32_t MODIFIERS[16][8] = {
    { -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 },
    { -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 },
    { -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 },
    { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
    { -2, -6, -8, -10, 1, 5, 7, 9 },  { -2, -5, -8, -10, 1, 4, 7, 9 },
    { -2, -4, -8, -10, 1, 3, 7, 9 },  { -2, -5, -7, -10, 1, 4, 6, 9 },
    { -3, -4, -7, -10, 2, 3, 6, 9 },  { -1, -2, -3, -10, 0, 1, 2, 9 },
    { -4, -6, -8, -9, 3, 5, 7, 8 },   { -3, -5, -7, -9, 2, 4, 6, 8 },
};

void EacDecodeUnsigned11(const uint8_t block[8], uint16_t values[16]) {
    uint64_t word = 0;
    for (uint32_t i = 0; i < 8u; ++i) {
        word = (word << 8) | block[i];
    }
    const int32_t base = (int32_t)(word >> 56);
    const int32_t multiplier = (int32_t)((word >> 52) & 0xFu);
    const uint32_t table = (uint32_t)((word >> 48) & 0xFu);
    for (uint32_t k = 0; k < 16u; ++k) {
        const uint32_t index = (uint32_t)((word >> (45u - 3u * k)) & 7u);
        const int32_t modifier = MODIFIERS[table][index];
        int32_t v = multiplier != 0 ? base * 8 + 4 + modifier * multiplier * 8
                                    : base * 8 + 4 + modifier;
        v = v < 0 ? 0 : v > 2047 ? 2047 : v;
        const uint32_t x = k / 4u;
        const uint32_t y = k % 4u;
        values[y * 4u + x] = (uint16_t)v;
    }
}

void EacUnorm8Answers(uint16_t v, uint8_t *direct, uint8_t *extended) {
    *direct = (uint8_t)(((uint32_t)v * 255u + 1023u) / 2047u);
    const uint32_t v16 = ((uint32_t)v << 5) | ((uint32_t)v >> 6);
    *extended = (uint8_t)((v16 * 255u + 32767u) / 65535u);
}

void EacBuildBlock(uint8_t block[8], uint32_t base, uint32_t multiplier, uint32_t table,
                   const uint8_t indices[16]) {
    uint64_t word = ((uint64_t)(base & 0xFFu) << 56) | ((uint64_t)(multiplier & 0xFu) << 52) |
                    ((uint64_t)(table & 0xFu) << 48);
    for (uint32_t k = 0; k < 16u; ++k) {
        const uint32_t x = k / 4u;
        const uint32_t y = k % 4u;
        word |= (uint64_t)(indices[y * 4u + x] & 7u) << (45u - 3u * k);
    }
    for (uint32_t i = 0; i < 8u; ++i) {
        block[i] = (uint8_t)(word >> (56u - 8u * i));
    }
}
