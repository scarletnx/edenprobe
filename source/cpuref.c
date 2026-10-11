// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cpuref.h"

const uint8_t CPU_CLZ8_INPUT[16] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40,
                                    0x80, 0xff, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f};
const uint16_t CPU_CLZ16_INPUT[8] = {0x0000, 0x0001, 0x0080, 0x7fff,
                                     0x8000, 0x0f00, 0x0003, 0x00ff};
const uint32_t CPU_CLZ32_INPUT[4] = {0x00000000u, 0x00010000u, 0x80000000u, 0x00000007u};

uint8_t CpuRefClz8(uint8_t value) {
    uint8_t count = 0;
    for (uint8_t bit = 0x80; bit != 0 && (value & bit) == 0; bit >>= 1) {
        ++count;
    }
    return count;
}

uint16_t CpuRefClz16(uint16_t value) {
    uint16_t count = 0;
    for (uint16_t bit = 0x8000; bit != 0 && (value & bit) == 0; bit >>= 1) {
        ++count;
    }
    return count;
}

uint32_t CpuRefClz32(uint32_t value) {
    uint32_t count = 0;
    for (uint32_t bit = 0x80000000u; bit != 0 && (value & bit) == 0; bit >>= 1) {
        ++count;
    }
    return count;
}

const uint64_t CPU_DIV_INPUT[4] = {0x12345678u, 0u, 0x123456789abcdef0ull, 0u};

// 0.3, 1024.5, 2.5, -0.7 and 2.5, -2.5, 0.5, 1.4.
const uint64_t CPU_FRINTX_INPUT[4] = {0x3fd3333333333333ull, 0x4090020000000000ull,
                                      0x4004000000000000ull, 0xbfe6666666666666ull};
const uint64_t CPU_FRINTA_INPUT[4] = {0x4004000000000000ull, 0xc004000000000000ull,
                                      0x3fe0000000000000ull, 0x3ff6666666666666ull};

uint64_t CpuRefRoundDouble(uint64_t bits, bool ties_away, bool *inexact) {
    const uint64_t sign = bits & 0x8000000000000000ull;
    const uint64_t fraction = bits & 0x000fffffffffffffull;
    const int exponent = (int)((bits >> 52) & 0x7ffu);
    *inexact = false;
    // Infinities, NaNs and anything from 2^52 up are whole already; so is a zero.
    if (exponent == 0x7ff || exponent >= 1075 || (exponent == 0 && fraction == 0)) {
        return bits;
    }
    // The magnitude is mantissa / 2^below, with the hidden bit for a normal number.
    const uint64_t mantissa = fraction | (exponent != 0 ? 0x0010000000000000ull : 0u);
    const int below = 1075 - (exponent != 0 ? exponent : 1);
    uint64_t whole = 0;
    bool up = false;
    if (below <= 53) {
        whole = mantissa >> below;
        const uint64_t rest = mantissa & ((1ull << below) - 1u);
        const uint64_t half = 1ull << (below - 1);
        up = rest > half || (rest == half && (ties_away || (whole & 1u) != 0));
        *inexact = rest != 0;
    } else {
        // Under a half, since the mantissa is under 2^53: nearer to zero than to one.
        *inexact = true;
    }
    whole += up ? 1u : 0u;
    if (whole == 0) {
        return sign;
    }
    int top = 52;
    while ((whole >> top) == 0) {
        --top;
    }
    return sign | ((uint64_t)(1023 + top) << 52) | ((whole << (52 - top)) & 0x000fffffffffffffull);
}

const uint64_t CPU_SQSHLU_INPUT[5] = {0x8ecb02792c32fe49ull, 0x80007fff10000fffull,
                                      0xef319b929116dc5aull, 0x362758dfbfb5c4dbull,
                                      0x00000000bf800000ull};
const uint8_t CPU_SQSHLU_SHIFT[4] = {3, 4, 0, 24};

