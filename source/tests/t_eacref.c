// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include "../check.h"
#include "../eacref.h"
#include "../report.h"
#include "tests.h"

// The eac suite's expected values, checked against numbers worked out by hand from the
// specification's formula: base * 8 + 4 + modifier * multiplier * 8, clamped to 0..2047, with a
// multiplier of nought adding the bare modifier (one eighth of a step). The same reading as
// Mesa's texcompress_etc.c, which agreed with source/eacref.c on a million random blocks.

static const char *const SUITE = "reference";

void RunReferenceEac(void) {
    uint8_t indices[16];
    for (uint32_t i = 0; i < 16u; ++i) {
        indices[i] = (uint8_t)(i & 7u);
    }
    uint8_t block[8];
    uint16_t v[16];
    // Base 0x80, multiplier 11, table 0 {-3 -6 -9 -15 2 5 8 14}: index 0 gives 1028 - 264 = 764,
    // index 3 gives 1028 - 1320, clamped to 0, index 7 gives 1028 + 1232, clamped to 2047.
    EacBuildBlock(block, 0x80, 11, 0, indices);
    EacDecodeUnsigned11(block, v);
    CHECK_TRUE(SUITE, "an EAC texel is base * 8 + 4 plus modifier * multiplier * 8, clamped",
               CHECK_NONE, v[0] == 764u && v[3] == 0u && v[7] == 2047u,
               "got %u, %u and %u", (unsigned)v[0], (unsigned)v[3], (unsigned)v[7]);
    // Base 0x30, multiplier 0, table 13 {-1 -2 -3 -10 0 1 2 9}: index 7 gives 388 + 9 = 397 and
    // index 3 gives 388 - 10 = 378.
    EacBuildBlock(block, 0x30, 0, 13, indices);
    EacDecodeUnsigned11(block, v);
    CHECK_TRUE(SUITE, "an EAC multiplier of nought adds the bare modifier", CHECK_NONE,
               v[7] == 397u && v[3] == 378u, "got %u and %u", (unsigned)v[7], (unsigned)v[3]);
    // The index of texel (x, y) is the (4x + y)th in the block, top bits first: texel (1, 0)
    // is the fifth, bits 35 to 33 of the word, which are bits 3 to 1 of its fourth byte.
    uint8_t one_hot[16] = { 0 };
    one_hot[1] = 7; // texel (1, 0), row major
    EacBuildBlock(block, 0x80, 1, 0, one_hot);
    CHECK_TRUE(SUITE, "EAC indices run down each column", CHECK_NONE,
               block[2] == 0x00u && block[3] == 0x0Eu, "bytes 2 and 3 are %02x %02x", block[2],
               block[3]);
    uint8_t direct = 0;
    uint8_t extended = 0;
    EacUnorm8Answers(2047, &direct, &extended);
    CHECK_TRUE(SUITE, "the 11-bit value 2047 is 255 either way", CHECK_NONE,
               direct == 255u && extended == 255u, "got %u and %u", direct, extended);
}
