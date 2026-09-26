// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <string.h>

#include "etc2blocks.h"
#include "etc2ref.h"

// Every pixel index in every row, column and subblock (row major).
static const uint8_t ALL_FOUR[16] = { 0, 1, 2, 3, 1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2 };

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
    default: {
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
    }
}
