// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../check.h"
#include "../cpuref.h"
#include "../report.h"
#include "tests.h"

// The cpu suite's answers, checked on the host against numbers written out by hand, so a case
// that fails on an emulator cannot be the reference agreeing with a translator it shares a bug
// with.

static const char *const SUITE = "reference";

// By hand: the position of each input's highest set bit, counted from the top of the lane.
static const uint8_t CLZ8_EXPECTED[16] = {8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1};
static const uint16_t CLZ16_EXPECTED[8] = {16, 15, 8, 1, 0, 4, 14, 8};
static const uint32_t CLZ32_EXPECTED[4] = {32, 15, 0, 29};

void RunReferenceCpu(void) {
    int wrong = -1;
    for (int i = 0; i < 16 && wrong < 0; ++i) {
        if (CpuRefClz8(CPU_CLZ8_INPUT[i]) != CLZ8_EXPECTED[i]) {
            wrong = i;
        }
    }
    CHECK_TRUE(SUITE, "clz of each 8-bit lane the cpu suite uses", CHECK_NONE, wrong < 0,
               "lane %d: %u gave %u, by hand %u", wrong, wrong < 0 ? 0u : CPU_CLZ8_INPUT[wrong],
               wrong < 0 ? 0u : CpuRefClz8(CPU_CLZ8_INPUT[wrong]),
               wrong < 0 ? 0u : CLZ8_EXPECTED[wrong]);

    wrong = -1;
    for (int i = 0; i < 8 && wrong < 0; ++i) {
        if (CpuRefClz16(CPU_CLZ16_INPUT[i]) != CLZ16_EXPECTED[i]) {
            wrong = i;
        }
    }
    CHECK_TRUE(SUITE, "clz of each 16-bit lane the cpu suite uses", CHECK_NONE, wrong < 0,
               "lane %d: %u gave %u, by hand %u", wrong, wrong < 0 ? 0u : CPU_CLZ16_INPUT[wrong],
               wrong < 0 ? 0u : CpuRefClz16(CPU_CLZ16_INPUT[wrong]),
               wrong < 0 ? 0u : CLZ16_EXPECTED[wrong]);

    wrong = -1;
    for (int i = 0; i < 4 && wrong < 0; ++i) {
        if (CpuRefClz32(CPU_CLZ32_INPUT[i]) != CLZ32_EXPECTED[i]) {
            wrong = i;
        }
    }
    CHECK_TRUE(SUITE, "clz of each 32-bit lane the cpu suite uses", CHECK_NONE, wrong < 0,
               "lane %d: %u gave %u, by hand %u", wrong,
               wrong < 0 ? 0u : (unsigned)CPU_CLZ32_INPUT[wrong],
               wrong < 0 ? 0u : (unsigned)CpuRefClz32(CPU_CLZ32_INPUT[wrong]),
               wrong < 0 ? 0u : (unsigned)CLZ32_EXPECTED[wrong]);

    // The upper half of the 16-bit register has to hold answers a lost half cannot fake: a
    // translator that drops lanes 4-7 and subtracts the widening leaves 0xfff0 there.
    int fakeable = 0;
    for (int i = 4; i < 8; ++i) {
        if (CLZ16_EXPECTED[i] == 0xfff0u || CLZ16_EXPECTED[i] == 0u) {
            ++fakeable;
        }
    }
    CHECK_TRUE(SUITE, "the 16-bit upper lanes hold answers a lost half cannot give", CHECK_NONE,
               fakeable < 4, "all four upper lanes expect 0 or 0xfff0");

    // By hand: 0.3, 1024.5, 2.5 and -0.7 to nearest, ties to even, are 0, 1024, 2 and -1, each
    // with a fraction; 2.5, -2.5, 0.5 and 1.4 with ties away are 3, -3, 1 and 1.
    static const uint64_t FRINTX_EXPECTED[4] = {0x0000000000000000ull, 0x4090000000000000ull,
                                                0x4000000000000000ull, 0xbff0000000000000ull};
    static const uint64_t FRINTA_EXPECTED[4] = {0x4008000000000000ull, 0xc008000000000000ull,
                                                0x3ff0000000000000ull, 0x3ff0000000000000ull};
    wrong = -1;
    for (int i = 0; i < 8 && wrong < 0; ++i) {
        const bool away = i >= 4;
        const uint64_t in = away ? CPU_FRINTA_INPUT[i - 4] : CPU_FRINTX_INPUT[i];
        const uint64_t want = away ? FRINTA_EXPECTED[i - 4] : FRINTX_EXPECTED[i];
        bool inexact;
        if (CpuRefRoundDouble(in, away, &inexact) != want || !inexact) {
            wrong = i;
        }
    }
    // A whole number, a zero and an exact half below one: nothing to round, and a tie.
    bool whole_inexact = true;
    bool half_inexact = false;
    const bool exact_right = CpuRefRoundDouble(0x4008000000000000ull, false, &whole_inexact) ==
                                 0x4008000000000000ull &&
                             !whole_inexact &&
                             CpuRefRoundDouble(0xbfe0000000000000ull, false, &half_inexact) ==
                                 0x8000000000000000ull &&
                             half_inexact;
    CHECK_TRUE(SUITE, "frintx and frinta's roundings the cpu suite uses", CHECK_NONE,
               wrong < 0 && exact_right, "input %d rounds wrong; 3.0 and -0.5 %s", wrong,
               exact_right ? "right" : "wrong");

    // By hand, lane by lane: 8B by 3 of 49 fe 32 2c 79 02 cb 8e is ff 00 ff ff ff 10 00 00; 4H by
    // 4 of 0fff 1000 7fff 8000 is fff0 ffff ffff 0000; 2D by 0 keeps the positive half and
    // zeroes the negative one; D 0xbf800000 by 24 fits. Every vector saturates, the scalar not.
    static const uint64_t SQSHLU_EXPECTED[5] = {0x000010ffffff00ffull, 0x0000fffffffffff0ull,
                                                0x0000000000000000ull, 0x362758dfbfb5c4dbull,
                                                0x00bf800000000000ull};
    static const unsigned ESIZE[5] = {8, 16, 64, 64, 64};
    static const int SHIFT_OF[5] = {0, 1, 2, 2, 3};
    static const bool SATURATES[5] = {true, true, true, false, false};
    wrong = -1;
    for (int i = 0; i < 5 && wrong < 0; ++i) {
        bool saturated;
        if (CpuRefSqshlu(CPU_SQSHLU_INPUT[i], ESIZE[i], CPU_SQSHLU_SHIFT[SHIFT_OF[i]],
                         &saturated) != SQSHLU_EXPECTED[i] ||
            saturated != SATURATES[i]) {
            wrong = i;
        }
    }
    CHECK_TRUE(SUITE, "sqshlu's lanes and saturation the cpu suite uses", CHECK_NONE, wrong < 0,
               "register %d shifts wrong", wrong);

    // By hand, times 2^64 toward zero: 0.25 and -0.5 are 2^62 and -2^63, exactly the most
    // negative integer; 0.75 unsigned is 3 * 2^62 and 2^-63 is 2; 1.0 and -1.0 saturate.
    static const uint64_t FCVTZ_EXPECTED[8] = {
        0x4000000000000000ull, 0x8000000000000000ull, 0xc000000000000000ull, 0x0000000000000002ull,
        0x7fffffffffffffffull, 0x8000000000000000ull, 0xffffffffffffffffull, 0x0000000000000000ull};
    static const int SOURCE[8] = {0, 1, 2, 3, 4, 5, 4, 5};
    static const bool UNSIGNED[8] = {false, false, true, true, false, false, true, true};
    wrong = -1;
    for (int i = 0; i < 8 && wrong < 0; ++i) {
        if (CpuRefFcvtzFixed64(CPU_FCVTZ_INPUT[SOURCE[i]], UNSIGNED[i]) != FCVTZ_EXPECTED[i]) {
            wrong = i;
        }
    }
    CHECK_TRUE(SUITE, "fcvtzs and fcvtzu's lanes the cpu suite uses", CHECK_NONE, wrong < 0,
               "lane %d converts wrong", wrong);

    // The SM4 inputs from their definitions: CK byte j of word i is (4i + j) * 7 mod 256, first
    // byte highest; the key is the standard's, exclusive-or FK; the plaintext is the key.
    static const uint32_t KEY[4] = {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u};
    static const uint32_t FK[4] = {0xa3b1bac6u, 0x56aa3350u, 0x677d9197u, 0xb27022dcu};
    wrong = -1;
    for (int i = 0; i < 4 && wrong < 0; ++i) {
        uint32_t ck = 0;
        for (int j = 0; j < 4; ++j) {
            ck = (ck << 8) | (uint32_t)(((4 * i + j) * 7) & 0xff);
        }
        if (CPU_SM4_CK[i] != ck || CPU_SM4_KEY[i] != (KEY[i] ^ FK[i]) ||
            CPU_SM4_PLAIN[i] != KEY[i]) {
            wrong = i;
        }
    }
    CHECK_TRUE(SUITE, "sm4's inputs are the standard's key, FK and CK", CHECK_NONE, wrong < 0,
               "word %d differs", wrong);
}
