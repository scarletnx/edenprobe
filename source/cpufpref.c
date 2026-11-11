// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cpufpref.h"

// Each function follows its pseudocode function's branches in the order the pseudocode takes
// them, since which branch comes first decides the answer and the flag for an input two of them
// match (a signalling NaN against a quiet one, a negative nought under FRSQRTE).

#define FPSR_IOC CPU_FPSR_IOC
#define FPSR_DZC CPU_FPSR_DZC
#define FPSR_OFC CPU_FPSR_OFC
#define FPSR_UFC CPU_FPSR_UFC
#define FPSR_IXC CPU_FPSR_IXC

#define SIGN32 0x80000000u
#define EXP32(x) (((x) >> 23) & 0xFFu)
#define FRAC32(x) ((x) & 0x7FFFFFu)

static bool IsNan32(uint32_t x) {
    return EXP32(x) == 0xFFu && FRAC32(x) != 0u;
}
static bool IsSignalling32(uint32_t x) {
    return IsNan32(x) && (x & 0x400000u) == 0u;
}

/// FPProcessNaN for a single with FPCR.DN clear: the NaN quieted, Invalid for a signalling one.
static uint32_t ProcessNan32(uint32_t x, uint32_t *fpsr) {
    if (IsSignalling32(x)) {
        *fpsr |= FPSR_IOC;
    }
    return x | 0x400000u;
}

/// RecipEstimate, the Armv8.0 one: @p a in 256..511 is a fraction in 0.5..1 in steps of 1/512.
uint32_t CpuRefRecipEstimate(uint32_t a) {
    a = a * 2u + 1u;
    const uint32_t b = (1u << 19) / a;
    return (b + 1u) / 2u;
}

/// RecipSqrtEstimate, the Armv8.0 one: @p a in 128..511.
uint32_t CpuRefRecipSqrtEstimate(uint32_t a) {
    if (a < 256u) {
        a = a * 2u + 1u;
    } else {
        a = (a >> 1) << 1;
        a = (a + 1u) * 2u;
    }
    uint64_t b = 512u;
    while ((uint64_t)a * (b + 1u) * (b + 1u) < (1ull << 28)) {
        ++b;
    }
    return (uint32_t)((b + 1u) / 2u);
}

uint32_t CpuRefUrecpe(uint32_t x) {
    return (x & SIGN32) == 0u ? 0xFFFFFFFFu : CpuRefRecipEstimate(x >> 23) << 23;
}

uint32_t CpuRefUrsqrte(uint32_t x) {
    return (x >> 30) == 0u ? 0xFFFFFFFFu : CpuRefRecipSqrtEstimate(x >> 23) << 23;
}

uint32_t CpuRefFrecpe(uint32_t x, uint32_t *fpsr) {
    const uint32_t sign = x & SIGN32;
    const uint32_t exp_bits = EXP32(x);
    if (IsNan32(x)) {
        return ProcessNan32(x, fpsr);
    }
    if (exp_bits == 0xFFu) {
        return sign;
    }
    if (exp_bits == 0u && FRAC32(x) == 0u) {
        *fpsr |= FPSR_DZC;
        return sign | 0x7F800000u;
    }
    // Under 2^-128: a denormal whose fraction's top two bits are clear. Rounding to nearest
    // overflows to an infinity.
    if (exp_bits == 0u && (FRAC32(x) >> 21) == 0u) {
        *fpsr |= FPSR_OFC | FPSR_IXC;
        return sign | 0x7F800000u;
    }
    // The fraction as the pseudocode's 52 bits; only its top nine matter.
    uint64_t fraction = (uint64_t)FRAC32(x) << 29;
    int32_t exp = (int32_t)exp_bits;
    if (exp == 0) {
        if (((fraction >> 51) & 1u) == 0u) {
            exp = -1;
            fraction = (fraction << 2) & ((1ull << 52) - 1u);
        } else {
            fraction = (fraction << 1) & ((1ull << 52) - 1u);
        }
    }
    const uint32_t scaled = 0x100u | (uint32_t)((fraction >> 44) & 0xFFu);
    int32_t result_exp = 253 - exp;
    const uint32_t estimate = CpuRefRecipEstimate(scaled);
    fraction = (uint64_t)(estimate & 0xFFu) << 44;
    if (result_exp == 0) {
        fraction = (1ull << 51) | (fraction >> 1);
    } else if (result_exp == -1) {
        fraction = (1ull << 50) | (fraction >> 2);
        result_exp = 0;
    }
    return sign | ((uint32_t)result_exp & 0xFFu) << 23 | (uint32_t)(fraction >> 29);
}

