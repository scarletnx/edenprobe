// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "../check.h"
#include "../cpufpref.h"
#include "../cpugaps.h"
#include "../cpuref.h"
#include "../jitblocks.h"
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
static const char *const SM4_CASE = CPU_SM4_CASE;
static const char *const COMPILE = "48 new straight-line blocks with 33 live registers leave the "
                                   "state worked out apart";
static const char *const GAP_RSHL = "sqrshl and uqrshl round, shift and saturate lanes of every width";
static const char *const GAP_DMUL = "sqdmlal, sqdmlsl and sqdmull double, accumulate and saturate";
static const char *const GAP_RNARROW = "sqrshrn, uqrshrn and sqrshrun round and narrow scalars";
static const char *const CNTVCT =
    "mrs cntvct_el0 reads a virtual count that moves with the physical one";
static const char *const DAIF = "mrs daif reads nothing outside the four interrupt mask bits";
static const char *const FRECPE =
    "frecpe gives the Arm ARM's reciprocal estimate of singles of every class, flags too";
static const char *const FRSQRTE =
    "frsqrte gives the reciprocal square root estimate of singles of every class, flags too";
static const char *const UESTIMATE =
    "urecpe and ursqrte give the unsigned estimates, and all ones below their range";
static const char *const FRECPX_CASE =
    "frecpx gives the reciprocal exponent, the largest for noughts and denormals";
static const char *const AHP_CASE =
    "fcvtn and fcvtl with AHP take exponent 31 as a number and saturate past 131008";
static const char *const DN_CASE =
    "fcvtn keeps a NaN's payload's top bits, and gives the default NaN with DN set";
static const char *const MINMAX =
    "fmax, fmin, fmaxnm and fminnm order NaNs, noughts and denormals as the Arm ARM does";

/// Calls each block twice from JIT_INITIAL: the first call is the one a translator compiles on,
/// the second only runs. The state after each call is checked against JIT_EXPECTED; the time
/// both calls took, added over every block, goes to @p first_ns and @p second_ns. Returns the
/// number of blocks that left a wrong state and the first wrong block and word.
static unsigned CompileBlocks(uint64_t *first_ns, uint64_t *second_ns, unsigned *bad_block,
                              unsigned *bad_word) {
    static uint64_t state[JIT_STATE_WORDS] __attribute__((aligned(16)));
    unsigned bad = 0;
    *first_ns = 0;
    *second_ns = 0;
    *bad_block = JIT_BLOCKS;
    *bad_word = 0;
    // Index -1 is the warm-up: the same call site, so the loop's own code is compiled untimed.
    for (int k = -1; k < (int)JIT_BLOCKS; ++k) {
        void (*block)(uint64_t *) = k < 0 ? JitWarm : JIT_BLOCK[k];
        uint64_t took[2];
        bool wrong = false;
        for (int call = 0; call < 2; ++call) {
            memcpy(state, JIT_INITIAL, sizeof(state));
            const uint64_t start = armGetSystemTick();
            block(state);
            took[call] = armTicksToNs(armGetSystemTick() - start);
            for (unsigned w = 0; k >= 0 && w < JIT_STATE_WORDS; ++w) {
                if (w != 17u && state[w] != JIT_EXPECTED[k][w]) {  // word 17 is padding
                    if (!wrong && *bad_block == JIT_BLOCKS) {
                        *bad_block = (unsigned)k;
                        *bad_word = w;
                    }
                    wrong = true;
                }
            }
        }
        if (k >= 0) {
            bad += wrong;
            *first_ns += took[0];
            *second_ns += took[1];
        }
    }
    return bad;
}

/// The sources and accumulator each NEON gap form starts from, as cpuref.h describes them.
static void GapInputs(const CpuGapForm *form, uint64_t v0[2], uint64_t v1[2], uint64_t v2[2]) {
    v0[0] = v0[1] = v1[0] = v1[1] = v2[0] = v2[1] = 0;
    if (form->kind == CPU_GAP_KIND_RSHL) {
        memcpy(v0, CPU_RSHL_VALUE[form->input], 16);
        memcpy(v1, CPU_RSHL_SHIFT[form->input], 16);
    } else if (form->kind == CPU_GAP_KIND_DMUL) {
        memcpy(v0, CPU_DMUL_A, 16);
        memcpy(v1, CPU_DMUL_B, 16);
        memcpy(v2, CPU_DMUL_ACC, 16);
    } else {
        v0[0] = CPU_RNARROW_VALUE[form->input / 2u][form->input % 2u];
    }
}

