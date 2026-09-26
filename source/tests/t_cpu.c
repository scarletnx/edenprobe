// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../cpuref.h"
#include "../report.h"
#include "tests.h"

// The processor's own instructions, run on the console and compared bit for bit against the
// reference the host pinned. No device: an emulator's translator is what gets judged. Each case
// loads its operands from memory, so the translator cannot fold the answer away.

static const char *const SUITE = "cpu";

// FPSR's cumulative flags: QC, and IXC, which FRINTX raises when it changes a value.
#define FPSR_QC (1u << 27)
#define FPSR_IXC (1u << 4)

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

static void Div(const uint64_t in[4], uint64_t out[4]) {
    __asm__ volatile("ldr w2, [%[in]]\n"
                     "ldr w3, [%[in], #8]\n"
                     "ldr x4, [%[in], #16]\n"
                     "ldr x5, [%[in], #24]\n"
                     "sdiv w6, w2, w3\n"
                     "udiv w7, w2, w3\n"
                     "sdiv x8, x4, x5\n"
                     "udiv x9, x4, x5\n"
                     "stp x6, x7, [%[out]]\n"
                     "stp x8, x9, [%[out], #16]\n"
                     :
                     : [in] "r"(in), [out] "r"(out)
                     : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "memory");
}

// FPCR is set to round to nearest for the four roundings and put back after; FPSR is cleared
// first and handed back, for FRINTX's Inexact.
#define ROUND_FOUR(INSTRUCTION)                                                                    \
    __asm__ volatile("mrs x9, fpcr\n"                                                             \
                     "msr fpcr, xzr\n"                                                            \
                     "msr fpsr, xzr\n"                                                            \
                     "ldp d0, d1, [%[in]]\n"                                                      \
                     "ldp d2, d3, [%[in], #16]\n" INSTRUCTION " d0, d0\n" INSTRUCTION " d1, d1\n"  \
                     INSTRUCTION " d2, d2\n" INSTRUCTION " d3, d3\n"                               \
                     "mrs %[fpsr], fpsr\n"                                                        \
                     "msr fpcr, x9\n"                                                             \
                     "stp d0, d1, [%[out]]\n"                                                     \
                     "stp d2, d3, [%[out], #16]\n"                                                \
                     : [fpsr] "=&r"(fpsr)                                                         \
                     : [in] "r"(in), [out] "r"(out)                                               \
                     : "x9", "v0", "v1", "v2", "v3", "memory")

static uint64_t Frintx(const uint64_t in[4], uint64_t out[4]) {
    uint64_t fpsr;
    ROUND_FOUR("frintx");
    return fpsr;
}

static uint64_t Frinta(const uint64_t in[4], uint64_t out[4]) {
    uint64_t fpsr;
    ROUND_FOUR("frinta");
    return fpsr;
}

// Each shift with FPSR cleared before it and read after it, so each lane width owns its QC.
static void Sqshlu(const uint64_t in[5], uint64_t out[5], uint64_t fpsr[4]) {
    __asm__ volatile("ldr d0, [%[in]]\n"
                     "ldr d2, [%[in], #8]\n"
                     "ldr q4, [%[in], #16]\n"
                     "ldr d6, [%[in], #32]\n"
                     "msr fpsr, xzr\n"
                     "sqshlu v1.8b, v0.8b, #3\n"
                     "mrs x9, fpsr\n"
                     "msr fpsr, xzr\n"
                     "sqshlu v3.4h, v2.4h, #4\n"
                     "mrs x10, fpsr\n"
                     "msr fpsr, xzr\n"
                     "sqshlu v5.2d, v4.2d, #0\n"
                     "mrs x11, fpsr\n"
                     "msr fpsr, xzr\n"
                     "sqshlu d7, d6, #24\n"
                     "mrs x12, fpsr\n"
                     "str d1, [%[out]]\n"
                     "str d3, [%[out], #8]\n"
                     "str q5, [%[out], #16]\n"
                     "str d7, [%[out], #32]\n"
                     "stp x9, x10, [%[fpsr]]\n"
                     "stp x11, x12, [%[fpsr], #16]\n"
                     :
                     : [in] "r"(in), [out] "r"(out), [fpsr] "r"(fpsr)
                     : "x9", "x10", "x11", "x12", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
                       "memory");
}

