// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "etc2ref.h"

// Khronos Data Format Specification 1.3, ETC2 chapter: the ETC1 intensity modifier table, the
// T and H mode distance table and the alpha modifier table. The modifier for a pixel index
// (msb, lsb) is +a, +b, -a, -b for 00, 01, 10, 11.
static const int32_t INTENSITY[8][2] = {
    { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 }, { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 },
};

static const int32_t DISTANCE[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

static const int32_t ALPHA[16][8] = {
    { -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 },
    { -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 },
    { -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 },
    { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
    { -2, -6, -8, -10, 1, 5, 7, 9 },  { -2, -5, -8, -10, 1, 4, 7, 9 },
    { -2, -4, -8, -10, 1, 3, 7, 9 },  { -2, -5, -7, -10, 1, 4, 6, 9 },
    { -3, -4, -7, -10, 2, 3, 6, 9 },  { -1, -2, -3, -10, 0, 1, 2, 9 },
    { -4, -6, -8, -9, 3, 5, 7, 8 },   { -3, -5, -7, -9, 2, 4, 6, 8 },
};

static uint64_t Word(const uint8_t block[8]) {
    uint64_t word = 0;
    for (uint32_t i = 0; i < 8u; ++i) {
        word = (word << 8) | block[i];
    }
    return word;
}

static void Store(uint8_t block[8], uint64_t word) {
    for (uint32_t i = 0; i < 8u; ++i) {
        block[i] = (uint8_t)(word >> (56u - 8u * i));
    }
}

static uint32_t Bits(uint64_t word, uint32_t high, uint32_t low) {
    return (uint32_t)((word >> low) & ((1ull << (high - low + 1u)) - 1u));
}

static uint64_t Field(uint32_t value, uint32_t high, uint32_t low) {
    return ((uint64_t)value & ((1ull << (high - low + 1u)) - 1u)) << low;
}

static uint8_t Clamp(int32_t v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static int32_t Extend4(uint32_t v) {
    return (int32_t)((v << 4) | v);
}

static int32_t Extend5(uint32_t v) {
    return (int32_t)((v << 3) | (v >> 2));
}

static int32_t Extend6(uint32_t v) {
    return (int32_t)((v << 2) | (v >> 4));
}

static int32_t Extend7(uint32_t v) {
    return (int32_t)((v << 1) | (v >> 6));
}

static int32_t Signed3(uint32_t v) {
    return v >= 4u ? (int32_t)v - 8 : (int32_t)v;
}

// The pixel index of texel (x, y): its msb in bits 31..16 and its lsb in bits 15..0, both at
// bit x * 4 + y.
static uint32_t PixelIndex(uint64_t word, uint32_t x, uint32_t y) {
    const uint32_t k = x * 4u + y;
    return (Bits(word, 16u + k, 16u + k) << 1) | Bits(word, k, k);
}

static uint64_t PackIndices(const uint8_t indices[16]) {
    uint64_t word = 0;
    for (uint32_t y = 0; y < 4u; ++y) {
        for (uint32_t x = 0; x < 4u; ++x) {
            const uint32_t k = x * 4u + y;
            const uint32_t index = indices[y * 4u + x] & 3u;
            word |= (uint64_t)(index >> 1) << (16u + k);
            word |= (uint64_t)(index & 1u) << k;
        }
    }
    return word;
}

static void Put(uint8_t texel[4], int32_t r, int32_t g, int32_t b, uint8_t a) {
    texel[0] = Clamp(r);
    texel[1] = Clamp(g);
    texel[2] = Clamp(b);
    texel[3] = a;
}

static void Transparent(uint8_t texel[4]) {
    texel[0] = texel[1] = texel[2] = texel[3] = 0;
}

// Individual and differential mode: two subblocks, left and right or, flipped, top and bottom.
// A non-opaque punch-through block has no +a or -a: index 0 adds nothing and index 2 is
// transparent black.
static void DecodeSubblocks(uint64_t word, const int32_t base[2][3], bool see_through,
                            uint8_t texels[16][4]) {
    const uint32_t table[2] = { Bits(word, 39, 37), Bits(word, 36, 34) };
    const bool flip = Bits(word, 32, 32) != 0;
    for (uint32_t y = 0; y < 4u; ++y) {
        for (uint32_t x = 0; x < 4u; ++x) {
            uint8_t *const texel = texels[y * 4u + x];
            const uint32_t index = PixelIndex(word, x, y);
            const uint32_t sub = flip ? (y >= 2u) : (x >= 2u);
            if (see_through && index == 2u) {
                Transparent(texel);
                continue;
            }
            const int32_t a = see_through ? 0 : INTENSITY[table[sub]][0];
            const int32_t b = INTENSITY[table[sub]][1];
            const int32_t modifier = index == 0u ? a : index == 1u ? b : index == 2u ? -a : -b;
            Put(texel, base[sub][0] + modifier, base[sub][1] + modifier, base[sub][2] + modifier,
                255);
        }
    }
}

// T and H mode: four paint colours chosen by the pixel index. A non-opaque punch-through block
// makes index 2 transparent black.
static void DecodePaint(uint64_t word, const int32_t paint[4][3], bool see_through,
                        uint8_t texels[16][4]) {
    for (uint32_t y = 0; y < 4u; ++y) {
        for (uint32_t x = 0; x < 4u; ++x) {
            uint8_t *const texel = texels[y * 4u + x];
            const uint32_t index = PixelIndex(word, x, y);
            if (see_through && index == 2u) {
                Transparent(texel);
                continue;
            }
            Put(texel, paint[index][0], paint[index][1], paint[index][2], 255);
        }
    }
}

static int32_t Planar(int32_t o, int32_t h, int32_t v, uint32_t x, uint32_t y) {
    const int32_t sum = (int32_t)x * (h - o) + (int32_t)y * (v - o) + 4 * o + 2;
    return sum < 0 ? 0 : sum >> 2;
}

void Etc2DecodeRgb(const uint8_t block[8], bool punchthrough, uint8_t texels[16][4]) {
    const uint64_t word = Word(block);
    const bool bit33 = Bits(word, 33, 33) != 0;
    // RGB8A1 has no individual mode: its bit 33 says whether the block is opaque.
    const bool see_through = punchthrough && !bit33;
    const int32_t r = (int32_t)Bits(word, 63, 59) + Signed3(Bits(word, 58, 56));
    const int32_t g = (int32_t)Bits(word, 55, 51) + Signed3(Bits(word, 50, 48));
    const int32_t b = (int32_t)Bits(word, 47, 43) + Signed3(Bits(word, 42, 40));

    if (!punchthrough && !bit33) {
        // Individual mode: two 4-bit bases.
        const int32_t base[2][3] = {
            { Extend4(Bits(word, 63, 60)), Extend4(Bits(word, 55, 52)),
              Extend4(Bits(word, 47, 44)) },
            { Extend4(Bits(word, 59, 56)), Extend4(Bits(word, 51, 48)),
              Extend4(Bits(word, 43, 40)) },
        };
        DecodeSubblocks(word, base, false, texels);
    } else if (r < 0 || r > 31) {
        // T mode: base1's red is split around bit 58, the distance index around bit 33.
        const int32_t base1[3] = { Extend4((Bits(word, 60, 59) << 2) | Bits(word, 57, 56)),
                                   Extend4(Bits(word, 55, 52)), Extend4(Bits(word, 51, 48)) };
        const int32_t base2[3] = { Extend4(Bits(word, 47, 44)), Extend4(Bits(word, 43, 40)),
                                   Extend4(Bits(word, 39, 36)) };
        const int32_t d = DISTANCE[(Bits(word, 35, 34) << 1) | Bits(word, 32, 32)];
        int32_t paint[4][3];
        for (uint32_t c = 0; c < 3u; ++c) {
            paint[0][c] = base1[c];
            paint[1][c] = base2[c] + d;
            paint[2][c] = base2[c];
            paint[3][c] = base2[c] - d;
        }
        DecodePaint(word, paint, see_through, texels);
    } else if (g < 0 || g > 31) {
        // H mode: the distance index's lowest bit is whether base1 >= base2.
        const int32_t base1[3] = {
            Extend4(Bits(word, 62, 59)),
            Extend4((Bits(word, 58, 56) << 1) | Bits(word, 52, 52)),
            Extend4((Bits(word, 51, 51) << 3) | (Bits(word, 49, 48) << 1) | Bits(word, 47, 47)),
        };
        const int32_t base2[3] = { Extend4(Bits(word, 46, 43)),
                                   Extend4((Bits(word, 42, 40) << 1) | Bits(word, 39, 39)),
                                   Extend4(Bits(word, 38, 35)) };
        const int32_t value1 = (base1[0] << 16) | (base1[1] << 8) | base1[2];
        const int32_t value2 = (base2[0] << 16) | (base2[1] << 8) | base2[2];
        const uint32_t index =
            (Bits(word, 34, 34) << 2) | (Bits(word, 32, 32) << 1) | (value1 >= value2 ? 1u : 0u);
        const int32_t d = DISTANCE[index];
        int32_t paint[4][3];
        for (uint32_t c = 0; c < 3u; ++c) {
            paint[0][c] = base1[c] + d;
            paint[1][c] = base1[c] - d;
            paint[2][c] = base2[c] + d;
            paint[3][c] = base2[c] - d;
        }
        DecodePaint(word, paint, see_through, texels);
    } else if (b < 0 || b > 31) {
        // Planar mode: a gradient from three colours, always opaque.
        const int32_t o[3] = {
            Extend6(Bits(word, 62, 57)),
            Extend7((Bits(word, 56, 56) << 6) | Bits(word, 54, 49)),
            Extend6((Bits(word, 48, 48) << 5) | (Bits(word, 44, 43) << 3) |
                    (Bits(word, 41, 40) << 1) | Bits(word, 39, 39)),
        };
        const int32_t h[3] = { Extend6((Bits(word, 38, 34) << 1) | Bits(word, 32, 32)),
                               Extend7(Bits(word, 31, 25)),
                               Extend6((Bits(word, 24, 24) << 5) | Bits(word, 23, 19)) };
        const int32_t v[3] = { Extend6(Bits(word, 18, 13)), Extend7(Bits(word, 12, 6)),
                               Extend6(Bits(word, 5, 0)) };
        for (uint32_t y = 0; y < 4u; ++y) {
            for (uint32_t x = 0; x < 4u; ++x) {
                Put(texels[y * 4u + x], Planar(o[0], h[0], v[0], x, y),
                    Planar(o[1], h[1], v[1], x, y), Planar(o[2], h[2], v[2], x, y), 255);
            }
        }
    } else {
        // Differential mode: a 5-bit base and a 3-bit signed delta for the second subblock.
        const int32_t base[2][3] = {
            { Extend5(Bits(word, 63, 59)), Extend5(Bits(word, 55, 51)),
              Extend5(Bits(word, 47, 43)) },
            { Extend5((uint32_t)r), Extend5((uint32_t)g), Extend5((uint32_t)b) },
        };
        DecodeSubblocks(word, base, see_through, texels);
    }
}

void Etc2DecodeRgba(const uint8_t block[16], uint8_t texels[16][4]) {
    Etc2DecodeRgb(block + 8, false, texels);
    const uint64_t word = Word(block);
    const int32_t base = (int32_t)Bits(word, 63, 56);
    const int32_t multiplier = (int32_t)Bits(word, 55, 52);
    const uint32_t table = Bits(word, 51, 48);
    for (uint32_t k = 0; k < 16u; ++k) {
        const uint32_t index = Bits(word, 47u - 3u * k, 45u - 3u * k);
        const uint32_t x = k / 4u;
        const uint32_t y = k % 4u;
        texels[y * 4u + x][3] = Clamp(base + ALPHA[table][index] * multiplier);
    }
}

void Etc2BuildIndividual(uint8_t block[8], const uint8_t base1[3], const uint8_t base2[3],
                         uint32_t table1, uint32_t table2, bool flip, const uint8_t indices[16]) {
    const uint64_t word = Field(base1[0], 63, 60) | Field(base2[0], 59, 56) |
                          Field(base1[1], 55, 52) | Field(base2[1], 51, 48) |
                          Field(base1[2], 47, 44) | Field(base2[2], 43, 40) |
                          Field(table1, 39, 37) | Field(table2, 36, 34) | Field(flip, 32, 32) |
                          PackIndices(indices);
    Store(block, word);
}

void Etc2BuildDifferential(uint8_t block[8], const uint8_t base[3], const int8_t delta[3],
                           uint32_t table1, uint32_t table2, bool flip, bool bit33,
                           const uint8_t indices[16]) {
    const uint64_t word =
        Field(base[0], 63, 59) | Field((uint32_t)delta[0], 58, 56) | Field(base[1], 55, 51) |
        Field((uint32_t)delta[1], 50, 48) | Field(base[2], 47, 43) |
        Field((uint32_t)delta[2], 42, 40) | Field(table1, 39, 37) | Field(table2, 36, 34) |
        Field(bit33, 33, 33) | Field(flip, 32, 32) | PackIndices(indices);
    Store(block, word);
}

// The free bits of a channel that has to leave the 0..31 range: a base of 0..3 with a negative
// delta, or 28..31 with a positive one, whichever the stored two-bit pieces allow.
static uint64_t Overflow(uint32_t low, uint32_t delta_low, uint32_t high, uint32_t delta_bit) {
    if (low + delta_low < 4u) {
        return Field(0u, high, high - 2u) | Field(1u, delta_bit, delta_bit);
    }
    return Field(7u, high, high - 2u) | Field(0u, delta_bit, delta_bit);
}

void Etc2BuildT(uint8_t block[8], const uint8_t base1[3], const uint8_t base2[3],
                uint32_t distance, bool bit33, const uint8_t indices[16]) {
    const uint32_t r1a = (base1[0] >> 2) & 3u;
    const uint32_t r1b = base1[0] & 3u;
    const uint64_t word = Overflow(r1a, r1b, 63, 58) | Field(r1a, 60, 59) | Field(r1b, 57, 56) |
                          Field(base1[1], 55, 52) | Field(base1[2], 51, 48) |
                          Field(base2[0], 47, 44) | Field(base2[1], 43, 40) |
                          Field(base2[2], 39, 36) | Field(distance >> 1, 35, 34) |
                          Field(bit33, 33, 33) | Field(distance & 1u, 32, 32) |
                          PackIndices(indices);
    Store(block, word);
}

void Etc2BuildH(uint8_t block[8], const uint8_t base1[3], const uint8_t base2[3],
                uint32_t distance_high, bool bit33, const uint8_t indices[16]) {
    const uint32_t g1a = (base1[1] >> 1) & 7u;
    const uint32_t g1b = base1[1] & 1u;
    const uint32_t b1a = (base1[2] >> 3) & 1u;
    const uint32_t b1b = (base1[2] >> 1) & 3u;
    const uint32_t b1c = base1[2] & 1u;
    // Red stays in range: bit 63 lifts a base whose negative delta would take it below nought.
    const uint32_t red_low = base1[0] & 15u;
    const bool lift = (int32_t)red_low + Signed3(g1a) < 0;
    const uint64_t word = Field(lift, 63, 63) | Field(red_low, 62, 59) | Field(g1a, 58, 56) |
                          Overflow((g1b << 1) | b1a, b1b, 55, 50) | Field(g1b, 52, 52) |
                          Field(b1a, 51, 51) | Field(b1b, 49, 48) | Field(b1c, 47, 47) |
                          Field(base2[0], 46, 43) | Field(base2[1] >> 1, 42, 40) |
                          Field(base2[1] & 1u, 39, 39) | Field(base2[2], 38, 35) |
                          Field(distance_high >> 1, 34, 34) | Field(bit33, 33, 33) |
                          Field(distance_high & 1u, 32, 32) | PackIndices(indices);
    Store(block, word);
}

void Etc2BuildPlanar(uint8_t block[8], const uint8_t origin[3], const uint8_t horizontal[3],
                     const uint8_t vertical[3], bool bit33) {
    const uint32_t ro = origin[0] & 63u;
    const uint32_t go = origin[1] & 127u;
    const uint32_t bo = origin[2] & 63u;
    // Red and green stay in range, blue leaves it.
    const bool lift_red = (int32_t)(ro >> 2) + Signed3(((ro & 3u) << 1) | (go >> 6)) < 0;
    const bool lift_green =
        (int32_t)((go >> 2) & 15u) + Signed3(((go & 3u) << 1) | (bo >> 5)) < 0;
    const uint64_t word =
        Field(lift_red, 63, 63) | Field(ro, 62, 57) | Field(go >> 6, 56, 56) |
        Field(lift_green, 55, 55) | Field(go & 63u, 54, 49) | Field(bo >> 5, 48, 48) |
        Overflow((bo >> 3) & 3u, (bo >> 1) & 3u, 47, 42) | Field((bo >> 3) & 3u, 44, 43) |
        Field((bo >> 1) & 3u, 41, 40) | Field(bo & 1u, 39, 39) |
        Field(horizontal[0] >> 1, 38, 34) | Field(bit33, 33, 33) |
        Field(horizontal[0] & 1u, 32, 32) | Field(horizontal[1], 31, 25) |
        Field(horizontal[2] >> 5, 24, 24) | Field(horizontal[2] & 31u, 23, 19) |
        Field(vertical[0], 18, 13) | Field(vertical[1], 12, 6) | Field(vertical[2], 5, 0);
    Store(block, word);
}

void Etc2BuildAlpha(uint8_t block[8], uint32_t base, uint32_t multiplier, uint32_t table,
                    const uint8_t indices[16]) {
    uint64_t word = Field(base, 63, 56) | Field(multiplier, 55, 52) | Field(table, 51, 48);
    for (uint32_t k = 0; k < 16u; ++k) {
        const uint32_t x = k / 4u;
        const uint32_t y = k % 4u;
        word |= (uint64_t)(indices[y * 4u + x] & 7u) << (45u - 3u * k);
    }
    Store(block, word);
}
