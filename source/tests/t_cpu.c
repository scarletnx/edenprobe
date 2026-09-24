// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../cpuref.h"
#include "../report.h"
#include "tests.h"

// The processor's own vector instructions, run on the console and compared bit for bit against
// the reference the host pinned. No device: an emulator's translator is what gets judged. Each
// case loads a whole register from memory, so the translator cannot fold the answer away.

static const char *const SUITE = "cpu";

static void Clz8(const uint8_t in[16], uint8_t out[16]) {
    __asm__ volatile("ld1 {v0.16b}, [%[in]]\n"
                     "clz v1.16b, v0.16b\n"
                     "st1 {v1.16b}, [%[out]]\n"
                     :
                     : [in] "r"(in), [out] "r"(out)
                     : "v0", "v1", "memory");
}

static void Clz16(const uint16_t in[8], uint16_t out[8]) {
    __asm__ volatile("ld1 {v0.8h}, [%[in]]\n"
                     "clz v1.8h, v0.8h\n"
                     "st1 {v1.8h}, [%[out]]\n"
                     :
                     : [in] "r"(in), [out] "r"(out)
                     : "v0", "v1", "memory");
}

static void Clz32(const uint32_t in[4], uint32_t out[4]) {
    __asm__ volatile("ld1 {v0.4s}, [%[in]]\n"
                     "clz v1.4s, v0.4s\n"
                     "st1 {v1.4s}, [%[out]]\n"
                     :
                     : [in] "r"(in), [out] "r"(out)
                     : "v0", "v1", "memory");
}

void RunCpu(void) {
    char seen[160];
    size_t used;

    uint8_t out8[16];
    Clz8(CPU_CLZ8_INPUT, out8);
    int wrong = 0;
    used = 0;
    for (int i = 0; i < 16; ++i) {
        wrong += out8[i] != CpuRefClz8(CPU_CLZ8_INPUT[i]);
        used += (size_t)snprintf(seen + used, sizeof(seen) - used, " %u", out8[i]);
    }
    CHECK_TRUE(SUITE, "clz counts each of sixteen 8-bit lanes", CHECK_EXACT, wrong == 0,
               "%d lanes wrong, got%s", wrong, seen);

    uint16_t out16[8];
    Clz16(CPU_CLZ16_INPUT, out16);
    wrong = 0;
    used = 0;
    for (int i = 0; i < 8; ++i) {
        wrong += out16[i] != CpuRefClz16(CPU_CLZ16_INPUT[i]);
        used += (size_t)snprintf(seen + used, sizeof(seen) - used, " 0x%04x", out16[i]);
    }
    CHECK_TRUE(SUITE, "clz counts each of eight 16-bit lanes", CHECK_EXACT, wrong == 0,
               "%d lanes wrong, got%s", wrong, seen);

    uint32_t out32[4];
    Clz32(CPU_CLZ32_INPUT, out32);
    wrong = 0;
    used = 0;
    for (int i = 0; i < 4; ++i) {
        wrong += out32[i] != CpuRefClz32(CPU_CLZ32_INPUT[i]);
        used += (size_t)snprintf(seen + used, sizeof(seen) - used, " %u", (unsigned)out32[i]);
    }
    CHECK_TRUE(SUITE, "clz counts each of four 32-bit lanes", CHECK_EXACT, wrong == 0,
               "%d lanes wrong, got%s", wrong, seen);
}