uint64_t CpuRefSqshlu(uint64_t lanes, unsigned esize, unsigned shift, bool *saturated) {
    const uint64_t mask = esize >= 64u ? ~0ull : (1ull << esize) - 1u;
    uint64_t out = 0;
    *saturated = false;
    for (unsigned at = 0; at < 64u; at += esize) {
        const uint64_t lane = (lanes >> at) & mask;
        const bool negative = ((lane >> (esize - 1u)) & 1u) != 0;
        uint64_t result;
        if (negative) {
            result = 0;
            *saturated = true;
        } else if (lane > (mask >> shift)) {
            result = mask;
            *saturated = true;
        } else {
            result = lane << shift;
        }
        out |= result << at;
    }
    return out;
}

// 0.25, -0.5; 0.75, 2^-63; 1.0, -1.0.
const uint64_t CPU_FCVTZ_INPUT[6] = {0x3fd0000000000000ull, 0xbfe0000000000000ull,
                                     0x3fe8000000000000ull, 0x3c00000000000000ull,
                                     0x3ff0000000000000ull, 0xbff0000000000000ull};

uint64_t CpuRefFcvtzFixed64(uint64_t bits, bool is_unsigned) {
    const bool negative = (bits >> 63) != 0;
    const int exponent = (int)((bits >> 52) & 0x7ffu);
    const uint64_t fraction = bits & 0x000fffffffffffffull;
    const uint64_t most = is_unsigned ? ~0ull : 0x7fffffffffffffffull;
    const uint64_t least = is_unsigned ? 0u : 0x8000000000000000ull;
    if (exponent == 0x7ff) {
        return fraction != 0 ? 0u : (negative ? least : most);
    }
    // A zero, or a denormal, which times 2^64 is still far under one.
    if (exponent == 0) {
        return 0;
    }
    // mantissa * 2^(exponent - 1075), times 2^64.
    const uint64_t mantissa = fraction | 0x0010000000000000ull;
    const int shift = exponent - 1011;
    bool overflow = false;
    uint64_t magnitude = 0;
    if (shift > 11) {
        overflow = true;
    } else if (shift >= 0) {
        magnitude = mantissa << shift;
    } else if (shift > -64) {
        magnitude = mantissa >> -shift;
    }
    if (negative) {
        if (overflow || magnitude > 0x8000000000000000ull) {
            return least;
        }
        return is_unsigned ? 0u : ~magnitude + 1u;
    }
    return overflow || magnitude > most ? most : magnitude;
}

static uint64_t LaneMask(unsigned bits) {
    return bits >= 64u ? ~0ull : (1ull << bits) - 1u;
}

static __int128 SignedLane(uint64_t value, unsigned bits) {
    const uint64_t lane = value & LaneMask(bits);
    const uint64_t sign = 1ull << (bits - 1u);
    return (__int128)(int64_t)((lane ^ sign) - sign);
}

// SignedSatQ and UnsignedSatQ: x clamped to what @p bits hold, as a lane's bit pattern.
static uint64_t Saturate(__int128 x, unsigned bits, bool is_unsigned, bool *saturated) {
    const __int128 most = is_unsigned ? (__int128)LaneMask(bits) : ((__int128)1 << (bits - 1u)) - 1;
    const __int128 least = is_unsigned ? 0 : -((__int128)1 << (bits - 1u));
    if (x > most) {
        *saturated = true;
        x = most;
    } else if (x < least) {
        *saturated = true;
        x = least;
    }
    return (uint64_t)x & LaneMask(bits);
}

uint64_t CpuRefRoundingShiftLane(uint64_t value, int shift, unsigned esize, bool is_unsigned,
                                 bool *saturated) {
    *saturated = false;
    const __int128 x = is_unsigned ? (__int128)(value & LaneMask(esize)) : SignedLane(value, esize);
    __int128 shifted;
    if (shift >= 0) {
        // Any non-zero lane shifted by its width or more is out of range: saturate it.
        if (x == 0) {
            shifted = 0;
        } else if (shift >= (int)esize) {
            shifted = x < 0 ? -((__int128)1 << 100) : ((__int128)1 << 100);
        } else {
            shifted = x * ((__int128)1 << shift);
        }
    } else {
        // (x + 2^(n-1)) >> n is zero for every lane once n passes the lane's width.
        const unsigned n = (unsigned)-shift;
        shifted = n > esize ? 0 : (x + ((__int128)1 << (n - 1u))) >> n;
    }
    return Saturate(shifted, esize, is_unsigned, saturated);
}