uint32_t CpuRefFrsqrte(uint32_t x, uint32_t *fpsr) {
    const uint32_t sign = x & SIGN32;
    const uint32_t exp_bits = EXP32(x);
    if (IsNan32(x)) {
        return ProcessNan32(x, fpsr);
    }
    if (exp_bits == 0u && FRAC32(x) == 0u) {
        *fpsr |= FPSR_DZC;
        return sign | 0x7F800000u;
    }
    if (sign != 0u) {
        *fpsr |= FPSR_IOC;
        return 0x7FC00000u;
    }
    if (exp_bits == 0xFFu) {
        return 0u;
    }
    uint64_t fraction = (uint64_t)FRAC32(x) << 29;
    int32_t exp = (int32_t)exp_bits;
    if (exp == 0) {
        while (((fraction >> 51) & 1u) == 0u) {
            fraction = (fraction << 1) & ((1ull << 52) - 1u);
            --exp;
        }
        fraction = (fraction << 1) & ((1ull << 52) - 1u);
    }
    uint32_t scaled;
    if ((exp & 1) == 0) {
        scaled = 0x100u | (uint32_t)((fraction >> 44) & 0xFFu);
    } else {
        scaled = 0x80u | (uint32_t)((fraction >> 45) & 0x7Fu);
    }
    // (380 - exp) DIV 2, exp never above 254, so the dividend is positive.
    const int32_t result_exp = (380 - exp) / 2;
    const uint32_t estimate = CpuRefRecipSqrtEstimate(scaled);
    return ((uint32_t)result_exp & 0xFFu) << 23 | (estimate & 0xFFu) << 15;
}

uint32_t CpuRefFrecpx(uint32_t x, uint32_t *fpsr) {
    if (IsNan32(x)) {
        return ProcessNan32(x, fpsr);
    }
    const uint32_t sign = x & SIGN32;
    const uint32_t exp_bits = EXP32(x);
    return sign | (exp_bits == 0u ? 0xFEu : (~exp_bits & 0xFFu)) << 23;
}

#define FPCR_AHP CPU_FPCR_AHP
#define FPCR_DN CPU_FPCR_DN

/// FPConvert of a single to half precision at round to nearest, FPCR's AHP and DN as @p fpcr has
/// them (FZ and FZ16 clear).
uint16_t CpuRefToHalf(uint32_t x, uint32_t fpcr, uint32_t *fpsr) {
    const bool ahp = (fpcr & FPCR_AHP) != 0u;
    const uint16_t sign = (uint16_t)((x >> 16) & 0x8000u);
    const uint32_t exp_bits = EXP32(x);
    const uint32_t frac = FRAC32(x);
    if (IsNan32(x)) {
        if (IsSignalling32(x) || ahp) {
            *fpsr |= FPSR_IOC;
        }
        if (ahp) {
            return sign;
        }
        if ((fpcr & FPCR_DN) != 0u) {
            return 0x7E00u;
        }
        // FPConvertNaN: the sign, all ones, the quiet bit and the payload's top nine bits.
        return (uint16_t)(sign | 0x7E00u | ((frac >> 13) & 0x1FFu));
    }
    if (exp_bits == 0xFFu) {
        if (ahp) {
            *fpsr |= FPSR_IOC;
            return (uint16_t)(sign | 0x7FFFu);
        }
        return (uint16_t)(sign | 0x7C00u);
    }
    if (exp_bits == 0u && frac == 0u) {
        return sign;
    }
    // The value is m * 2^e exactly.
    uint64_t m = exp_bits == 0u ? frac : (0x800000u | frac);
    const int32_t e = exp_bits == 0u ? -149 : (int32_t)exp_bits - 150;
    int32_t bits = 0;
    for (uint64_t t = m; t != 0u; t >>= 1) {
        ++bits;
    }
    const int32_t exponent = bits - 1 + e;
    int32_t biased = exponent + 15 > 0 ? exponent + 15 : 0;
    const int32_t shift = (biased > 0 ? 10 - exponent : 24) + e;
    uint64_t int_mant;
    int error; // -1 below a half, 0 exact, 1 a half, 2 above
    if (shift >= 0) {
        int_mant = m << shift;
        error = 0;
    } else if (-shift >= 63) {
        int_mant = 0;
        error = -1;
    } else {
        const uint32_t s = (uint32_t)-shift;
        int_mant = m >> s;
        const uint64_t rest = m & ((1ull << s) - 1u);
        const uint64_t half = 1ull << (s - 1u);
        error = rest == 0u ? 0 : rest < half ? -1 : rest == half ? 1 : 2;
    }
    if (biased == 0 && error != 0) {
        *fpsr |= FPSR_UFC;
    }
    if (error == 2 || (error == 1 && (int_mant & 1u) != 0u)) {
        ++int_mant;
        if (int_mant == 1024u) {
            biased = 1;
        }
        if (int_mant == 2048u) {
            ++biased;
            int_mant >>= 1;
        }
    }
    if (!ahp) {
        if (biased >= 31) {
            *fpsr |= FPSR_OFC | FPSR_IXC;
            return (uint16_t)(sign | 0x7C00u);
        }
    } else if (biased >= 32) {
        *fpsr |= FPSR_IOC;
        return (uint16_t)(sign | 0x7FFFu);
    }
    if (error != 0) {
        *fpsr |= FPSR_IXC;
    }
    return (uint16_t)(sign | (uint32_t)biased << 10 | (uint32_t)(int_mant & 0x3FFu));
}

