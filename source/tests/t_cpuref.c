// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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
}