uint64_t CpuRefDoublingMultiplyLane(uint64_t acc, uint64_t a, uint64_t b, unsigned esize,
                                    int accumulate, bool *saturated) {
    bool product_saturated = false;
    bool sum_saturated = false;
    const unsigned wide = 2u * esize;
    const uint64_t product =
        Saturate(2 * SignedLane(a, esize) * SignedLane(b, esize), wide, false, &product_saturated);
    if (accumulate == 0) {
        *saturated = product_saturated;
        return product;
    }
    const __int128 sum = SignedLane(acc, wide) + accumulate * SignedLane(product, wide);
    const uint64_t result = Saturate(sum, wide, false, &sum_saturated);
    *saturated = product_saturated || sum_saturated;
    return result;
}

uint64_t CpuRefRoundingNarrow(uint64_t value, unsigned esize, unsigned shift, bool source_unsigned,
                              bool result_unsigned, bool *saturated) {
    *saturated = false;
    const unsigned wide = 2u * esize;
    const __int128 x = source_unsigned ? (__int128)(value & LaneMask(wide)) : SignedLane(value, wide);
    return Saturate((x + ((__int128)1 << (shift - 1u))) >> shift, esize, result_unsigned, saturated);
}

const uint64_t CPU_RSHL_VALUE[4][2] = {
    {0x7f80017f80ff4001ull, 0x0102037e81fe7f80ull},
    {0x7fff80000001c000ull, 0x8001400000ff7ffeull},
    {0x7fffffff80000000ull, 0x00000001fffffffeull},
    {0x7fffffffffffffffull, 0x8000000000000001ull},
};
const uint64_t CPU_RSHL_SHIFT[4][2] = {
    {0x01ff0702f9fe0807ull, 0x80f8f7fd030081ffull},
    {0x0001ffff0010fff0ull, 0xfff100110002fffeull},
    {0x00000001ffffffe1ull, 0x00000020ffffffc0ull},
    {0x0000000000000001ull, 0xffffffffffffffc1ull},
};
const uint64_t CPU_DMUL_A[2] = {0x8000800012347fffull, 0x80000000fffffffbull};
const uint64_t CPU_DMUL_B[2] = {0x8000ffff56788000ull, 0x8000000000000007ull};
const uint64_t CPU_DMUL_ACC[2] = {0x7ffffffe00000001ull, 0x8000000000000005ull};
const uint64_t CPU_RNARROW_VALUE[3][2] = {
    {0x00000000000001ffull, 0x0000000000007f80ull},
    {0x000000000001ffffull, 0x00000000ffff8001ull},
    {0x00000001ffffffffull, 0x7fffffffffffffffull},
};