/// FPConvert of a half to single precision, FPCR's AHP and DN as @p fpcr has them: exact.
uint32_t CpuRefFromHalf(uint16_t h, uint32_t fpcr, uint32_t *fpsr) {
    const uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t frac = h & 0x3FFu;
    if (exp == 0x1Fu && (fpcr & FPCR_AHP) == 0u) {
        if (frac == 0u) {
            return sign | 0x7F800000u;
        }
        if ((frac & 0x200u) == 0u) {
            *fpsr |= FPSR_IOC;
        }
        if ((fpcr & FPCR_DN) != 0u) {
            return 0x7FC00000u;
        }
        return sign | 0x7FC00000u | frac << 13;
    }
    if (exp == 0u) {
        if (frac == 0u) {
            return sign;
        }
        // frac * 2^-24, normalised.
        uint32_t f = frac;
        int32_t e = -14;
        while ((f & 0x400u) == 0u) {
            f <<= 1;
            --e;
        }
        return sign | (uint32_t)(e + 127) << 23 | (f & 0x3FFu) << 13;
    }
    return sign | (exp - 15u + 127u) << 23 | frac << 13;
}

/// The value order of two singles that are not NaNs: below nought, nought, above.
static int Compare32(uint32_t a, uint32_t b) {
    const bool za = (a & 0x7FFFFFFFu) == 0u, zb = (b & 0x7FFFFFFFu) == 0u;
    if (za && zb) {
        return 0;
    }
    // Sign and magnitude to a two's complement key, which orders as the values do.
    const int64_t ka = (a & SIGN32) ? -(int64_t)(a & 0x7FFFFFFFu) : (int64_t)a;
    const int64_t kb = (b & SIGN32) ? -(int64_t)(b & 0x7FFFFFFFu) : (int64_t)b;
    return ka < kb ? -1 : ka > kb ? 1 : 0;
}

/// FMAX, FMIN, FMAXNM and FMINNM of two singles, FPCR clear: FPMax, FPMin, FPMaxNum and FPMinNum.
uint32_t CpuRefMinMax(uint32_t a, uint32_t b, bool max, bool number, uint32_t *fpsr) {
    if (number) {
        // A single quiet NaN is taken as the infinity that loses.
        const bool qa = IsNan32(a) && !IsSignalling32(a), qb = IsNan32(b) && !IsSignalling32(b);
        const uint32_t loses = max ? 0xFF800000u : 0x7F800000u;
        if (qa && !qb) {
            a = loses;
        } else if (!qa && qb) {
            b = loses;
        }
    }
    // FPProcessNaNs: a signalling NaN first, the first operand first.
    if (IsSignalling32(a)) {
        return ProcessNan32(a, fpsr);
    }
    if (IsSignalling32(b)) {
        return ProcessNan32(b, fpsr);
    }
    if (IsNan32(a)) {
        return ProcessNan32(a, fpsr);
    }
    if (IsNan32(b)) {
        return ProcessNan32(b, fpsr);
    }
    if ((a & 0x7FFFFFFFu) == 0u && (b & 0x7FFFFFFFu) == 0u) {
        // Nought against nought: the most positive sign for max, the most negative for min.
        return max ? (a & b & SIGN32) : ((a | b) & SIGN32);
    }
    const int order = Compare32(a, b);
    if (max) {
        return order > 0 ? a : b;
    }
    return order < 0 ? a : b;
}

