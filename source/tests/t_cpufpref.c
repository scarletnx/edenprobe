// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>

#include "../check.h"
#include "../cpufpref.h"
#include "../report.h"
#include "tests.h"

// The cpu suite's floating point rules, against values worked out by hand from the Arm ARM's
// pseudocode: each table at its ends, the estimates of one, two and four, each class of input,
// AHP's exponent 31 and its saturation, a NaN's payload in half precision, and the order of NaNs
// and noughts under the minimum and the maximum.

static const char *const SUITE = "reference";

typedef struct {
    uint32_t in;
    uint32_t out;
    uint32_t flags;
} ByHand;

static int FirstWrong(const ByHand *cases, int count, uint32_t (*f)(uint32_t, uint32_t *)) {
    for (int i = 0; i < count; ++i) {
        uint32_t flags = 0;
        if (f(cases[i].in, &flags) != cases[i].out || flags != cases[i].flags) {
            return i;
        }
    }
    return -1;
}

static void Tables(void) {
    // RecipEstimate(256): 2^19 / 513 = 1022, (1022 + 1) / 2 = 511; RecipEstimate(511): 2^19 /
    // 1023 = 512, 256. RecipSqrtEstimate(128): a = 257, the largest b with 257 (b + 1)^2 under
    // 2^28 is 1022, 511; (256): a = 514, b = 722, 361; (511): a = 1022, b stays 512, 256.
    const bool tables = CpuRefRecipEstimate(256) == 511u && CpuRefRecipEstimate(511) == 256u &&
                        CpuRefRecipSqrtEstimate(128) == 511u &&
                        CpuRefRecipSqrtEstimate(256) == 361u &&
                        CpuRefRecipSqrtEstimate(511) == 256u;
    // The unsigned estimates: the top nine bits through the tables, all ones below the range.
    const bool unsigned_ok =
        CpuRefUrecpe(0x80000000u) == 0xff800000u && CpuRefUrecpe(0xffffffffu) == 0x80000000u &&
        CpuRefUrecpe(0x7fffffffu) == 0xffffffffu && CpuRefUrsqrte(0x3fffffffu) == 0xffffffffu &&
        CpuRefUrsqrte(0x40000000u) == 0xff800000u;
    CHECK_TRUE(SUITE, "the estimate tables and the unsigned estimates give hand-worked values",
               CHECK_NONE, tables && unsigned_ok, "tables %d, unsigned %d", tables, unsigned_ok);
}

static void Estimates(void) {
    // FRECPE: 1 and 2 through RecipEstimate(256) = 511 at exponents 126 and 125; 2^127 to the
    // denormal 01 then the estimate's eight bits; nought to infinity, divide by zero; under
    // 2^-128 to infinity, overflow and inexact; an infinity to nought; a signalling NaN quieted.
    static const ByHand FRECPE[] = {
        { 0x3f800000u, 0x3f7f8000u, 0 },
        { 0x40000000u, 0x3eff8000u, 0 },
        { 0x7f000000u, 0x003fe000u, 0 },
        { 0x00000000u, 0x7f800000u, CPU_FPSR_DZC },
        { 0x001fffffu, 0x7f800000u, CPU_FPSR_OFC | CPU_FPSR_IXC },
        { 0xff800000u, 0x80000000u, 0 },
        { 0x7f800005u, 0x7fc00005u, CPU_FPSR_IOC },
    };
    // FRSQRTE: 1 and 4 (odd exponents) through RecipSqrtEstimate(128) = 511, 2 (even) through
    // 361 = 0x169; a negative value the default NaN, Invalid; minus nought minus infinity.
    static const ByHand FRSQRTE[] = {
        { 0x3f800000u, 0x3f7f8000u, 0 },
        { 0x40800000u, 0x3eff8000u, 0 },
        { 0x40000000u, 0x3f348000u, 0 },
        { 0xbf800000u, 0x7fc00000u, CPU_FPSR_IOC },
        { 0x80000000u, 0xff800000u, CPU_FPSR_DZC },
        { 0x7f800000u, 0x00000000u, 0 },
    };
    // FRECPX: the exponent's bits inverted, the largest exponent for nought and denormals.
    static const ByHand FRECPX[] = {
        { 0x3f800000u, 0x40000000u, 0 },
        { 0xc0600000u, 0xbf800000u, 0 },
        { 0x00000001u, 0x7f000000u, 0 },
        { 0x7f800000u, 0x00000000u, 0 },
        { 0x7f800001u, 0x7fc00001u, CPU_FPSR_IOC },
    };
    const int recpe = FirstWrong(FRECPE, 7, CpuRefFrecpe);
    const int rsqrte = FirstWrong(FRSQRTE, 6, CpuRefFrsqrte);
    const int recpx = FirstWrong(FRECPX, 5, CpuRefFrecpx);
    CHECK_TRUE(SUITE, "frecpe, frsqrte and frecpx give hand-worked singles and flags", CHECK_NONE,
               recpe < 0 && rsqrte < 0 && recpx < 0,
               "the first wrong: frecpe %d, frsqrte %d, frecpx %d (-1 none)", recpe, rsqrte, recpx);
}