static void Fcvtz(const uint64_t in[6], uint64_t out[8]) {
    __asm__ volatile("ldr q0, [%[in]]\n"
                     "ldr q2, [%[in], #16]\n"
                     "ldr q4, [%[in], #32]\n"
                     "fcvtzs v1.2d, v0.2d, #64\n"
                     "fcvtzu v3.2d, v2.2d, #64\n"
                     "fcvtzs v5.2d, v4.2d, #64\n"
                     "fcvtzu v6.2d, v4.2d, #64\n"
                     "str q1, [%[out]]\n"
                     "str q3, [%[out], #16]\n"
                     "str q5, [%[out], #32]\n"
                     "str q6, [%[out], #48]\n"
                     :
                     : [in] "r"(in), [out] "r"(out)
                     : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "memory");
}

// SM4 is past the console's processor, so the assembler is not asked for it: the words are
// sm4ekey v0.4s, v1.4s, v2.4s and sm4e v3.4s, v0.4s.
static void Sm4(const uint32_t key[4], const uint32_t ck[4], const uint32_t plain[4],
                uint32_t rk[4], uint32_t x[4]) {
    __asm__ volatile("ld1 {v1.4s}, [%[key]]\n"
                     "ld1 {v2.4s}, [%[ck]]\n"
                     "ld1 {v3.4s}, [%[plain]]\n"
                     ".inst 0xce62c820\n"
                     ".inst 0xcec08403\n"
                     "st1 {v0.4s}, [%[rk]]\n"
                     "st1 {v3.4s}, [%[x]]\n"
                     :
                     : [key] "r"(key), [ck] "r"(ck), [plain] "r"(plain), [rk] "r"(rk), [x] "r"(x)
                     : "v0", "v1", "v2", "v3", "memory");
}

// Each case says it started before its instruction runs, so a translator that ends the program
// on one costs that case alone: the launch after skips it and goes on with the next.
static const char *const CLZ8 = "clz counts each of sixteen 8-bit lanes";
static const char *const CLZ16 = "clz counts each of eight 16-bit lanes";
static const char *const CLZ32 = "clz counts each of four 32-bit lanes";
static const char *const DIV = "sdiv and udiv by zero give zero, 32 and 64 bits";
static const char *const FRINTX = "frintx rounds to nearest even and raises inexact";
static const char *const FRINTA = "frinta rounds halfway cases away from zero";
static const char *const SQSHLU = "sqshlu saturates 8, 16 and 64-bit lanes and sets QC";
static const char *const FCVTZ = "fcvtzs and fcvtzu with 64 fraction bits scale by 2^64";
// Last, and in the may-crash table: a translator that cannot compile SM4 ends the program.
static const char *const SM4_CASE = CPU_SM4_CASE;

