// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>

#include "intref.h"

// Deliberately written without the processor's own bit counting builtins: this file says what
// the answer is in terms a reader can check by hand, not how the hardware gets there.

uint32_t IntMathMix(uint32_t x, uint32_t y) {
    // Both products overflow 32 bits over most of the target and the sum overflows again, so
    // anything that quietly widens or narrows the arithmetic disagrees nearly everywhere.
    return x * 1664525u + y * 1013904223u;
}

/// The position of the lowest set bit, counting from nought. Undefined for nought, which the
/// caller rules out by setting a bit first.
static uint32_t LowestSetBit(uint32_t value) {
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        if ((value >> bit) & 1u) {
            return bit;
        }
    }
    return 0u;
}

/// The position of the highest set bit. Same condition, same guard on the caller's side.
static uint32_t HighestSetBit(uint32_t value) {
    for (uint32_t bit = 32u; bit > 0u; --bit) {
        if ((value >> (bit - 1u)) & 1u) {
            return bit - 1u;
        }
    }
    return 0u;
}

static uint32_t CountSetBits(uint32_t value) {
    uint32_t count = 0;
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        count += (value >> bit) & 1u;
    }
    return count;
}

/// The bit pattern of a float, which is what the fragment coordinate cases compare.
static uint32_t FloatBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint32_t IntMathReference(IntMathMode mode, uint32_t x, uint32_t y, uint32_t probe) {
    const uint32_t v = IntMathMix(x, y);
    switch (mode) {
    case INTMATH_FRAGCOORD_X:
        // Plus a half, because the fragment coordinate names the centre of the pixel. Comparing
        // the bit pattern is what makes the half something a case can fail on: a corner would
        // otherwise truncate back to the same integer.
        return FloatBits((float)x + 0.5f);
    case INTMATH_FRAGCOORD_Y:
        return FloatBits((float)y + 0.5f);
    case INTMATH_UNIFORM:
        return probe;
    case INTMATH_MIX:
        return v;
    case INTMATH_SHIFT_LEFT:
        return v << 13u;
    case INTMATH_SHIFT_RIGHT:
        return v >> 7u;
    case INTMATH_SHIFT_ARITHMETIC: {
        // Written out rather than shifting a signed value: C leaves a negative right shift to
        // the compiler, while the shading language requires the sign bit to be extended.
        const uint32_t shifted = v >> 7u;
        const uint32_t sign_fill = 0xFFFFFFFFu << (32u - 7u);
        return (v & 0x80000000u) != 0u ? (shifted | sign_fill) : shifted;
    }
    case INTMATH_AND:
        return v & 0xF0F0F0F0u;
    case INTMATH_OR:
        return v | 0x0F0F0F0Fu;
    case INTMATH_XOR:
        return v ^ 0xA5A5A5A5u;
    case INTMATH_BIT_COUNT:
        return CountSetBits(v);
    case INTMATH_BIT_SCAN:
        // The top bit is forced on for the low scan and the bottom bit for the high one, so
        // neither is ever asked about a value with no bits set, which the shading language
        // leaves undefined.
        return LowestSetBit(v | 0x80000000u) * 32u + HighestSetBit(v | 1u);
    case INTMATH_DIVIDE:
        return v / INTMATH_DIVISOR;
    case INTMATH_REMAINDER:
        return v % INTMATH_DIVISOR;
    case INTMATH_MODE_COUNT:
        break;
    }
    return 0u;
}

const char *IntMathName(IntMathMode mode) {
    switch (mode) {
    case INTMATH_FRAGCOORD_X:
        return "the fragment coordinate's x is the pixel centre, bit for bit";
    case INTMATH_FRAGCOORD_Y:
        return "the fragment coordinate's y is the pixel centre, bit for bit";
    case INTMATH_UNIFORM:
        return "the uniform buffer delivers its value unaltered";
    case INTMATH_MIX:
        return "a multiply and an add that both overflow 32 bits";
    case INTMATH_SHIFT_LEFT:
        return "a left shift discards the bits it pushes out";
    case INTMATH_SHIFT_RIGHT:
        return "an unsigned right shift brings in noughts";
    case INTMATH_SHIFT_ARITHMETIC:
        return "a signed right shift extends the sign bit";
    case INTMATH_AND:
        return "bitwise and";
    case INTMATH_OR:
        return "bitwise or";
    case INTMATH_XOR:
        return "bitwise exclusive or";
    case INTMATH_BIT_COUNT:
        return "counting the set bits";
    case INTMATH_BIT_SCAN:
        return "finding the lowest and the highest set bit";
    case INTMATH_DIVIDE:
        return "unsigned division by a prime";
    case INTMATH_REMAINDER:
        return "unsigned remainder by a prime";
    case INTMATH_MODE_COUNT:
        break;
    }
    return "unknown";
}