static void Halves(void) {
    uint32_t f[10] = { 0 };
    // Under AHP exponent 31 is 2^16: 0x7c00 is 65536, 0xffff -131008; 131072 does not fit and
    // saturates, Invalid with no Inexact, as an infinity does; a NaN is nought, Invalid. Without
    // AHP 65536 overflows to an infinity. A NaN keeps its quiet bit and the payload's top nine
    // bits (0x412345 >> 13 is 0x209), the default NaN with DN; 2^-25 and a bit rounds up to the
    // least denormal, underflow and inexact.
    const bool ahp =
        CpuRefFromHalf(0x7c00u, CPU_FPCR_AHP, &f[0]) == 0x47800000u &&
        CpuRefFromHalf(0xffffu, CPU_FPCR_AHP, &f[0]) == 0xc7ffe000u && f[0] == 0u &&
        CpuRefToHalf(0x48000000u, CPU_FPCR_AHP, &f[1]) == 0x7fffu && f[1] == CPU_FPSR_IOC &&
        CpuRefToHalf(0x7f800000u, CPU_FPCR_AHP, &f[2]) == 0x7fffu && f[2] == CPU_FPSR_IOC &&
        CpuRefToHalf(0x7fc00000u, CPU_FPCR_AHP, &f[3]) == 0x0000u && f[3] == CPU_FPSR_IOC &&
        CpuRefToHalf(0x47800000u, CPU_FPCR_AHP, &f[4]) == 0x7c00u && f[4] == 0u;
    const bool ieee =
        CpuRefToHalf(0x47800000u, 0, &f[5]) == 0x7c00u && f[5] == (CPU_FPSR_OFC | CPU_FPSR_IXC) &&
        CpuRefToHalf(0x7fc12345u, 0, &f[6]) == 0x7e09u && f[6] == 0u &&
        CpuRefToHalf(0x7fc12345u, CPU_FPCR_DN, &f[7]) == 0x7e00u &&
        CpuRefToHalf(0x33000001u, 0, &f[8]) == 0x0001u && f[8] == (CPU_FPSR_UFC | CPU_FPSR_IXC);
    CHECK_TRUE(SUITE, "half precision under AHP and DN gives hand-worked values and flags",
               CHECK_NONE, ahp && ieee, "AHP %d, IEEE %d", ahp, ieee);
}