/// Checks what each gap instruction stored, register and QC, against the reference. Returns how
/// many were wrong and describes the first in @p detail.
static unsigned GapCheck(const CpuGapForm *forms, unsigned count, const uint64_t out[][2],
                         const uint64_t *fpsr, char *detail, size_t size) {
    unsigned wrong = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint64_t v0[2], v1[2], v2[2], want[2];
        bool qc;
        GapInputs(&forms[i], v0, v1, v2);
        CpuRefGapExpected(&forms[i], v0, v1, v2, want, &qc);
        const bool got_qc = (fpsr[i] & FPSR_QC) != 0;
        if (out[i][0] != want[0] || out[i][1] != want[1] || got_qc != qc) {
            if (wrong == 0) {
                snprintf(detail, size, "%s gave %016llx%016llx QC %d, expected %016llx%016llx QC %d",
                         forms[i].text, (unsigned long long)out[i][1], (unsigned long long)out[i][0],
                         got_qc, (unsigned long long)want[1], (unsigned long long)want[0], qc);
            }
            ++wrong;
        }
    }
    return wrong;
}

/// The physical count, the virtual count twice, the physical count again.
static void ReadCounts(uint64_t counts[4]) {
    __asm__ volatile("isb\n"
                     "mrs x9, cntpct_el0\n"
                     "isb\n"
                     "mrs x10, cntvct_el0\n"
                     "isb\n"
                     "mrs x11, cntvct_el0\n"
                     "isb\n"
                     "mrs x12, cntpct_el0\n"
                     "stp x9, x10, [%[counts]]\n"
                     "stp x11, x12, [%[counts], #16]\n"
                     :
                     : [counts] "r"(counts)
                     : "x9", "x10", "x11", "x12", "memory");
}

static uint64_t ReadDaif(void) {
    uint64_t daif;
    __asm__ volatile("mrs %[daif], daif\n" : [daif] "=r"(daif));
    return daif;
}

// ---- floating point rules (cpufpref.c)

/// FPSR's five cumulative flags, which these cases compare.
#define FP_FLAGS CPU_FPSR_FLAGS

// One vector instruction over four registers of four singles, FPCR set to @p fpcr for it and put
// back after, FPSR cleared before each register and read after it, so each register owns its
// flags.
#define FOUR_REGISTERS(INSTRUCTION)                                                                \
    __asm__ volatile("mrs x9, fpcr\n"                                                             \
                     "msr fpcr, %[fpcr]\n"                                                        \
                     "ldp q0, q1, [%[in]]\n"                                                      \
                     "ldp q2, q3, [%[in], #32]\n"                                                 \
                     "msr fpsr, xzr\n" INSTRUCTION " v4.4s, v0.4s\n"                              \
                     "mrs x10, fpsr\n"                                                            \
                     "msr fpsr, xzr\n" INSTRUCTION " v5.4s, v1.4s\n"                              \
                     "mrs x11, fpsr\n"                                                            \
                     "msr fpsr, xzr\n" INSTRUCTION " v6.4s, v2.4s\n"                              \
                     "mrs x12, fpsr\n"                                                            \
                     "msr fpsr, xzr\n" INSTRUCTION " v7.4s, v3.4s\n"                              \
                     "mrs x13, fpsr\n"                                                            \
                     "msr fpcr, x9\n"                                                             \
                     "stp q4, q5, [%[out]]\n"                                                     \
                     "stp q6, q7, [%[out], #32]\n"                                                \
                     "stp x10, x11, [%[fpsr]]\n"                                                  \
                     "stp x12, x13, [%[fpsr], #16]\n"                                             \
                     :                                                                            \
                     : [in] "r"(in), [out] "r"(out), [fpsr] "r"(fpsr), [fpcr] "r"(fpcr)           \
                     : "x9", "x10", "x11", "x12", "x13", "v0", "v1", "v2", "v3", "v4", "v5",      \
                       "v6", "v7", "memory")

static void Frecpe(const uint32_t in[16], uint32_t out[16], uint64_t fpsr[4], uint64_t fpcr) {
    FOUR_REGISTERS("frecpe");
}

static void Frsqrte(const uint32_t in[16], uint32_t out[16], uint64_t fpsr[4], uint64_t fpcr) {
    FOUR_REGISTERS("frsqrte");
}

static void Urecpe(const uint32_t in[16], uint32_t out[16], uint64_t fpsr[4], uint64_t fpcr) {
    FOUR_REGISTERS("urecpe");
}