// Normal values, then large values whose estimates are denormal and small ones processed as they
// are, then the ones that overflow or divide by nought, then infinities and NaNs.
const uint32_t CPU_FRECPE_INPUT[16] = {
    0x3f800000u, 0x3fc00000u, 0xc2f6e979u, 0x3fabcdefu, // 1, 1.5, -123.456, 1.3422
    0x7f000000u, 0x7e800000u, 0x00400000u, 0x80200000u, // 2^127, 2^126, 2^-127, -2^-128
    0x001fffffu, 0x80000001u, 0x00000000u, 0x80000000u, // under 2^-128 twice, +0, -0
    0x7f800000u, 0xff800000u, 0x7fc00001u, 0x7f800005u, // +inf, -inf, a quiet and a signalling NaN
};

// Exponents even and odd, then denormals and the largest single, then noughts and a negative
// value, then an infinity and NaNs.
const uint32_t CPU_FRSQRTE_INPUT[16] = {
    0x3f800000u, 0x40000000u, 0x40800000u, 0x40490fdbu, // 1, 2, 4, pi
    0x00000001u, 0x007fffffu, 0x00400000u, 0x7f7fffffu, // the least denormal, two more, the largest
    0x00000000u, 0x80000000u, 0xbf800000u, 0x3e800000u, // +0, -0, -1, 0.25
    0x7f800000u, 0x7fc12345u, 0x7f812345u, 0xffc00000u, // +inf, a quiet, a signalling, -quiet NaN
};

// URECPE's all ones below 2^31, URSQRTE's below 2^30, and estimates in between.
const uint32_t CPU_UESTIMATE_INPUT[8] = {
    0x7fffffffu, 0x80000000u, 0xffffffffu, 0xc0000000u,
    0x3fffffffu, 0x40000000u, 0x12345678u, 0x9abcdef0u,
};

const uint32_t CPU_FRECPX_INPUT[8] = {
    0x3f800000u, 0xc0600000u, 0x00000001u, 0x00000000u, // 1, -3.5, the least denormal, +0
    0x80000000u, 0x7f800000u, 0x7fc00000u, 0x7f800001u, // -0, +inf, a quiet and a signalling NaN
};

// Around half precision's largest value, 65504 for IEEE and 131008 with AHP, then an infinity, a
// NaN, one, and a value just over half the least half denormal, which rounds up to it.
const uint32_t CPU_AHP_SINGLES[8] = {
    0x477fe000u, 0x47800000u, 0x47ffe000u, 0x48000000u, // 65504, 65536, 131008, 131072
    0x7f800000u, 0x7fc00000u, 0x3f800000u, 0x33000001u, // +inf, NaN, 1, 2^-25 and a bit
};

// Exponent 31, a number under AHP, then ordinary halves, the least denormal among them.
const uint16_t CPU_AHP_HALVES[8] = {
    0x7c00u, 0xffffu, 0x7e00u, 0x3c00u, 0x0001u, 0x8400u, 0x7bffu, 0x0000u,
};

// Quiet and signalling, both signs, each with a payload whose top nine bits are not all nought.
const uint32_t CPU_DN_NANS[4] = { 0x7fc12345u, 0x7f812345u, 0xffd55555u, 0xffaaaaaau };

// A quiet NaN against a number either way round, a signalling NaN against a quiet one either way,
// the two noughts both ways, two denormals of opposite sign, and an infinity against a number.
const uint32_t CPU_MINMAX_A[8] = {
    0x7fc00001u, 0x3f800000u, 0x7f800002u, 0x7fc00003u,
    0x00000000u, 0x80000000u, 0x00000001u, 0xff800000u,
};
const uint32_t CPU_MINMAX_B[8] = {
    0x3f800000u, 0x7fc00004u, 0x7fc00005u, 0xff800006u,
    0x80000000u, 0x00000000u, 0x80000001u, 0x3f800000u,
};
