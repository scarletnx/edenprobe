// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>
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


uint32_t IntMathSecond(uint32_t x, uint32_t y) {
    // The same mix with the axes swapped, so the two operands of a binary mode differ everywhere
    // except on the diagonal.
    return IntMathMix(y, x);
}

float IntMathSmallFloat(uint32_t x, uint32_t y) {
    // Every step here is exact in single precision: a half integer times 3.25 is a multiple of
    // an eighth under 110, and so is the sum. Fused or not, there is one answer.
    return ((float)x - 31.5f) * 3.25f + (float)y * 0.125f;
}

float IntMathUnormInput(uint32_t x, uint32_t y, uint32_t component) {
    // An odd number of 256ths, from just under minus a quarter to just over 1.3, so both clamps
    // are reached. Times 255 that is an odd number of 256ths again, never a half.
    const uint32_t odd = ((x * 4u + y * 3u + component * 37u) % 200u) * 2u + 1u;
    return ((float)odd - 64.0f) * (1.0f / 256.0f);
}

float IntMathSnormInput(uint32_t x, uint32_t y, uint32_t component) {
    // An odd number of 256ths from minus 333 to 333, so beyond minus one and one both ways.
    // 32767 is minus one modulo 256, so times it the fraction is an odd number of 256ths too.
    const int32_t odd = (int32_t)((x * 5u + y * 7u + component * 101u) % 334u) * 2 - 333;
    return (float)odd * (1.0f / 256.0f);
}

uint32_t IntMathHalfPatterns(uint32_t x, uint32_t y) {
    const uint32_t v = IntMathMix(x, y);
    uint32_t out = 0;
    for (uint32_t half = 0; half < 2u; ++half) {
        uint32_t h = (v >> (half * 16u)) & 0xFFFFu;
        const uint32_t exponent = (h >> 10) & 0x1Fu;
        if (exponent == 0u) {
            h |= 1u << 10;
        } else if (exponent == 31u) {
            h &= ~0x3FFu;
        }
        out |= h << (half * 16u);
    }
    return out;
}

void IntMathMinMaxOperands(uint32_t x, uint32_t y, uint32_t *first, uint32_t *second) {
    static const uint32_t ENDS[4] = { 0x80000000u, 0x7FFFFFFFu, 0u, 0xFFFFFFFFu };
    if (y == 63u) {
        *first = ENDS[x & 3u];
        *second = ENDS[(x >> 2) & 3u];
        return;
    }
    *first = IntMathMix(x, y);
    *second = IntMathSecond(x, y);
}

int32_t IntMathDividend(uint32_t x, uint32_t y) {
    // x whole divisors and a remainder of one to one less than the divisor, spread by a
    // multiply and a shift so no division is needed to build the operand of one.
    const int32_t magnitude = (int32_t)(y * 37u + 3u);
    const int32_t remainder =
        1 + (int32_t)((((x * 29u + y * 13u) & 0x7Fu) * (uint32_t)(magnitude - 2)) >> 7);
    const int32_t dividend = (int32_t)x * magnitude + remainder;
    return (x & 2u) != 0u ? -dividend : dividend;
}

int32_t IntMathDivisor(uint32_t y) {
    const int32_t magnitude = (int32_t)(y * 37u + 3u);
    return (y & 1u) != 0u ? -magnitude : magnitude;
}

uint32_t IntMathFieldOffset(uint32_t x) {
    return x & 31u;
}

uint32_t IntMathFieldBits(uint32_t x, uint32_t y) {
    const uint32_t wanted = y % 33u;
    const uint32_t room = 32u - IntMathFieldOffset(x);
    return wanted < room ? wanted : room;
}

/// A magnitude below two to the 32 as a float, rounded to nearest with ties to even, worked out
/// with integers: the top 24 significant bits kept, the rest deciding the rounding.
static uint32_t FloatFromMagnitude(uint32_t magnitude, uint32_t sign) {
    if (magnitude == 0u) {
        return sign;
    }
    const uint32_t top = HighestSetBit(magnitude);
    uint32_t significand;
    uint32_t exponent = top;
    if (top <= 23u) {
        significand = magnitude << (23u - top);
    } else {
        const uint32_t dropped = top - 23u;
        significand = magnitude >> dropped;
        const uint32_t rest = magnitude & ((1u << dropped) - 1u);
        const uint32_t half = 1u << (dropped - 1u);
        if (rest > half || (rest == half && (significand & 1u) != 0u)) {
            ++significand;
            if (significand == (1u << 24)) {
                significand >>= 1;
                ++exponent;
            }
        }
    }
    return sign | ((exponent + 127u) << 23) | (significand & 0x7FFFFFu);
}