static void Ursqrte(const uint32_t in[16], uint32_t out[16], uint64_t fpsr[4], uint64_t fpcr) {
    FOUR_REGISTERS("ursqrte");
}

/// FRECPX, which has only a scalar form: eight singles, FPSR cleared before each and read after.
static void Frecpx(const uint32_t in[8], uint32_t out[8], uint64_t fpsr[8]) {
    for (int i = 0; i < 8; ++i) {
        __asm__ volatile("mrs x9, fpcr\n"
                         "msr fpcr, xzr\n"
                         "ldr s0, [%[in]]\n"
                         "msr fpsr, xzr\n"
                         "frecpx s1, s0\n"
                         "mrs x10, fpsr\n"
                         "msr fpcr, x9\n"
                         "str s1, [%[out]]\n"
                         "str x10, [%[fpsr]]\n"
                         :
                         : [in] "r"(in + i), [out] "r"(out + i), [fpsr] "r"(fpsr + i)
                         : "x9", "x10", "v0", "v1", "memory");
    }
}

/// FCVTN and FCVTN2 of eight singles into eight halves, and FCVTL and FCVTL2 of eight halves
/// into eight singles, FPCR set to @p fpcr; FPSR read after each instruction.
static void Narrow(const uint32_t in[8], uint16_t out[8], uint64_t fpsr[2], uint64_t fpcr) {
    __asm__ volatile("mrs x9, fpcr\n"
                     "msr fpcr, %[fpcr]\n"
                     "ldp q0, q1, [%[in]]\n"
                     "msr fpsr, xzr\n"
                     "fcvtn v2.4h, v0.4s\n"
                     "mrs x10, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fcvtn2 v2.8h, v1.4s\n"
                     "mrs x11, fpsr\n"
                     "msr fpcr, x9\n"
                     "str q2, [%[out]]\n"
                     "stp x10, x11, [%[fpsr]]\n"
                     :
                     : [in] "r"(in), [out] "r"(out), [fpsr] "r"(fpsr), [fpcr] "r"(fpcr)
                     : "x9", "x10", "x11", "v0", "v1", "v2", "memory");
}

static void Widen(const uint16_t in[8], uint32_t out[8], uint64_t fpsr[2], uint64_t fpcr) {
    __asm__ volatile("mrs x9, fpcr\n"
                     "msr fpcr, %[fpcr]\n"
                     "ldr q0, [%[in]]\n"
                     "msr fpsr, xzr\n"
                     "fcvtl v1.4s, v0.4h\n"
                     "mrs x10, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fcvtl2 v2.4s, v0.8h\n"
                     "mrs x11, fpsr\n"
                     "msr fpcr, x9\n"
                     "stp q1, q2, [%[out]]\n"
                     "stp x10, x11, [%[fpsr]]\n"
                     :
                     : [in] "r"(in), [out] "r"(out), [fpsr] "r"(fpsr), [fpcr] "r"(fpcr)
                     : "x9", "x10", "x11", "v0", "v1", "v2", "memory");
}

/// FMAX, FMIN, FMAXNM and FMINNM of the same two registers of pairs, in that order, FPCR clear;
/// FPSR read after each instruction on each register.
static void MinMax(const uint32_t a[8], const uint32_t b[8], uint32_t out[32], uint64_t fpsr[8]) {
    __asm__ volatile("mrs x9, fpcr\n"
                     "msr fpcr, xzr\n"
                     "ldp q0, q1, [%[a]]\n"
                     "ldp q2, q3, [%[b]]\n"
                     "msr fpsr, xzr\n"
                     "fmax v4.4s, v0.4s, v2.4s\n"
                     "mrs x10, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fmax v5.4s, v1.4s, v3.4s\n"
                     "mrs x11, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fmin v6.4s, v0.4s, v2.4s\n"
                     "mrs x12, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fmin v7.4s, v1.4s, v3.4s\n"
                     "mrs x13, fpsr\n"
                     "stp q4, q5, [%[out]]\n"
                     "stp q6, q7, [%[out], #32]\n"
                     "stp x10, x11, [%[fpsr]]\n"
                     "stp x12, x13, [%[fpsr], #16]\n"
                     "msr fpsr, xzr\n"
                     "fmaxnm v4.4s, v0.4s, v2.4s\n"
                     "mrs x10, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fmaxnm v5.4s, v1.4s, v3.4s\n"
                     "mrs x11, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fminnm v6.4s, v0.4s, v2.4s\n"
                     "mrs x12, fpsr\n"
                     "msr fpsr, xzr\n"
                     "fminnm v7.4s, v1.4s, v3.4s\n"
                     "mrs x13, fpsr\n"
                     "msr fpcr, x9\n"
                     "stp q4, q5, [%[out], #64]\n"
                     "stp q6, q7, [%[out], #96]\n"
                     "stp x10, x11, [%[fpsr], #32]\n"
                     "stp x12, x13, [%[fpsr], #48]\n"
                     :
                     : [a] "r"(a), [b] "r"(b), [out] "r"(out), [fpsr] "r"(fpsr)
                     : "x9", "x10", "x11", "x12", "x13", "v0", "v1", "v2", "v3", "v4", "v5", "v6",
                       "v7", "memory");
}