static void MinMax(void) {
    uint32_t f[8] = { 0 };
    // A quiet NaN wins FMAX and loses FMAXNM and FMINNM; a signalling NaN wins either, quieted
    // and Invalid, even second; the maximum of two noughts is +0 and the minimum -0, either way
    // round; denormals compare as numbers.
    const bool ok =
        CpuRefMinMax(0x7fc00001u, 0x3f800000u, true, false, &f[0]) == 0x7fc00001u &&
        CpuRefMinMax(0x7fc00001u, 0x3f800000u, true, true, &f[0]) == 0x3f800000u &&
        CpuRefMinMax(0x3f800000u, 0x7fc00004u, false, true, &f[0]) == 0x3f800000u && f[0] == 0u &&
        CpuRefMinMax(0x7f800002u, 0x7fc00005u, true, false, &f[1]) == 0x7fc00002u &&
        f[1] == CPU_FPSR_IOC &&
        CpuRefMinMax(0x7fc00003u, 0xff800006u, true, true, &f[2]) == 0xffc00006u &&
        f[2] == CPU_FPSR_IOC &&
        CpuRefMinMax(0x00000000u, 0x80000000u, true, false, &f[3]) == 0x00000000u &&
        CpuRefMinMax(0x00000000u, 0x80000000u, false, false, &f[3]) == 0x80000000u &&
        CpuRefMinMax(0x80000000u, 0x00000000u, false, true, &f[3]) == 0x80000000u &&
        CpuRefMinMax(0x00000001u, 0x80000001u, true, false, &f[3]) == 0x00000001u && f[3] == 0u;
    CHECK_TRUE(SUITE, "fmax, fmin, fmaxnm and fminnm order hand-worked NaNs and noughts",
               CHECK_NONE, ok,
               "a pair disagrees with the Arm ARM's FPMax, FPMin or their Num forms");
}

// By hand from FPRoundInt: a single, a mode, FRINTX or not, the answer and its flags.
typedef struct {
    uint32_t in;
    unsigned mode;
    bool exact;
    uint32_t out;
    uint32_t fpsr;
} RoundByHand;
static const RoundByHand ROUND_BY_HAND[] = {
    {0x40200000u, CPU_ROUND_NEAREST, false, 0x40000000u, 0},            // 2.5 to the even 2
    {0x40200000u, CPU_ROUND_AWAY, false, 0x40400000u, 0},               // 2.5 away to 3
    {0xbf000000u, CPU_ROUND_NEAREST, false, 0x80000000u, 0},            // -0.5 to -0
    {0xbe800000u, CPU_ROUND_PLUS, false, 0x80000000u, 0},               // -0.25 up to -0
    {0xbe800000u, CPU_ROUND_MINUS, true, 0xbf800000u, CPU_FPSR_IXC},    // -0.25 down to -1
    {0x4affffffu, CPU_ROUND_ZERO, true, 0x4afffffeu, CPU_FPSR_IXC},     // 2^23 - 0.5 to 2^23 - 1
    {0x4affffffu, CPU_ROUND_NEAREST, false, 0x4b000000u, 0},            // to the even 2^23
    {0x00000001u, CPU_ROUND_PLUS, true, 0x3f800000u, CPU_FPSR_IXC},     // a denormal up to 1
    {0x7f812345u, CPU_ROUND_NEAREST, false, 0x7fc12345u, CPU_FPSR_IOC}, // quieted, invalid
    {0x4b000001u, CPU_ROUND_MINUS, true, 0x4b000001u, 0},               // an integer as it is
};

static void Rounding(void) {
    int at = -1;
    for (int i = 0; i < (int)(sizeof(ROUND_BY_HAND) / sizeof(ROUND_BY_HAND[0])) && at < 0; ++i) {
        const RoundByHand *h = &ROUND_BY_HAND[i];
        uint32_t fpsr = 0;
        if (CpuRefRoundSingle(h->in, h->mode, h->exact, &fpsr) != h->out || fpsr != h->fpsr) {
            at = i;
        }
    }
    CHECK_TRUE(SUITE, "frint's roundings of a single worked out by hand", CHECK_NONE, at < 0,
               "row %d differs from its value by hand", at);
}

void RunReferenceCpuFp(void) {
    Rounding();
    Tables();
    Estimates();
    Halves();
    MinMax();
}