const CpuGapForm CPU_GAP_RSHL[CPU_GAP_RSHL_FORMS] = {
    {"sqrshl v2.16b, v0.16b, v1.16b", CPU_GAP_KIND_RSHL, 0, 8u, 0u, -1, 0u, 0u, 0u},
    {"sqrshl b2, b0, b1", CPU_GAP_KIND_RSHL, 0, 8u, 0u, -1, 1u, 0u, 0u},
    {"uqrshl v2.16b, v0.16b, v1.16b", CPU_GAP_KIND_RSHL, 1, 8u, 0u, -1, 0u, 0u, 0u},
    {"uqrshl b2, b0, b1", CPU_GAP_KIND_RSHL, 1, 8u, 0u, -1, 1u, 0u, 0u},
    {"sqrshl v2.8h, v0.8h, v1.8h", CPU_GAP_KIND_RSHL, 0, 16u, 0u, -1, 0u, 0u, 1u},
    {"sqrshl h2, h0, h1", CPU_GAP_KIND_RSHL, 0, 16u, 0u, -1, 1u, 0u, 1u},
    {"uqrshl v2.8h, v0.8h, v1.8h", CPU_GAP_KIND_RSHL, 1, 16u, 0u, -1, 0u, 0u, 1u},
    {"uqrshl h2, h0, h1", CPU_GAP_KIND_RSHL, 1, 16u, 0u, -1, 1u, 0u, 1u},
    {"sqrshl v2.4s, v0.4s, v1.4s", CPU_GAP_KIND_RSHL, 0, 32u, 0u, -1, 0u, 0u, 2u},
    {"sqrshl s2, s0, s1", CPU_GAP_KIND_RSHL, 0, 32u, 0u, -1, 1u, 0u, 2u},
    {"uqrshl v2.4s, v0.4s, v1.4s", CPU_GAP_KIND_RSHL, 1, 32u, 0u, -1, 0u, 0u, 2u},
    {"uqrshl s2, s0, s1", CPU_GAP_KIND_RSHL, 1, 32u, 0u, -1, 1u, 0u, 2u},
    {"sqrshl v2.2d, v0.2d, v1.2d", CPU_GAP_KIND_RSHL, 0, 64u, 0u, -1, 0u, 0u, 3u},
    {"sqrshl d2, d0, d1", CPU_GAP_KIND_RSHL, 0, 64u, 0u, -1, 1u, 0u, 3u},
    {"uqrshl v2.2d, v0.2d, v1.2d", CPU_GAP_KIND_RSHL, 1, 64u, 0u, -1, 0u, 0u, 3u},
    {"uqrshl d2, d0, d1", CPU_GAP_KIND_RSHL, 1, 64u, 0u, -1, 1u, 0u, 3u},
};
const CpuGapForm CPU_GAP_DMUL[CPU_GAP_DMUL_FORMS] = {
    {"sqdmlal s2, h0, h1", CPU_GAP_KIND_DMUL, 1, 16u, 0u, -1, 1u, 0u, 0u},
    {"sqdmlal v2.4s, v0.4h, v1.4h", CPU_GAP_KIND_DMUL, 1, 16u, 0u, -1, 0u, 0u, 0u},
    {"sqdmlal2 v2.4s, v0.8h, v1.8h", CPU_GAP_KIND_DMUL, 1, 16u, 1u, -1, 0u, 0u, 0u},
    {"sqdmlal v2.4s, v0.4h, v1.h[5]", CPU_GAP_KIND_DMUL, 1, 16u, 0u, 5, 0u, 0u, 0u},
    {"sqdmlal2 v2.4s, v0.8h, v1.h[3]", CPU_GAP_KIND_DMUL, 1, 16u, 1u, 3, 0u, 0u, 0u},
    {"sqdmlal s2, h0, v1.h[4]", CPU_GAP_KIND_DMUL, 1, 16u, 0u, 4, 1u, 0u, 0u},
    {"sqdmlal d2, s0, s1", CPU_GAP_KIND_DMUL, 1, 32u, 0u, -1, 1u, 0u, 0u},
    {"sqdmlal v2.2d, v0.2s, v1.2s", CPU_GAP_KIND_DMUL, 1, 32u, 0u, -1, 0u, 0u, 0u},
    {"sqdmlal2 v2.2d, v0.4s, v1.4s", CPU_GAP_KIND_DMUL, 1, 32u, 1u, -1, 0u, 0u, 0u},
    {"sqdmlal v2.2d, v0.2s, v1.s[3]", CPU_GAP_KIND_DMUL, 1, 32u, 0u, 3, 0u, 0u, 0u},
    {"sqdmlal2 v2.2d, v0.4s, v1.s[1]", CPU_GAP_KIND_DMUL, 1, 32u, 1u, 1, 0u, 0u, 0u},
    {"sqdmlal d2, s0, v1.s[2]", CPU_GAP_KIND_DMUL, 1, 32u, 0u, 2, 1u, 0u, 0u},
    {"sqdmlsl s2, h0, h1", CPU_GAP_KIND_DMUL, -1, 16u, 0u, -1, 1u, 0u, 0u},
    {"sqdmlsl v2.4s, v0.4h, v1.4h", CPU_GAP_KIND_DMUL, -1, 16u, 0u, -1, 0u, 0u, 0u},
    {"sqdmlsl2 v2.4s, v0.8h, v1.8h", CPU_GAP_KIND_DMUL, -1, 16u, 1u, -1, 0u, 0u, 0u},
    {"sqdmlsl v2.4s, v0.4h, v1.h[5]", CPU_GAP_KIND_DMUL, -1, 16u, 0u, 5, 0u, 0u, 0u},
    {"sqdmlsl2 v2.4s, v0.8h, v1.h[3]", CPU_GAP_KIND_DMUL, -1, 16u, 1u, 3, 0u, 0u, 0u},
    {"sqdmlsl s2, h0, v1.h[4]", CPU_GAP_KIND_DMUL, -1, 16u, 0u, 4, 1u, 0u, 0u},
    {"sqdmlsl d2, s0, s1", CPU_GAP_KIND_DMUL, -1, 32u, 0u, -1, 1u, 0u, 0u},
    {"sqdmlsl v2.2d, v0.2s, v1.2s", CPU_GAP_KIND_DMUL, -1, 32u, 0u, -1, 0u, 0u, 0u},
    {"sqdmlsl2 v2.2d, v0.4s, v1.4s", CPU_GAP_KIND_DMUL, -1, 32u, 1u, -1, 0u, 0u, 0u},
    {"sqdmlsl v2.2d, v0.2s, v1.s[3]", CPU_GAP_KIND_DMUL, -1, 32u, 0u, 3, 0u, 0u, 0u},
    {"sqdmlsl2 v2.2d, v0.4s, v1.s[1]", CPU_GAP_KIND_DMUL, -1, 32u, 1u, 1, 0u, 0u, 0u},
    {"sqdmlsl d2, s0, v1.s[2]", CPU_GAP_KIND_DMUL, -1, 32u, 0u, 2, 1u, 0u, 0u},
    {"sqdmull s2, h0, h1", CPU_GAP_KIND_DMUL, 0, 16u, 0u, -1, 1u, 0u, 0u},
    {"sqdmull d2, s0, s1", CPU_GAP_KIND_DMUL, 0, 32u, 0u, -1, 1u, 0u, 0u},
};
const CpuGapForm CPU_GAP_RNARROW[CPU_GAP_RNARROW_FORMS] = {
    {"sqrshrn b2, h0, #1", CPU_GAP_KIND_RNARROW, 0, 8u, 0u, -1, 1u, 1u, 0u},
    {"sqrshrn b2, h0, #8", CPU_GAP_KIND_RNARROW, 0, 8u, 0u, -1, 1u, 8u, 1u},
    {"sqrshrn h2, s0, #1", CPU_GAP_KIND_RNARROW, 0, 16u, 0u, -1, 1u, 1u, 2u},
    {"sqrshrn h2, s0, #16", CPU_GAP_KIND_RNARROW, 0, 16u, 0u, -1, 1u, 16u, 3u},
    {"sqrshrn s2, d0, #1", CPU_GAP_KIND_RNARROW, 0, 32u, 0u, -1, 1u, 1u, 4u},
    {"sqrshrn s2, d0, #32", CPU_GAP_KIND_RNARROW, 0, 32u, 0u, -1, 1u, 32u, 5u},
    {"uqrshrn b2, h0, #1", CPU_GAP_KIND_RNARROW, 1, 8u, 0u, -1, 1u, 1u, 0u},
    {"uqrshrn b2, h0, #8", CPU_GAP_KIND_RNARROW, 1, 8u, 0u, -1, 1u, 8u, 1u},
    {"uqrshrn h2, s0, #1", CPU_GAP_KIND_RNARROW, 1, 16u, 0u, -1, 1u, 1u, 2u},
    {"uqrshrn h2, s0, #16", CPU_GAP_KIND_RNARROW, 1, 16u, 0u, -1, 1u, 16u, 3u},
    {"uqrshrn s2, d0, #1", CPU_GAP_KIND_RNARROW, 1, 32u, 0u, -1, 1u, 1u, 4u},
    {"uqrshrn s2, d0, #32", CPU_GAP_KIND_RNARROW, 1, 32u, 0u, -1, 1u, 32u, 5u},
    {"sqrshrun b2, h0, #1", CPU_GAP_KIND_RNARROW, 2, 8u, 0u, -1, 1u, 1u, 0u},
    {"sqrshrun b2, h0, #8", CPU_GAP_KIND_RNARROW, 2, 8u, 0u, -1, 1u, 8u, 1u},
    {"sqrshrun h2, s0, #1", CPU_GAP_KIND_RNARROW, 2, 16u, 0u, -1, 1u, 1u, 2u},
    {"sqrshrun h2, s0, #16", CPU_GAP_KIND_RNARROW, 2, 16u, 0u, -1, 1u, 16u, 3u},
    {"sqrshrun s2, d0, #1", CPU_GAP_KIND_RNARROW, 2, 32u, 0u, -1, 1u, 1u, 4u},
    {"sqrshrun s2, d0, #32", CPU_GAP_KIND_RNARROW, 2, 32u, 0u, -1, 1u, 32u, 5u},
};

