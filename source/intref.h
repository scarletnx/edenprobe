// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the integer fragment shader is supposed to produce, worked out on the processor.
///
/// This file and source/shaders/intmath.frag.glsl are two statements of the same arithmetic, one
/// for each machine, and the whole value of the suite is that they were written separately and
/// have to agree. The enumeration's numbers are the mode the shader switches on, so they are a
/// contract between the two files and cannot be reordered on one side alone.
typedef enum {
    /// The bit pattern of the fragment coordinate itself, one axis each. Not arithmetic: these
    /// two are what establish that the shader is being told where it is, including the half
    /// pixel that puts the coordinate at the centre rather than at the corner.
    INTMATH_FRAGCOORD_X = 0,
    INTMATH_FRAGCOORD_Y = 1,
    /// The value out of the uniform buffer, unaltered. The guard case: every mode below reads
    /// the mode out of that same buffer, so if this one is wrong none of them is measuring what
    /// its name says.
    INTMATH_UNIFORM = 2,
    /// A multiply and an add that both overflow 32 bits, which is the value every mode below
    /// starts from.
    INTMATH_MIX = 3,
    INTMATH_SHIFT_LEFT = 4,
    INTMATH_SHIFT_RIGHT = 5,
    /// The same shift on a signed value, which extends the sign bit instead of shifting in
    /// noughts. A translator that loses the distinction gets every negative input wrong and no
    /// positive one, which is why the sweep over a whole target matters here.
    INTMATH_SHIFT_ARITHMETIC = 6,
    INTMATH_AND = 7,
    INTMATH_OR = 8,
    INTMATH_XOR = 9,
    INTMATH_BIT_COUNT = 10,
    /// The lowest set bit times 32 plus the highest, so one value carries both scans.
    INTMATH_BIT_SCAN = 11,
    /// Division and remainder by a prime, which the hardware has no single instruction for and
    /// has to synthesise.
    INTMATH_DIVIDE = 12,
    INTMATH_REMAINDER = 13,
    /// Conversions. Integer to float rounds to nearest with ties to even, which is what a
    /// correctly rounded conversion means; float to integer truncates towards nought.
    INTMATH_INT_TO_FLOAT = 14,
    INTMATH_UINT_TO_FLOAT = 15,
    /// Small operands with a fraction, both signs, so truncation towards nought is judged.
    INTMATH_FLOAT_TO_INT = 16,
    /// Whole floats up to just under two to the 32, so the top bit of an unsigned result is.
    INTMATH_FLOAT_TO_UINT = 17,
    /// The packing functions. Every input here lands at least 1/256 of a code away from a
    /// halfway point, so the language's own "round" has one answer.
    INTMATH_PACK_UNORM4X8 = 18,
    /// Unpacking and packing again gives back the same word, whatever precision the division
    /// inside the unpack has, since it cannot move a code by half.
    INTMATH_ROUNDTRIP_UNORM4X8 = 19,
    INTMATH_PACK_SNORM2X16 = 20,
    /// The same, except that a half holding 0x8000 comes back as 0x8001: minus 32768 unpacks
    /// below minus one, is clamped to it, and minus one packs as minus 32767.
    INTMATH_ROUNDTRIP_SNORM2X16 = 21,
    /// Floats built bit by bit from half precision patterns, so the conversion has an exact
    /// answer. No half precision denormal and no NaN, which a machine may flush or requiet.
    INTMATH_PACK_HALF2X16 = 22,
    /// The two floats unpacked from such a word, combined by exclusive or so both are judged.
    INTMATH_UNPACK_HALF2X16 = 23,
    /// Shifts by 31, 32 and 33, the count in a register. The language leaves a count of 32 or
    /// more undefined, so the rule is the console's: its compiler emits these as shifts without
    /// the wrap flag (read out of the compiled shader, see the README), and such a shift clamps
    /// the count to 32, so 32 and up shift everything out: nought, or the sign for a signed
    /// right shift. That is PTX's definition of the same instructions.
    INTMATH_SHIFT_LEFT_WIDE = 24,
    INTMATH_SHIFT_RIGHT_WIDE = 25,
    INTMATH_SHIFT_ARITHMETIC_WIDE = 26,
    /// Every count from nought to 63, one per row.
    INTMATH_SHIFT_LEFT_BY_REGISTER = 27,
    INTMATH_SHIFT_RIGHT_BY_REGISTER = 28,
    INTMATH_SHIFT_ARITHMETIC_BY_REGISTER = 29,
    /// Signed division and remainder with every combination of signs. Division truncates
    /// towards nought and the remainder takes the sign of the dividend. The console's compiler
    /// builds both out of a float reciprocal (two conversions, a reciprocal, a multiply and a
    /// truncation, read out of the compiled shader), so the operands are built to keep that
    /// honest: the dividend is under two to the 18 and never a multiple of the divisor, so the
    /// true quotient is at least one part in 2334 from a whole number and a float error of a
    /// few units in the last place cannot move the truncation.
    INTMATH_SIGNED_DIVIDE = 30,
    INTMATH_SIGNED_REMAINDER = 31,
    /// The bit scans on inputs that include nought and minus one, which answer minus one.
    INTMATH_FIND_MSB_UINT = 32,
    INTMATH_FIND_MSB_INT = 33,
    INTMATH_FIND_LSB = 34,
    /// Every offset, and every width that fits beside it including nought and the whole word.
    INTMATH_BITFIELD_EXTRACT_UINT = 35,
    INTMATH_BITFIELD_EXTRACT_INT = 36,
    INTMATH_BITFIELD_INSERT = 37,
    INTMATH_BITFIELD_REVERSE = 38,
    /// The minimum, maximum and clamps. The last row swaps the mix for the ends of the signed
    /// range, nought and minus one (IntMathMinMaxOperands), so a minimum built as the negated
    /// maximum of the negations, which overflows at the most negative value, is caught.
    INTMATH_MIN_SIGNED = 39,
    INTMATH_MAX_UNSIGNED = 40,
    INTMATH_CLAMP_SIGNED = 41,
    INTMATH_CLAMP_UNSIGNED = 42,
    INTMATH_MODE_COUNT = 43,
} IntMathMode;

