// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>

#include "etc2blocks.h"
#include "etc2ref.h"

// Every pixel index in every row, column and subblock (row major).
static const uint8_t ALL_FOUR[16] = { 0, 1, 2, 3, 1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2 };

// Each row runs through the four indices and each column holds one: the block a decoder that
// reads the indices along the rows gets wrong. ALL_FOUR is the same read either way.
static const uint8_t ACROSS[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };

// Bytes printed from Etc2BuildTestBlock, and the hash of what Mesa decodes from them
// (notas, tasks/ryubing-gpu/artifacts/part2/etc2/, make_etc2_table.c).
const Etc2TestBlock ETC2_TEST_BLOCK[ETC2_TEST_BLOCKS] = {
    { "individual", "an individual mode block reaches both clamps in its left subblock", 0,
      { 0xf2, 0x0d, 0x85, 0xec, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xc42d3c15u },
    { "differential", "a flipped differential block takes its second base from the delta", 0,
      { 0xa4, 0x2b, 0xff, 0x3b, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xb2dccff1u },
    { "T", "a T mode block paints its four colours, the distance clamped at both ends", 0,
      { 0x1c, 0x39, 0xf1, 0x0f, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xf0af65b1u },
    { "H-high", "an H mode block whose first base is the larger adds 1 to the distance index",
      0,
      { 0x73, 0x05, 0x1c, 0xe6, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x65c1112du },
    { "H-low", "an H mode block whose first base is the smaller keeps the distance index even",
      0,
      { 0x9c, 0xfa, 0x73, 0x16, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x302a5c21u },
    { "planar", "a planar block interpolates three colours and clamps them", 0,
      { 0x7e, 0x01, 0x04, 0x02, 0xff, 0xfa, 0xb0, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x1863abd4u },
    { "pta-diff", "a see-through punch-through block is transparent at index 2 and unmodified "
                  "at index 0", 1,
      { 0x53, 0xa6, 0xf1, 0x94, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xb312e959u },
    { "pta-t", "a see-through punch-through T block is transparent at index 2", 1,
      { 0x05, 0xe7, 0x84, 0xb5, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x8432f87du },
    { "pta-planar", "a punch-through planar block is opaque whatever its opaque bit says", 1,
      { 0x0b, 0x49, 0xfa, 0x50, 0x28, 0x1f, 0xff, 0xd1,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xf198a24cu },
    { "rgba", "an RGBA block takes its alpha from an 8-bit EAC block, clamped at both ends", 2,
      { 0x80, 0xf0, 0x10, 0x43, 0x4d, 0x59, 0x67, 0xdf,
        0x3a, 0xcd, 0x60, 0x5e, 0x93, 0x6c, 0x5a, 0x5a },
      0xb98999f1u },
    { "columns", "a block's pixel indices run down its columns, not along its rows", 0,
      { 0x4c, 0xb2, 0x79, 0x54, 0xff, 0x00, 0xf0, 0xf0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xc181be95u },
    { "H-equal", "an H mode block whose two bases are the same adds 1 to the distance index", 0,
      { 0x35, 0x05, 0xb5, 0x1b, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x6e68dda5u },
    { "H-green", "an H mode block orders its bases by red, then green, then blue", 0,
      { 0x41, 0xeb, 0xc1, 0x87, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x8d5c2119u },
    { "edge-high", "a differential block whose second base is 31 0 0 stays differential", 0,
      { 0xe3, 0x1d, 0x0f, 0x0e, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x002215edu },
    { "edge-low", "a differential block whose second base is 0 31 31 stays differential", 0,
      { 0x1d, 0xe3, 0xf1, 0x63, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0x28c0a015u },
    { "planar-bits", "a planar block's six bit colours repeat their top bits, 44 for 11 and "
                     "not 45", 0,
      { 0x17, 0x1b, 0x15, 0x1b, 0xc9, 0x8e, 0x80, 0xce,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xacbec35du },
    { "pta-h", "a see-through punch-through H block is transparent at index 2", 1,
      { 0x6a, 0x14, 0x95, 0x71, 0x93, 0x6c, 0x5a, 0x5a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      0xdf635ebdu },
    { "rgba-flat", "an RGBA block whose alpha multiplier is 0 is its base alpha throughout", 2,
      { 0x5a, 0x0d, 0x10, 0x43, 0x4d, 0x59, 0x67, 0xdf,
        0x67, 0x4a, 0xa3, 0xa7, 0x93, 0x6c, 0x5a, 0x5a },
      0x388a73e5u },
};

void Etc2BuildTestBlock(uint32_t index, uint8_t bytes[16]) {
    memset(bytes, 0, 16);
    switch (index) {
    case 0: {
        const uint8_t base1[3] = { 0xF, 0x0, 0x8 };
        const uint8_t base2[3] = { 0x2, 0xD, 0x5 };
        Etc2BuildIndividual(bytes, base1, base2, 7, 3, false, ALL_FOUR);
        break;
    }
    case 1: {
        const uint8_t base[3] = { 20, 5, 31 };
        const int8_t delta[3] = { -4, 3, -1 };
        Etc2BuildDifferential(bytes, base, delta, 1, 6, true, true, ALL_FOUR);
        break;
    }
    case 2: {
        const uint8_t base1[3] = { 0xC, 0x3, 0x9 };
        const uint8_t base2[3] = { 0xF, 0x1, 0x0 };
        Etc2BuildT(bytes, base1, base2, 7, true, ALL_FOUR);
        break;
    }
    case 3: {
        const uint8_t base1[3] = { 0xE, 0x6, 0x2 };
        const uint8_t base2[3] = { 0x3, 0x9, 0xC };
        Etc2BuildH(bytes, base1, base2, 2, true, ALL_FOUR);
        break;
    }
    case 4: {
        const uint8_t base1[3] = { 0x3, 0x9, 0xC };
        const uint8_t base2[3] = { 0xE, 0x6, 0x2 };
        Etc2BuildH(bytes, base1, base2, 2, true, ALL_FOUR);
        break;
    }
    case 5: {
        const uint8_t origin[3] = { 63, 0, 32 };
        const uint8_t horizontal[3] = { 0, 127, 63 };
        const uint8_t vertical[3] = { 21, 64, 0 };
        Etc2BuildPlanar(bytes, origin, horizontal, vertical, true);
        break;
    }
    case 6: {
        const uint8_t base[3] = { 10, 20, 30 };
        const int8_t delta[3] = { 3, -2, 1 };
        Etc2BuildDifferential(bytes, base, delta, 4, 5, false, false, ALL_FOUR);
        break;
    }
    case 7: {
        const uint8_t base1[3] = { 0x1, 0xE, 0x7 };
        const uint8_t base2[3] = { 0x8, 0x4, 0xB };
        Etc2BuildT(bytes, base1, base2, 3, false, ALL_FOUR);
        break;
    }
    case 8: {
        const uint8_t origin[3] = { 5, 100, 60 };
        const uint8_t horizontal[3] = { 40, 20, 3 };
        const uint8_t vertical[3] = { 63, 127, 17 };
        Etc2BuildPlanar(bytes, origin, horizontal, vertical, false);
        break;
    }
    case 9: {
        uint8_t alpha[16];
        for (uint32_t i = 0; i < 16u; ++i) {
            alpha[i] = (uint8_t)(i & 7u);
        }
        Etc2BuildAlpha(bytes, 0x80, 15, 0, alpha);
        const uint8_t base[3] = { 7, 25, 12 };
        const int8_t delta[3] = { 2, -3, 0 };
        Etc2BuildDifferential(bytes + 8, base, delta, 2, 7, false, true, ALL_FOUR);
        break;
    }
    case 10: {
        const uint8_t base1[3] = { 0x4, 0xB, 0x7 };
        const uint8_t base2[3] = { 0xC, 0x2, 0x9 };
        Etc2BuildIndividual(bytes, base1, base2, 2, 5, false, ACROSS);
        break;
    }
    case 11: {
        // The same base twice: (66, aa, 33) >= itself, so the index is (1 << 1) | 1, distance 16.
        const uint8_t base[3] = { 0x6, 0xA, 0x3 };
        Etc2BuildH(bytes, base, base, 1, true, ALL_FOUR);
        break;
    }
    case 12: {
        // Red the same, green smaller and blue larger in the first base: (88, 22, ff) is below
        // (88, 33, 00), so the index stays (3 << 1), distance 41.
        const uint8_t base1[3] = { 0x8, 0x2, 0xF };
        const uint8_t base2[3] = { 0x8, 0x3, 0x0 };
        Etc2BuildH(bytes, base1, base2, 3, true, ALL_FOUR);
        break;
    }
    case 13: {
        const uint8_t base[3] = { 28, 3, 1 };
        const int8_t delta[3] = { 3, -3, -1 };
        Etc2BuildDifferential(bytes, base, delta, 0, 3, false, true, ALL_FOUR);
        break;
    }
    case 14: {
        const uint8_t base[3] = { 3, 28, 30 };
        const int8_t delta[3] = { -3, 3, 1 };
        Etc2BuildDifferential(bytes, base, delta, 3, 0, true, true, ALL_FOUR);
        break;
    }
    case 15: {
        // Six bit codes 11 to 15 and 48 to 52 are the ones whose bits repeated are not the code
        // over 63 rounded: 11 is 44 where that is 45, 50 is 203 where it is 202.
        const uint8_t origin[3] = { 11, 77, 50 };
        const uint8_t horizontal[3] = { 13, 100, 49 };
        const uint8_t vertical[3] = { 52, 3, 14 };
        Etc2BuildPlanar(bytes, origin, horizontal, vertical, true);
        break;
    }
    case 16: {
        const uint8_t base1[3] = { 0xD, 0x5, 0x1 };
        const uint8_t base2[3] = { 0x2, 0xA, 0xE };
        Etc2BuildH(bytes, base1, base2, 1, false, ALL_FOUR);
        break;
    }
    default: {
        uint8_t alpha[16];
        for (uint32_t i = 0; i < 16u; ++i) {
            alpha[i] = (uint8_t)(i & 7u);
        }
        Etc2BuildAlpha(bytes, 0x5A, 0, 13, alpha);
        const uint8_t base[3] = { 12, 9, 20 };
        const int8_t delta[3] = { -1, 2, 3 };
        Etc2BuildDifferential(bytes + 8, base, delta, 5, 1, true, true, ALL_FOUR);
        break;
    }
    }
}