/// Holds @p count lanes at @p got, four to a register, against @p want and each register's flags
/// against the lanes' ORed; the line names the first wrong lane or register.
static void JudgeLanes(const char *name, const uint32_t *got, const uint32_t *want,
                       const uint64_t *fpsr, const uint32_t *want_flags, uint32_t count,
                       uint32_t lanes_per_flag, const uint32_t *inputs) {
    uint32_t wrong = 0, first = 0, flags_wrong = 0, first_flags = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (got[i] != want[i]) {
            first = wrong == 0u ? i : first;
            ++wrong;
        }
    }
    for (uint32_t r = 0; r < count / lanes_per_flag; ++r) {
        if ((fpsr[r] & FP_FLAGS) != want_flags[r]) {
            first_flags = flags_wrong == 0u ? r : first_flags;
            ++flags_wrong;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u && flags_wrong == 0u,
               "%u of %u lanes wrong, the first of %08x gave %08x, expected %08x; %u of %u flag "
               "sets wrong, the first %02x, expected %02x",
               (unsigned)wrong, (unsigned)count, (unsigned)inputs[first], (unsigned)got[first],
               (unsigned)want[first], (unsigned)flags_wrong, (unsigned)(count / lanes_per_flag),
               (unsigned)(fpsr[first_flags] & FP_FLAGS), (unsigned)want_flags[first_flags]);
}

/// A four register estimate case: @p run on @p in, each lane against @p ref.
static void EstimateCase(const char *name, void (*run)(const uint32_t *, uint32_t *, uint64_t *,
                                                         uint64_t),
                         const uint32_t in[16], uint32_t (*ref)(uint32_t, uint32_t *),
                         uint32_t (*plain)(uint32_t)) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    uint32_t out[16], want[16], want_flags[4] = { 0, 0, 0, 0 };
    uint64_t fpsr[4];
    run(in, out, fpsr, 0);
    for (uint32_t i = 0; i < 16u; ++i) {
        uint32_t flags = 0;
        want[i] = ref != NULL ? ref(in[i], &flags) : plain(in[i]);
        want_flags[i / 4u] |= flags;
    }
    JudgeLanes(name, out, want, fpsr, want_flags, 16u, 4u, in);
}