void RunCpu(void) {
    char seen[160];
    size_t used;
    int wrong = 0;

    if (ReportStart(SUITE, CLZ8)) {
        uint8_t out8[16];
        Clz8(CPU_CLZ8_INPUT, out8);
        used = 0;
        for (int i = 0; i < 16; ++i) {
            wrong += out8[i] != CpuRefClz8(CPU_CLZ8_INPUT[i]);
            used += (size_t)snprintf(seen + used, sizeof(seen) - used, " %u", out8[i]);
        }
        CHECK_TRUE(SUITE, CLZ8, CHECK_EXACT, wrong == 0, "%d lanes wrong, got%s", wrong, seen);
    }

    if (ReportStart(SUITE, CLZ16)) {
        uint16_t out16[8];
        Clz16(CPU_CLZ16_INPUT, out16);
        wrong = 0;
        used = 0;
        for (int i = 0; i < 8; ++i) {
            wrong += out16[i] != CpuRefClz16(CPU_CLZ16_INPUT[i]);
            used += (size_t)snprintf(seen + used, sizeof(seen) - used, " 0x%04x", out16[i]);
        }
        CHECK_TRUE(SUITE, CLZ16, CHECK_EXACT, wrong == 0, "%d lanes wrong, got%s", wrong, seen);
    }

    if (ReportStart(SUITE, CLZ32)) {
        uint32_t out32[4];
        Clz32(CPU_CLZ32_INPUT, out32);
        wrong = 0;
        used = 0;
        for (int i = 0; i < 4; ++i) {
            wrong += out32[i] != CpuRefClz32(CPU_CLZ32_INPUT[i]);
            used += (size_t)snprintf(seen + used, sizeof(seen) - used, " %u", (unsigned)out32[i]);
        }
        CHECK_TRUE(SUITE, CLZ32, CHECK_EXACT, wrong == 0, "%d lanes wrong, got%s", wrong, seen);
    }

    // Division by zero is defined on this processor: zero, whatever the dividend.
    if (ReportStart(SUITE, DIV)) {
        uint64_t div[4];
        Div(CPU_DIV_INPUT, div);
        CHECK_TRUE(SUITE, DIV, CHECK_EXACT,
                   div[0] == 0 && div[1] == 0 && div[2] == 0 && div[3] == 0,
                   "sdiv w %016llx, udiv w %016llx, sdiv x %016llx, udiv x %016llx",
                   (unsigned long long)div[0], (unsigned long long)div[1],
                   (unsigned long long)div[2], (unsigned long long)div[3]);
    }

    uint64_t rounded[4];
    if (ReportStart(SUITE, FRINTX)) {
        uint64_t fpsr = Frintx(CPU_FRINTX_INPUT, rounded);
        wrong = 0;
        for (int i = 0; i < 4; ++i) {
            bool inexact;
            wrong += rounded[i] != CpuRefRoundDouble(CPU_FRINTX_INPUT[i], false, &inexact);
        }
        CHECK_TRUE(SUITE, FRINTX, CHECK_EXACT, wrong == 0 && (fpsr & FPSR_IXC) != 0,
                   "%d of 4 wrong, got %016llx %016llx %016llx %016llx, inexact %s", wrong,
                   (unsigned long long)rounded[0], (unsigned long long)rounded[1],
                   (unsigned long long)rounded[2], (unsigned long long)rounded[3],
                   (fpsr & FPSR_IXC) != 0 ? "raised" : "not raised");
    }

    if (ReportStart(SUITE, FRINTA)) {
        Frinta(CPU_FRINTA_INPUT, rounded);
        wrong = 0;
        for (int i = 0; i < 4; ++i) {
            bool inexact;
            wrong += rounded[i] != CpuRefRoundDouble(CPU_FRINTA_INPUT[i], true, &inexact);
        }
        CHECK_TRUE(SUITE, FRINTA, CHECK_EXACT, wrong == 0,
                   "%d of 4 wrong, got %016llx %016llx %016llx %016llx", wrong,
                   (unsigned long long)rounded[0], (unsigned long long)rounded[1],
                   (unsigned long long)rounded[2], (unsigned long long)rounded[3]);
    }

    // Lane sizes 8, 16, 64 and a scalar 64, each against the reference and each with its QC.
    if (ReportStart(SUITE, SQSHLU)) {
        static const unsigned SQSHLU_ESIZE[5] = {8, 16, 64, 64, 64};
        static const int SQSHLU_FPSR[5] = {0, 1, 2, 2, 3};
        uint64_t shifted[5];
        uint64_t flags[4];
        Sqshlu(CPU_SQSHLU_INPUT, shifted, flags);
        bool saturated[4] = {false, false, false, false};
        wrong = 0;
        for (int i = 0; i < 5; ++i) {
            bool lane_saturated;
            const uint64_t expected =
                CpuRefSqshlu(CPU_SQSHLU_INPUT[i], SQSHLU_ESIZE[i],
                             CPU_SQSHLU_SHIFT[SQSHLU_FPSR[i]], &lane_saturated);
            saturated[SQSHLU_FPSR[i]] = saturated[SQSHLU_FPSR[i]] || lane_saturated;
            wrong += shifted[i] != expected;
        }
        int qc_wrong = 0;
        for (int i = 0; i < 4; ++i) {
            qc_wrong += ((flags[i] & FPSR_QC) != 0) != saturated[i];
        }
        CHECK_TRUE(SUITE, SQSHLU, CHECK_EXACT, wrong == 0 && qc_wrong == 0,
                   "%d of 5 registers wrong, got 8b %016llx 4h %016llx 2d %016llx %016llx "
                   "d %016llx; QC %d%d%d%d, want %d%d%d%d",
                   wrong, (unsigned long long)shifted[0], (unsigned long long)shifted[1],
                   (unsigned long long)shifted[2], (unsigned long long)shifted[3],
                   (unsigned long long)shifted[4], (flags[0] & FPSR_QC) != 0,
                   (flags[1] & FPSR_QC) != 0, (flags[2] & FPSR_QC) != 0,
                   (flags[3] & FPSR_QC) != 0, saturated[0], saturated[1], saturated[2],
                   saturated[3]);
    }

    // Last of the cases every run has: a translator whose table of fraction bits stops at 63
    // calls a null pointer here, and nothing after it reports.
    if (ReportStart(SUITE, FCVTZ)) {
        static const int FCVTZ_SOURCE[8] = {0, 1, 2, 3, 4, 5, 4, 5};
        static const bool FCVTZ_UNSIGNED[8] = {false, false, true, true, false, false, true, true};
        uint64_t converted[8];
        Fcvtz(CPU_FCVTZ_INPUT, converted);
        wrong = 0;
        for (int i = 0; i < 8; ++i) {
            wrong += converted[i] !=
                     CpuRefFcvtzFixed64(CPU_FCVTZ_INPUT[FCVTZ_SOURCE[i]], FCVTZ_UNSIGNED[i]);
        }
        CHECK_TRUE(SUITE, FCVTZ, CHECK_EXACT, wrong == 0,
                   "%d of 8 lanes wrong, got %016llx %016llx, %016llx %016llx, %016llx %016llx, "
                   "%016llx %016llx",
                   wrong, (unsigned long long)converted[0], (unsigned long long)converted[1],
                   (unsigned long long)converted[2], (unsigned long long)converted[3],
                   (unsigned long long)converted[4], (unsigned long long)converted[5],
                   (unsigned long long)converted[6], (unsigned long long)converted[7]);
    }

    if (!ReportStart(SUITE, SM4_CASE)) {
        return;
    }
    uint32_t rk[4];
    uint32_t x[4];
    Sm4(CPU_SM4_KEY, CPU_SM4_CK, CPU_SM4_PLAIN, rk, x);
    wrong = 0;
    for (int i = 0; i < 4; ++i) {
        wrong += (rk[i] != CPU_SM4_RK[i]) + (x[i] != CPU_SM4_X[i]);
    }
    CHECK_TRUE(SUITE, SM4_CASE, CHECK_EXACT, wrong == 0,
               "%d of 8 words wrong, rk %08x %08x %08x %08x, x %08x %08x %08x %08x", wrong,
               (unsigned)rk[0], (unsigned)rk[1], (unsigned)rk[2], (unsigned)rk[3],
               (unsigned)x[0], (unsigned)x[1], (unsigned)x[2], (unsigned)x[3]);
}
