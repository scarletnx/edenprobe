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

const uint32_t CPU_SM4_KEY[4] = {0xa292ffa1u, 0xdf01febfu, 0x99a12b0fu, 0xc42410ccu};
const uint32_t CPU_SM4_CK[4] = {0x00070e15u, 0x1c232a31u, 0x383f464du, 0x545b6269u};
const uint32_t CPU_SM4_PLAIN[4] = {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u};
const uint32_t CPU_SM4_RK[4] = {0xf12186f9u, 0x41662b61u, 0x5a6ab19au, 0x7ba92077u};
const uint32_t CPU_SM4_X[4] = {0x27fad345u, 0xa18b4cb2u, 0x11c1e22au, 0xcc13e2eeu};