/// The divisor the last two modes use. A prime near a million, so the quotient varies across the
/// whole target rather than being nought or one everywhere.
#define INTMATH_DIVISOR 1000003u

/// The bounds the two clamp modes use.
#define INTMATH_CLAMP_SIGNED_LOW (-0x10000000)
#define INTMATH_CLAMP_SIGNED_HIGH 0x20000000
#define INTMATH_CLAMP_UNSIGNED_LOW 0x40000000u
#define INTMATH_CLAMP_UNSIGNED_HIGH 0xC0000000u

/// The operands the newer modes derive from the coordinate, one function each so the reference
/// suite can pin them. The shader writes the same arithmetic.
uint32_t IntMathSecond(uint32_t x, uint32_t y);
/// The two operands of the minimum, maximum and clamp modes: the mix and IntMathSecond, except on
/// the last row, where each is one of the most negative value, the most positive, nought and
/// minus one, every pairing across the row.
void IntMathMinMaxOperands(uint32_t x, uint32_t y, uint32_t *first, uint32_t *second);
float IntMathSmallFloat(uint32_t x, uint32_t y);
float IntMathUnormInput(uint32_t x, uint32_t y, uint32_t component);
float IntMathSnormInput(uint32_t x, uint32_t y, uint32_t component);
/// The word a mode's half precision patterns come from: the mix with every exponent of nought
/// raised to one and every NaN made an infinity.
uint32_t IntMathHalfPatterns(uint32_t x, uint32_t y);
int32_t IntMathDivisor(uint32_t y);
int32_t IntMathDividend(uint32_t x, uint32_t y);
uint32_t IntMathFieldOffset(uint32_t x);
uint32_t IntMathFieldBits(uint32_t x, uint32_t y);

/// The rules underneath, each on its own for the reference suite.
uint32_t IntMathFloatFromInt(int32_t value);
uint32_t IntMathFloatFromUint(uint32_t value);
uint32_t IntMathPackUnorm4x8(const float components[4]);
uint32_t IntMathPackSnorm2x16(const float components[2]);
uint32_t IntMathRoundtripSnorm2x16(uint32_t word);
uint32_t IntMathFloatFromHalfBits(uint32_t half);
uint32_t IntMathBitfieldExtract(uint32_t value, uint32_t offset, uint32_t bits, bool sign);
uint32_t IntMathBitfieldInsert(uint32_t base, uint32_t insert, uint32_t offset, uint32_t bits);
uint32_t IntMathReverse(uint32_t value);
/// The highest set bit, the highest bit that differs from the sign for a signed value, and the
/// lowest set bit, each minus one as an unsigned word when there is none.
uint32_t IntMathFindMsb(uint32_t value, bool sign);
uint32_t IntMathFindLsb(uint32_t value);
/// The three shifts with a count that is clamped to 32.
uint32_t IntMathShiftLeft(uint32_t value, uint32_t count);
uint32_t IntMathShiftRight(uint32_t value, uint32_t count);
uint32_t IntMathShiftArithmetic(uint32_t value, uint32_t count);

/// The answer for the pixel at @p x, @p y, with @p probe as whatever the uniform buffer carries.
uint32_t IntMathReference(IntMathMode mode, uint32_t x, uint32_t y, uint32_t probe);

/// The mixed value every arithmetic mode starts from, exposed so a self-check can state it.
uint32_t IntMathMix(uint32_t x, uint32_t y);

/// The name that goes on the case's line.
const char *IntMathName(IntMathMode mode);

#ifdef __cplusplus
}
#endif