uint32_t IntMathFloatFromInt(int32_t value) {
    const uint32_t word = (uint32_t)value;
    // The magnitude of the most negative value is two to the 31, which fits unsigned.
    return (word & 0x80000000u) != 0u ? FloatFromMagnitude(0u - word, 0x80000000u)
                                      : FloatFromMagnitude(word, 0u);
}

uint32_t IntMathFloatFromUint(uint32_t value) {
    return FloatFromMagnitude(value, 0u);
}

uint32_t IntMathPackUnorm4x8(const float components[4]) {
    uint32_t word = 0;
    for (uint32_t i = 0; i < 4u; ++i) {
        double c = (double)components[i];
        c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
        word |= (uint32_t)floor(c * 255.0 + 0.5) << (i * 8u);
    }
    return word;
}

uint32_t IntMathPackSnorm2x16(const float components[2]) {
    uint32_t word = 0;
    for (uint32_t i = 0; i < 2u; ++i) {
        double c = (double)components[i];
        c = c < -1.0 ? -1.0 : (c > 1.0 ? 1.0 : c);
        const double scaled = c * 32767.0;
        const int32_t code = scaled < 0.0 ? -(int32_t)floor(-scaled + 0.5)
                                          : (int32_t)floor(scaled + 0.5);
        word |= ((uint32_t)code & 0xFFFFu) << (i * 16u);
    }
    return word;
}

uint32_t IntMathRoundtripSnorm2x16(uint32_t word) {
    uint32_t out = word;
    for (uint32_t i = 0; i < 2u; ++i) {
        if (((word >> (i * 16u)) & 0xFFFFu) == 0x8000u) {
            out = (out & ~(0xFFFFu << (i * 16u))) | (0x8001u << (i * 16u));
        }
    }
    return out;
}

uint32_t IntMathFloatFromHalfBits(uint32_t half) {
    // Only the patterns IntMathHalfPatterns makes: normal numbers and infinities. The exponent
    // is rebiased from fifteen to 127 and the mantissa moved to the top of the float's.
    const uint32_t sign = (half & 0x8000u) << 16;
    const uint32_t exponent = (half >> 10) & 0x1Fu;
    const uint32_t mantissa = half & 0x3FFu;
    if (exponent == 31u) {
        return sign | 0x7F800000u | (mantissa << 13);
    }
    return sign | ((exponent + 112u) << 23) | (mantissa << 13);
}

uint32_t IntMathBitfieldExtract(uint32_t value, uint32_t offset, uint32_t bits, bool sign) {
    if (bits == 0u) {
        return 0u;
    }
    uint32_t field = 0;
    for (uint32_t bit = 0; bit < bits; ++bit) {
        field |= ((value >> (offset + bit)) & 1u) << bit;
    }
    if (sign && ((field >> (bits - 1u)) & 1u) != 0u) {
        for (uint32_t bit = bits; bit < 32u; ++bit) {
            field |= 1u << bit;
        }
    }
    return field;
}

uint32_t IntMathBitfieldInsert(uint32_t base, uint32_t insert, uint32_t offset, uint32_t bits) {
    uint32_t out = base;
    for (uint32_t bit = 0; bit < bits; ++bit) {
        const uint32_t position = offset + bit;
        out = (out & ~(1u << position)) | (((insert >> bit) & 1u) << position);
    }
    return out;
}

uint32_t IntMathReverse(uint32_t value) {
    uint32_t out = 0;
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        out |= ((value >> bit) & 1u) << (31u - bit);
    }
    return out;
}

uint32_t IntMathFindMsb(uint32_t value, bool sign) {
    // For a negative signed value the language asks for the highest bit that is nought, which is
    // the highest set bit of the complement.
    const uint32_t scanned = sign && (value & 0x80000000u) != 0u ? ~value : value;
    if (scanned == 0u) {
        return 0xFFFFFFFFu;
    }
    return HighestSetBit(scanned);
}

uint32_t IntMathFindLsb(uint32_t value) {
    return value == 0u ? 0xFFFFFFFFu : LowestSetBit(value);
}

// C leaves a shift by 32 or more undefined, which is the very case these are about, so each one
// says what happens there before it lets the processor shift.
uint32_t IntMathShiftLeft(uint32_t value, uint32_t count) {
    return count >= 32u ? 0u : value << count;
}

uint32_t IntMathShiftRight(uint32_t value, uint32_t count) {
    return count >= 32u ? 0u : value >> count;
}

uint32_t IntMathShiftArithmetic(uint32_t value, uint32_t count) {
    const bool negative = (value & 0x80000000u) != 0u;
    if (count >= 32u) {
        return negative ? 0xFFFFFFFFu : 0u;
    }
    if (count == 0u) {
        return value;
    }
    const uint32_t shifted = value >> count;
    return negative ? (shifted | (0xFFFFFFFFu << (32u - count))) : shifted;
}