static void FpCases(void) {
    EstimateCase(FRECPE, Frecpe, CPU_FRECPE_INPUT, CpuRefFrecpe, NULL);
    EstimateCase(FRSQRTE, Frsqrte, CPU_FRSQRTE_INPUT, CpuRefFrsqrte, NULL);

    if (ReportStart(SUITE, UESTIMATE)) {
        // URECPE on the first two registers and URSQRTE on the same lanes, as the second two.
        uint32_t in[16], out[16], want[16], want_flags[4] = { 0, 0, 0, 0 };
        uint64_t fpsr[4], fpsr2[4];
        memcpy(in, CPU_UESTIMATE_INPUT, sizeof(CPU_UESTIMATE_INPUT));
        memcpy(in + 8, CPU_UESTIMATE_INPUT, sizeof(CPU_UESTIMATE_INPUT));
        uint32_t ursqrte[16];
        Urecpe(in, out, fpsr, 0);
        Ursqrte(in, ursqrte, fpsr2, 0);
        memcpy(out + 8, ursqrte + 8, 8u * sizeof(uint32_t));
        fpsr[2] = fpsr2[2];
        fpsr[3] = fpsr2[3];
        for (uint32_t i = 0; i < 16u; ++i) {
            want[i] = i < 8u ? CpuRefUrecpe(in[i]) : CpuRefUrsqrte(in[i]);
        }
        JudgeLanes(UESTIMATE, out, want, fpsr, want_flags, 16u, 4u, in);
    }

    if (ReportStart(SUITE, FRECPX_CASE)) {
        uint32_t out[8], want[8], want_flags[8];
        uint64_t fpsr[8];
        Frecpx(CPU_FRECPX_INPUT, out, fpsr);
        for (uint32_t i = 0; i < 8u; ++i) {
            want_flags[i] = 0;
            want[i] = CpuRefFrecpx(CPU_FRECPX_INPUT[i], &want_flags[i]);
        }
        JudgeLanes(FRECPX_CASE, out, want, fpsr, want_flags, 8u, 1u, CPU_FRECPX_INPUT);
    }

    if (ReportStart(SUITE, AHP_CASE)) {
        // Narrowed and widened under AHP: the halves as the low half of each word to judge.
        uint16_t narrowed[8];
        uint32_t widened[8], got[16], want[16], inputs[16], want_flags[4] = { 0, 0, 0, 0 };
        uint64_t fpsr[4];
        Narrow(CPU_AHP_SINGLES, narrowed, fpsr, CPU_FPCR_AHP);
        Widen(CPU_AHP_HALVES, widened, fpsr + 2, CPU_FPCR_AHP);
        for (uint32_t i = 0; i < 8u; ++i) {
            got[i] = narrowed[i];
            inputs[i] = CPU_AHP_SINGLES[i];
            want[i] = CpuRefToHalf(CPU_AHP_SINGLES[i], CPU_FPCR_AHP, &want_flags[i / 4u]);
            got[8u + i] = widened[i];
            inputs[8u + i] = CPU_AHP_HALVES[i];
            want[8u + i] =
                CpuRefFromHalf(CPU_AHP_HALVES[i], CPU_FPCR_AHP, &want_flags[2u + i / 4u]);
        }
        JudgeLanes(AHP_CASE, got, want, fpsr, want_flags, 16u, 4u, inputs);
    }

    if (ReportStart(SUITE, DN_CASE)) {
        // The same four NaNs narrowed with DN clear, then set.
        uint32_t in[8];
        memcpy(in, CPU_DN_NANS, sizeof(CPU_DN_NANS));
        memcpy(in + 4, CPU_DN_NANS, sizeof(CPU_DN_NANS));
        uint16_t clear[8], set[8];
        uint32_t got[8], want[8], want_flags[2] = { 0, 0 };
        uint64_t fpsr_clear[2], fpsr_set[2], fpsr[2];
        Narrow(in, clear, fpsr_clear, 0);
        Narrow(in, set, fpsr_set, CPU_FPCR_DN);
        fpsr[0] = fpsr_clear[0];
        fpsr[1] = fpsr_set[1];
        for (uint32_t i = 0; i < 8u; ++i) {
            got[i] = i < 4u ? clear[i] : set[i];
            want[i] = CpuRefToHalf(in[i], i < 4u ? 0u : CPU_FPCR_DN, &want_flags[i / 4u]);
        }
        JudgeLanes(DN_CASE, got, want, fpsr, want_flags, 8u, 4u, in);
    }

    if (ReportStart(SUITE, MINMAX)) {
        uint32_t out[32], want[32], inputs[32], want_flags[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        uint64_t fpsr[8];
        MinMax(CPU_MINMAX_A, CPU_MINMAX_B, out, fpsr);
        for (uint32_t i = 0; i < 32u; ++i) {
            const uint32_t op = i / 8u, lane = i % 8u;
            inputs[i] = CPU_MINMAX_A[lane];
            want[i] = CpuRefMinMax(CPU_MINMAX_A[lane], CPU_MINMAX_B[lane], op == 0u || op == 2u,
                                   op >= 2u, &want_flags[i / 4u]);
        }
        JudgeLanes(MINMAX, out, want, fpsr, want_flags, 32u, 4u, inputs);
    }
}

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

    // A translator whose table of fraction bits stops at 63 calls a null pointer here.
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

    // The console's processor has no SM4: there, and on a translator that cannot compile it,
    // the program ends here and the launch after goes on with the next case.
    if (ReportStart(SUITE, SM4_CASE)) {
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

    // Code the translator has never seen, with more live registers than an x86-64 host has, so
    // its register allocator spills. The state is the verdict; the time of the first calls less
    // the second is what compiling cost, a note for comparing two builds of one emulator.
    if (ReportStart(SUITE, COMPILE)) {
        uint64_t first_ns, second_ns;
        unsigned bad_block, bad_word;
        const unsigned bad = CompileBlocks(&first_ns, &second_ns, &bad_block, &bad_word);
        CHECK_TRUE(SUITE, COMPILE, CHECK_EXACT, bad == 0,
                   "%u of %u blocks left a wrong state, the first block %u at word %u", bad,
                   JIT_BLOCKS, bad_block, bad_word);
        const uint64_t compile_ns = first_ns > second_ns ? first_ns - second_ns : 0;
        ReportNote("cpu compile: %u blocks of %u operations, first calls %llu us, second calls "
                   "%llu us, %llu ns a block to compile",
                   JIT_BLOCKS, JIT_OPS, (unsigned long long)(first_ns / 1000u),
                   (unsigned long long)(second_ns / 1000u),
                   (unsigned long long)(compile_ns / JIT_BLOCKS));
    }

    // Instructions of the base AdvSIMD set that dynarmic had no translation for. Eden logs the
    // exception and skips the instruction, so the program goes on with v2 never written: wrong
    // answers here, not a crash.
    static uint64_t gap_out[26][2];
    static uint64_t gap_fpsr[26];
    char gap_detail[200];
    if (ReportStart(SUITE, GAP_RSHL)) {
        memset(gap_out, 0, sizeof(gap_out));
        CpuGapRoundingShifts(CPU_RSHL_VALUE, CPU_RSHL_SHIFT, gap_out, gap_fpsr);
        const unsigned gap_wrong = GapCheck(CPU_GAP_RSHL, CPU_GAP_RSHL_FORMS, gap_out, gap_fpsr,
                                            gap_detail, sizeof(gap_detail));
        CHECK_TRUE(SUITE, GAP_RSHL, CHECK_EXACT, gap_wrong == 0, "%u of %u wrong, first: %s",
                   gap_wrong, CPU_GAP_RSHL_FORMS, gap_detail);
    }
    if (ReportStart(SUITE, GAP_DMUL)) {
        memset(gap_out, 0, sizeof(gap_out));
        CpuGapDoublingMultiplies(CPU_DMUL_A, CPU_DMUL_B, CPU_DMUL_ACC, gap_out, gap_fpsr);
        const unsigned gap_wrong = GapCheck(CPU_GAP_DMUL, CPU_GAP_DMUL_FORMS, gap_out, gap_fpsr,
                                            gap_detail, sizeof(gap_detail));
        CHECK_TRUE(SUITE, GAP_DMUL, CHECK_EXACT, gap_wrong == 0, "%u of %u wrong, first: %s",
                   gap_wrong, CPU_GAP_DMUL_FORMS, gap_detail);
    }
    if (ReportStart(SUITE, GAP_RNARROW)) {
        memset(gap_out, 0, sizeof(gap_out));
        CpuGapRoundingNarrows(CPU_RNARROW_VALUE, gap_out, gap_fpsr);
        const unsigned gap_wrong = GapCheck(CPU_GAP_RNARROW, CPU_GAP_RNARROW_FORMS, gap_out,
                                            gap_fpsr, gap_detail, sizeof(gap_detail));
        CHECK_TRUE(SUITE, GAP_RNARROW, CHECK_EXACT, gap_wrong == 0, "%u of %u wrong, first: %s",
                   gap_wrong, CPU_GAP_RNARROW_FORMS, gap_detail);
    }

    // The floating point rules a translator emulates in software or maps to a host's own
    // instructions, which differ: estimates, the reciprocal exponent, half precision under AHP and
    // DN, and the NaN and nought order of the minimum and maximum.
    FpCases();

    // System registers a game may reach from EL0 that dynarmic does not model: its translator
    // hits UNREACHABLE and the emulator ends. On the console they say whether Horizon lets EL0
    // read them at all.
    if (ReportStart(SUITE, CNTVCT)) {
        uint64_t counts[4];
        ReadCounts(counts);
        CHECK_TRUE(SUITE, CNTVCT, CHECK_EXACT,
                   counts[1] <= counts[2] && counts[2] - counts[1] <= counts[3] - counts[0],
                   "physical %llu, virtual %llu then %llu, physical %llu",
                   (unsigned long long)counts[0], (unsigned long long)counts[1],
                   (unsigned long long)counts[2], (unsigned long long)counts[3]);
    }
    if (ReportStart(SUITE, DAIF)) {
        const uint64_t daif = ReadDaif();
        CHECK_TRUE(SUITE, DAIF, CHECK_EXACT, (daif & ~0x3c0ull) == 0, "daif %016llx",
                   (unsigned long long)daif);
    }
}