static uint64_t GapLane(const uint64_t v[2], unsigned bits, unsigned i) {
    const unsigned at = i * bits;
    return (v[at / 64u] >> (at % 64u)) & LaneMask(bits);
}

static void GapSetLane(uint64_t v[2], unsigned bits, unsigned i, uint64_t x) {
    const unsigned at = i * bits;
    v[at / 64u] = (v[at / 64u] & ~(LaneMask(bits) << (at % 64u))) | ((x & LaneMask(bits)) << (at % 64u));
}

void CpuRefGapExpected(const CpuGapForm *form, const uint64_t v0[2], const uint64_t v1[2],
                       const uint64_t v2[2], uint64_t out[2], bool *qc) {
    const unsigned e = form->esize;
    bool saturated;
    out[0] = 0;
    out[1] = 0;
    *qc = false;
    if (form->kind == CPU_GAP_KIND_RSHL) {
        const unsigned lanes = form->scalar ? 1u : 128u / e;
        for (unsigned i = 0; i < lanes; ++i) {
            const int shift = (int8_t)(GapLane(v1, e, i) & 0xffu);
            GapSetLane(out, e, i, CpuRefRoundingShiftLane(GapLane(v0, e, i), shift, e, form->op == 1, &saturated));
            *qc = *qc || saturated;
        }
    } else if (form->kind == CPU_GAP_KIND_DMUL) {
        const unsigned lanes = form->scalar ? 1u : 64u / e;
        for (unsigned i = 0; i < lanes; ++i) {
            const unsigned source = form->upper * (64u / e) + i;
            const uint64_t a = GapLane(v0, e, source);
            const uint64_t b = form->index >= 0 ? GapLane(v1, e, (unsigned)form->index) : GapLane(v1, e, source);
            GapSetLane(out, 2u * e, i, CpuRefDoublingMultiplyLane(GapLane(v2, 2u * e, i), a, b, e, form->op, &saturated));
            *qc = *qc || saturated;
        }
    } else {
        out[0] = CpuRefRoundingNarrow(GapLane(v0, 2u * e, 0), e, form->shift, form->op == 1, form->op != 0, &saturated);
        *qc = saturated;
    }
}

const uint32_t CPU_SM4_KEY[4] = {0xa292ffa1u, 0xdf01febfu, 0x99a12b0fu, 0xc42410ccu};
const uint32_t CPU_SM4_CK[4] = {0x00070e15u, 0x1c232a31u, 0x383f464du, 0x545b6269u};
const uint32_t CPU_SM4_PLAIN[4] = {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u};
const uint32_t CPU_SM4_RK[4] = {0xf12186f9u, 0x41662b61u, 0x5a6ab19au, 0x7ba92077u};
const uint32_t CPU_SM4_X[4] = {0x27fad345u, 0xa18b4cb2u, 0x11c1e22au, 0xcc13e2eeu};