static uint32_t SignedDivide(int32_t a, int32_t b) {
    // Towards nought, written as the magnitudes' quotient with the sign put back.
    const uint32_t magnitude_a = a < 0 ? 0u - (uint32_t)a : (uint32_t)a;
    const uint32_t magnitude_b = b < 0 ? 0u - (uint32_t)b : (uint32_t)b;
    const uint32_t quotient = magnitude_a / magnitude_b;
    return (a < 0) != (b < 0) ? 0u - quotient : quotient;
}

static uint32_t SignedRemainder(int32_t a, int32_t b) {
    const uint32_t magnitude_a = a < 0 ? 0u - (uint32_t)a : (uint32_t)a;
    const uint32_t magnitude_b = b < 0 ? 0u - (uint32_t)b : (uint32_t)b;
    const uint32_t remainder = magnitude_a % magnitude_b;
    return a < 0 ? 0u - remainder : remainder;
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
    case INTMATH_INT_TO_FLOAT:
        return IntMathFloatFromInt((int32_t)v);
    case INTMATH_UINT_TO_FLOAT:
        return IntMathFloatFromUint(v);
    case INTMATH_FLOAT_TO_INT: {
        // The operand is a multiple of an eighth, so its whole part towards nought is the
        // eighths divided by eight with the remainder dropped, which C's division does.
        const int32_t eighths = (int32_t)(IntMathSmallFloat(x, y) * 8.0f);
        return (uint32_t)(eighths / 8);
    }
    case INTMATH_FLOAT_TO_UINT:
        // A whole number of 256s, so nothing to truncate: the top 24 bits, back where they were.
        return (v >> 8) << 8;
    case INTMATH_PACK_UNORM4X8: {
        float components[4];
        for (uint32_t i = 0; i < 4u; ++i) {
            components[i] = IntMathUnormInput(x, y, i);
        }
        return IntMathPackUnorm4x8(components);
    }
    case INTMATH_ROUNDTRIP_UNORM4X8:
        return v;
    case INTMATH_PACK_SNORM2X16: {
        const float components[2] = { IntMathSnormInput(x, y, 0), IntMathSnormInput(x, y, 1) };
        return IntMathPackSnorm2x16(components);
    }
    case INTMATH_ROUNDTRIP_SNORM2X16:
        return IntMathRoundtripSnorm2x16(v);
    case INTMATH_PACK_HALF2X16:
        return IntMathHalfPatterns(x, y);
    case INTMATH_UNPACK_HALF2X16: {
        const uint32_t h = IntMathHalfPatterns(x, y);
        return IntMathFloatFromHalfBits(h & 0xFFFFu) ^ IntMathFloatFromHalfBits(h >> 16);
    }
    case INTMATH_SHIFT_LEFT_WIDE:
        return IntMathShiftLeft(v, 31u + x % 3u);
    case INTMATH_SHIFT_RIGHT_WIDE:
        return IntMathShiftRight(v, 31u + x % 3u);
    case INTMATH_SHIFT_ARITHMETIC_WIDE:
        return IntMathShiftArithmetic(v, 31u + x % 3u);
    case INTMATH_SHIFT_LEFT_BY_REGISTER:
        return IntMathShiftLeft(v, y);
    case INTMATH_SHIFT_RIGHT_BY_REGISTER:
        return IntMathShiftRight(v, y);
    case INTMATH_SHIFT_ARITHMETIC_BY_REGISTER:
        return IntMathShiftArithmetic(v, y);
    case INTMATH_SIGNED_DIVIDE:
        return SignedDivide(IntMathDividend(x, y), IntMathDivisor(y));
    case INTMATH_SIGNED_REMAINDER:
        return SignedRemainder(IntMathDividend(x, y), IntMathDivisor(y));
    case INTMATH_FIND_MSB_UINT:
        return IntMathFindMsb(v >> (x & 31u), false);
    case INTMATH_FIND_MSB_INT:
        return IntMathFindMsb(IntMathShiftArithmetic(v, x & 31u), true);
    case INTMATH_FIND_LSB:
        return IntMathFindLsb(v << (x & 31u));
    case INTMATH_BITFIELD_EXTRACT_UINT:
        return IntMathBitfieldExtract(v, IntMathFieldOffset(x), IntMathFieldBits(x, y), false);
    case INTMATH_BITFIELD_EXTRACT_INT:
        return IntMathBitfieldExtract(v, IntMathFieldOffset(x), IntMathFieldBits(x, y), true);
    case INTMATH_BITFIELD_INSERT:
        return IntMathBitfieldInsert(v, IntMathSecond(x, y), IntMathFieldOffset(x),
                                     IntMathFieldBits(x, y));
    case INTMATH_BITFIELD_REVERSE:
        return IntMathReverse(v);
    case INTMATH_MIN_SIGNED: {
        uint32_t a;
        uint32_t w;
        IntMathMinMaxOperands(x, y, &a, &w);
        return (int32_t)a < (int32_t)w ? a : w;
    }
    case INTMATH_MAX_UNSIGNED: {
        uint32_t a;
        uint32_t w;
        IntMathMinMaxOperands(x, y, &a, &w);
        return a > w ? a : w;
    }
    case INTMATH_CLAMP_SIGNED: {
        uint32_t a;
        uint32_t w;
        IntMathMinMaxOperands(x, y, &a, &w);
        const int32_t value = (int32_t)a;
        return (uint32_t)(value < INTMATH_CLAMP_SIGNED_LOW    ? INTMATH_CLAMP_SIGNED_LOW
                          : value > INTMATH_CLAMP_SIGNED_HIGH ? INTMATH_CLAMP_SIGNED_HIGH
                                                              : value);
    }
    case INTMATH_CLAMP_UNSIGNED: {
        uint32_t a;
        uint32_t w;
        IntMathMinMaxOperands(x, y, &a, &w);
        return a < INTMATH_CLAMP_UNSIGNED_LOW    ? INTMATH_CLAMP_UNSIGNED_LOW
               : a > INTMATH_CLAMP_UNSIGNED_HIGH ? INTMATH_CLAMP_UNSIGNED_HIGH
                                                 : a;
    }
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
    case INTMATH_INT_TO_FLOAT:
        return "a signed integer converts to the nearest float, ties to even";
    case INTMATH_UINT_TO_FLOAT:
        return "an unsigned integer converts to the nearest float, ties to even";
    case INTMATH_FLOAT_TO_INT:
        return "a float converts to a signed integer towards nought";
    case INTMATH_FLOAT_TO_UINT:
        return "a float above two to the 31 converts to an unsigned integer";
    case INTMATH_PACK_UNORM4X8:
        return "packUnorm4x8 clamps and rounds each component";
    case INTMATH_ROUNDTRIP_UNORM4X8:
        return "unpackUnorm4x8 then packUnorm4x8 gives the word back";
    case INTMATH_PACK_SNORM2X16:
        return "packSnorm2x16 clamps and rounds each component";
    case INTMATH_ROUNDTRIP_SNORM2X16:
        return "unpackSnorm2x16 then packSnorm2x16 gives the word back, 0x8000 as 0x8001";
    case INTMATH_PACK_HALF2X16:
        return "packHalf2x16 of floats that are exactly half precision";
    case INTMATH_UNPACK_HALF2X16:
        return "unpackHalf2x16 of normal halves and infinities";
    case INTMATH_SHIFT_LEFT_WIDE:
        return "a left shift by 31, 32 and 33 in a register clamps the count to 32";
    case INTMATH_SHIFT_RIGHT_WIDE:
        return "an unsigned right shift by 31, 32 and 33 clamps the count to 32";
    case INTMATH_SHIFT_ARITHMETIC_WIDE:
        return "a signed right shift by 31, 32 and 33 clamps the count to 32";
    case INTMATH_SHIFT_LEFT_BY_REGISTER:
        return "a left shift by every count from nought to 63";
    case INTMATH_SHIFT_RIGHT_BY_REGISTER:
        return "an unsigned right shift by every count from nought to 63";
    case INTMATH_SHIFT_ARITHMETIC_BY_REGISTER:
        return "a signed right shift by every count from nought to 63";
    case INTMATH_SIGNED_DIVIDE:
        return "signed division with negative operands truncates towards nought";
    case INTMATH_SIGNED_REMAINDER:
        return "signed remainder with negative operands takes the dividend's sign";
    case INTMATH_FIND_MSB_UINT:
        return "findMSB of an unsigned value, nought included";
    case INTMATH_FIND_MSB_INT:
        return "findMSB of a signed value, minus one and nought included";
    case INTMATH_FIND_LSB:
        return "findLSB, nought included";
    case INTMATH_BITFIELD_EXTRACT_UINT:
        return "bitfieldExtract of an unsigned value at every offset and width";
    case INTMATH_BITFIELD_EXTRACT_INT:
        return "bitfieldExtract of a signed value extends the field's sign";
    case INTMATH_BITFIELD_INSERT:
        return "bitfieldInsert at every offset and width";
    case INTMATH_BITFIELD_REVERSE:
        return "bitfieldReverse";
    case INTMATH_MIN_SIGNED:
        return "the signed minimum";
    case INTMATH_MAX_UNSIGNED:
        return "the unsigned maximum";
    case INTMATH_CLAMP_SIGNED:
        return "a signed clamp";
    case INTMATH_CLAMP_UNSIGNED:
        return "an unsigned clamp";
    case INTMATH_MODE_COUNT:
        break;
    }
    return "unknown";
}
