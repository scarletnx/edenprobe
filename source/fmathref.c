// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <float.h>
#include <math.h>
#include <string.h>

#include "check.h"
#include "fmathref.h"

// The multiplier the multiply and add case uses. Written the same way in both files, and it is
// the nearest float to a third either way: the shading language divides two float literals and
// the processor rounds a double quotient, and both land on the same 24 bits.
#define ONE_THIRD ((float)(1.0 / 3.0))

// Multiplied by a sixty fourth, written the same way the shader writes it. A sixty fourth is
// exact in binary and so is the product of it with a whole number up to 64, so both sides form
// the same operand with no rounding anywhere.
float FMathInputA(uint32_t x) {
    return ((float)x + 1.0f) * (1.0f / 64.0f);
}

float FMathInputB(uint32_t y) {
    return ((float)y + 1.0f) * (1.0f / 64.0f);
}

static float BitsFloat(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t FloatBitsOf(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// A full 23 bit mantissa for the logarithm's operands, with its lowest bit set, so an
/// implementation that drops the low bits of the operand is measured and not waved through.
/// Shifts and exclusive ors only: an integer multiply in the float shader would make it depend
/// on the integer translator, and an emulator missing one would lose every float case with it.
static uint32_t LogMantissa(uint32_t x, uint32_t y) {
    return (((x << 17) ^ (y << 11) ^ (x << 5) ^ (y << 1) ^ 0x5A5A5Au) & 0x7FFFFFu) | 1u;
}

float FMathArgument(FMathMode mode, uint32_t x, uint32_t y) {
    const float a = FMathInputA(x);
    switch (mode) {
    case FMATH_EXP2_EDGES:
        // Four per column from -128 and a sixteenth per row: -128 to 127.9375, every value a
        // multiple of a sixteenth and so exact.
        return (float)x * 4.0f - 128.0f + (float)y * (1.0f / 16.0f);
    case FMATH_LOG2_EDGES:
        // Built from its bits: exponent fields 1, 5, ... 125 and 129, ... 253, which skips 126
        // and 127 and with them everything from a half to two, and a full mantissa.
        return BitsFloat(((x * 4u + 1u) << 23) | LogMantissa(x, y));
    case FMATH_SQRT_TINY:
    case FMATH_INVERSESQRT_TINY:
        // Exponent fields nought to 31, so the first two columns are denormals, and a mantissa
        // with its lowest bit set so no operand is nought.
        return BitsFloat(((x >> 1) << 23) | (y << 17) | 1u);
    case FMATH_FLOOR:
    case FMATH_FRACT:
        return (float)x - 32.0f + (float)y * (1.0f / 64.0f);
    case FMATH_ROUND:
    case FMATH_ROUND_EVEN:
        // Quarters, so a quarter of the pixels sit exactly on a half.
        return (float)x - 32.0f + (float)(y & 3u) * 0.25f;
    case FMATH_EXP2:
        // Eight times a whole number over 64, less four. Exact in single precision, and the
        // magnitude of it is what the power of two's budget grows with.
        return a * 8.0f - 4.0f;
    case FMATH_LOG2:
        // Exponent fields 118 to 125, so from two to the -9 to just under a half, and a full
        // mantissa. The table bounds the logarithm by units in the last place only outside the
        // range from a half to two, and by an absolute error inside it, so every operand here is
        // below a half and one budget covers them all.
        return BitsFloat(((118u + (x & 7u)) << 23) | LogMantissa(x, y));
    case FMATH_SINE:
    case FMATH_COSINE:
        // Six times a number at most one, less three, so the range is inside minus pi to pi,
        // which is the only range the table bounds these two over at all.
        return a * 6.0f - 3.0f;
    default:
        return a;
    }
}

float FMathReference(FMathMode mode, uint32_t x, uint32_t y, float probe) {
    const float a = FMathInputA(x);
    const float b = FMathInputB(y);

    switch (mode) {
    case FMATH_UNIFORM:
        return probe;
    case FMATH_ADD:
        // Two floats added in double is exact, so rounding that back to a float gives the same
        // answer a float addition would: there is no double rounding to worry about here, which
        // is what lets this case ask for no tolerance at all.
        return (float)((double)a + (double)b);
    case FMATH_MULTIPLY:
        // The same argument: the product of two 24 bit numbers fits in 53 bits exactly.
        return (float)((double)a * (double)b);
    case FMATH_MULTIPLY_ADD:
        // The processor's own fused multiply and add, rounded once, which is what the console's
        // FFMA does. The budget is nought: a machine that multiplies and then adds rounds twice,
        // differs from the console, and the case names it when it does.
        return fmaf(a, ONE_THIRD, b);
    case FMATH_DIVIDE:
        return (float)((double)a / (double)b);
    case FMATH_SQUARE_ROOT:
        return (float)sqrt((double)a);
    case FMATH_EXP2:
        return (float)exp2((double)FMathArgument(mode, x, y));
    case FMATH_LOG2:
        return (float)log2((double)FMathArgument(mode, x, y));
    case FMATH_SINE:
        return (float)sin((double)FMathArgument(mode, x, y));
    case FMATH_COSINE:
        return (float)cos((double)FMathArgument(mode, x, y));
    case FMATH_FMA_CANCELLATION:
        // The processor's fused multiply and add, which the reference suite proves is fused: the
        // exact product less its rounded value with the low twelve bits cleared. Exact itself,
        // and it keeps the product's rounding error, which an unfused machine loses.
        return fmaf(a, ONE_THIRD, -FMathCancelledProduct(x));
    case FMATH_MIN_MAX_INFINITY: {
        static const float answers[4] = { 0.0f, 0.0f, -INFINITY, INFINITY };
        const uint32_t row = y & 3u;
        return row < 2u ? a : answers[row];
    }
    case FMATH_MIN_MAX_NAN:
        return (y & 3u) == 3u ? -a : a;
    case FMATH_MIX_INFINITY_NAN:
        switch (y & 3u) {
        case 0u:
            return NAN; // a times one plus infinity times nought
        case 1u:
            return INFINITY;
        case 2u:
            return -INFINITY;
        default:
            return NAN;
        }
    case FMATH_DENORMAL:
        // The console's answers: every instruction in its binary flushes denormals, so a
        // denormal result is nought and a denormal operand is nought before it is used.
        switch (y & 3u) {
        case 2u:
            return FLT_MIN;
        default:
            return 0.0f;
        }
    case FMATH_EXP2_EDGES: {
        // Below the smallest normal the console's MUFU flushes the answer to nought.
        const float kept = (float)exp2((double)FMathArgument(mode, x, y));
        return kept < FLT_MIN ? 0.0f : kept;
    }
    case FMATH_LOG2_EDGES:
        return (float)log2((double)FMathArgument(mode, x, y));
    case FMATH_SQRT_TINY:
        // A denormal operand is flushed to nought first, as the console's MUFU does.
        if ((x >> 1) == 0u) {
            return 0.0f;
        }
        return (float)sqrt((double)FMathArgument(mode, x, y));
    case FMATH_INVERSESQRT_TINY:
        if ((x >> 1) == 0u) {
            return INFINITY;
        }
        return (float)(1.0 / sqrt((double)FMathArgument(mode, x, y)));
    case FMATH_FLOOR:
        return floorf(FMathArgument(mode, x, y));
    case FMATH_FRACT: {
        const float argument = FMathArgument(mode, x, y);
        return argument - floorf(argument);
    }
    // The language lets round take a half either way; the console's binary compiles it to the
    // same round to nearest even as roundEven (an F2F with rounding mode nought), so both modes
    // have that one answer.
    case FMATH_ROUND:
    case FMATH_ROUND_EVEN: {
        const float argument = FMathArgument(mode, x, y);
        const float down = floorf(argument);
        const float fraction = argument - down;
        float rounded = down;
        if (fraction > 0.5f) {
            rounded = down + 1.0f;
        } else if (fraction == 0.5f) {
            // The even one of the two: floor is even when half of it is a whole number.
            rounded = floorf(down * 0.5f) * 2.0f == down ? down : down + 1.0f;
        }
        // Rounding keeps the sign, so -0.25 rounds to minus nought, not to nought.
        return rounded == 0.0f ? copysignf(0.0f, argument) : rounded;
    }
    case FMATH_MODE_COUNT:
        break;
    }
    return 0.0f;
}

float FMathCancelledProduct(uint32_t x) {
    const float product = FMathInputA(x) * ONE_THIRD;
    uint32_t bits;
    memcpy(&bits, &product, sizeof(bits));
    return BitsFloat(bits & 0xFFFFF000u);
}

bool FMathOther(FMathMode mode, uint32_t x, uint32_t y, float *out) {
    const float a = FMathInputA(x);
    switch (mode) {
    case FMATH_MULTIPLY_ADD: {
        // Multiplied and added apart: two roundings. volatile keeps the compiler from fusing
        // them back into one.
        volatile float product = a * ONE_THIRD;
        *out = product + FMathInputB(y);
        return true;
    }
    case FMATH_FMA_CANCELLATION: {
        volatile float product = a * ONE_THIRD;
        *out = product - FMathCancelledProduct(x);
        return true;
    }
    case FMATH_DENORMAL:
        // Denormals kept: every one of these is exact in single precision then.
        switch (y & 3u) {
        case 0u:
            *out = (a * 0x1p-65f) * 0x1p-65f;
            break;
        case 1u:
            *out = ((a * 0x1p-65f) * 0x1p-65f) * 0x1p40f;
            break;
        case 2u:
            *out = BitsFloat(x + 1u) + FLT_MIN;
            break;
        default:
            *out = BitsFloat((x + 1u) << 4) * 8388608.0f;
            break;
        }
        return true;
    case FMATH_EXP2_EDGES: {
        const float kept = (float)exp2((double)FMathArgument(mode, x, y));
        if (kept < FLT_MIN) {
            *out = kept;
            return true;
        }
        return false;
    }
    case FMATH_SQRT_TINY:
    case FMATH_INVERSESQRT_TINY: {
        if ((x >> 1) != 0u) {
            return false;
        }
        const double root = sqrt((double)FMathArgument(mode, x, y));
        *out = mode == FMATH_SQRT_TINY ? (float)root : (float)(1.0 / root);
        return true;
    }
    case FMATH_ROUND:
    case FMATH_ROUND_EVEN: {
        const float argument = FMathArgument(mode, x, y);
        const float down = floorf(argument);
        const float want = FMathReference(mode, x, y, 0.0f);
        if (want == 0.0f && signbit(want)) {
            // A negative operand that rounds to nought: the console's F2F keeps the sign, and
            // a machine that does not gives plus nought.
            *out = 0.0f;
            return true;
        }
        if (mode == FMATH_ROUND_EVEN || argument - down != 0.5f) {
            return false;
        }
        // Away from nought, the other way the language allows round to go at a half.
        *out = argument > 0.0f ? down + 1.0f : down;
        return true;
    }
    default:
        return false;
    }
}

const char *FMathOtherName(FMathMode mode) {
    switch (mode) {
    case FMATH_MULTIPLY_ADD:
    case FMATH_FMA_CANCELLATION:
        return "the multiply and the add done apart";
    case FMATH_DENORMAL:
    case FMATH_EXP2_EDGES:
    case FMATH_SQRT_TINY:
    case FMATH_INVERSESQRT_TINY:
        return "denormals kept";
    case FMATH_ROUND:
        return "halves rounded away from nought, or +0 where the console keeps -0";
    case FMATH_ROUND_EVEN:
        return "+0 where the console keeps -0";
    default:
        return "";
    }
}

FMathVerdict FMathJudge(FMathMode mode, uint32_t x, uint32_t y, float probe, float got) {
    const float want = FMathReference(mode, x, y, probe);
    const FMathBudget budget = FMathBudgetFor(mode);
    FMathVerdict verdict = { false, false, 0.0f, 0.0f };

    if (isnan(want)) {
        // Any NaN: the payload of one is nobody's business here.
        verdict.within = isnan(got);
        verdict.error = verdict.within ? 0.0f : INFINITY;
    } else if (budget.absolute) {
        verdict.within = CheckFloatAbs(got, want, budget.limit);
        verdict.error = isnan(got) || isinf(got) ? INFINITY : fabsf(got - want);
    } else if (want == 0.0f) {
        // A nought whose sign is defined: a flushed positive, x less x, or a rounding that
        // keeps the operand's sign. The other nought is a wrong answer, not a close one.
        verdict.within = FloatBitsOf(got) == FloatBitsOf(want);
        verdict.error = verdict.within ? 0.0f : INFINITY;
    } else {
        verdict.allowed = FMathAllowedUlps(mode, x, y);
        verdict.error = CheckFloatUlpError(got, want);
        verdict.within = verdict.error <= verdict.allowed;
    }

    // Not a second chance: only a name for what a wrong pixel holds, when it is the answer the
    // tables would allow and the console's binary does not give.
    float other = 0.0f;
    if (!verdict.within && FMathOther(mode, x, y, &other)) {
        verdict.other = FloatBitsOf(got) == FloatBitsOf(other);
    }
    return verdict;
}

FMathBudget FMathBudgetFor(FMathMode mode) {
    switch (mode) {
    case FMATH_UNIFORM:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: nothing is computed, the value is "
                              "copied through a uniform buffer and written out again" };
    case FMATH_ADD:
    case FMATH_MULTIPLY:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: the core table requires addition and "
                              "multiplication to be correctly rounded, so there is one right "
                              "answer, and the reference reaches it exactly because the sum and "
                              "the product of two single precision numbers are exact in double "
                              "precision" };
    case FMATH_MULTIPLY_ADD:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: the console's binary is one fused FFMA, "
                              "which rounds once, and the reference is the processor's fused "
                              "multiply and add, the same single rounding. The table would let "
                              "an unfused pair round twice, up to 1.5 units off; a machine that "
                              "splits it differs from the console, and the case says so" };
    case FMATH_DIVIDE:
        return (FMathBudget){ false, 3.0f, 0.0f, 0.0f,
                              "3 units in the last place: 2.5 for division in the core table, "
                              "plus the half the reference carries for being a double precision "
                              "answer rounded to single" };
    case FMATH_SQUARE_ROOT:
        return (FMathBudget){ false, 5.0f, 0.0f, 0.0f,
                              "5 units in the last place: the table gives the square root no "
                              "figure of its own and inherits it from one over the inverse "
                              "square root, which is 2 for the inverse square root and 2.5 for "
                              "the division, plus the half the reference carries. Three would be "
                              "a number from somewhere else" };
    case FMATH_EXP2:
        // The only budget here that is not a constant. Written as the table writes it.
        return (FMathBudget){ false, 3.5f, 2.0f, 0.0f,
                              "3.5 plus 2 per unit of the argument: the table allows three plus "
                              "twice the magnitude of the argument, and the half the reference "
                              "carries goes on the constant. Over the range this case sweeps "
                              "that is three and a half at the smallest argument and eleven and "
                              "a half at the largest, and a flat three would fail a machine "
                              "that was inside its allowance everywhere but the middle" };
    case FMATH_LOG2:
        return (FMathBudget){ false, 3.5f, 0.0f, 0.0f,
                              "3.5 units in the last place: 3 from the table, which is the "
                              "figure that applies outside the range from a half to two, where "
                              "this operand is kept, plus the half the reference carries" };
    case FMATH_SINE:
    case FMATH_COSINE:
        // The reference's own error is under a ten millionth, more than a thousand times smaller
        // than the budget, so it does not need adding on.
        return (FMathBudget){ true, 0.0f, 0.0f, 1.0f / 2048.0f,
                              "0.000488281 absolute, which is two to the minus eleven: the table "
                              "bounds these two by an absolute error inside minus pi to pi and "
                              "not by units in the last place at all. The shading language's own "
                              "specification leaves them undefined, which is the clearest reason "
                              "this suite holds itself to the other one" };
    case FMATH_FMA_CANCELLATION:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: the console's FFMA is fused, and the "
                              "exact product less a nearby float is exact itself, so there is "
                              "one answer. Done apart the product rounds first and the answer "
                              "loses its low bits, which is a difference from the console" };
    case FMATH_MIN_MAX_INFINITY:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: a minimum or a maximum only picks one "
                              "of its operands, and against an infinity which one is defined" };
    case FMATH_MIN_MAX_NAN:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: the console's minimum and maximum hand "
                              "back the operand that is not a NaN, as GLSL.std.450's NMin and "
                              "NMax define it. The plain FMin a translator may reach for leaves "
                              "this undefined, so a NaN here is a difference from the console, "
                              "which is the machine an emulator answers to" };
    case FMATH_MIX_INFINITY_NAN:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: each answer is the infinity or the NaN "
                              "that x times one less a plus y times a gives in IEEE arithmetic, "
                              "and x plus a times y less x gives the same one. Vulkan lets a "
                              "machine assume no infinity or NaN arrives, so a failure is a "
                              "difference from the console rather than from the table" };
    case FMATH_DENORMAL:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: every instruction in the console's "
                              "binary flushes denormals, operands and results, so each answer is "
                              "exact and one. The table allows keeping them; a machine that "
                              "does differs from the console, and the case says so" };
    case FMATH_EXP2_EDGES:
        return (FMathBudget){ false, 3.5f, 2.0f, 0.0f,
                              "3.5 plus 2 per unit of the argument: the power of two's own "
                              "figure, over -128 to nearly 128, which reaches 259.5 at the top. "
                              "Below two to the -126 the console's MUFU flushes the answer, so "
                              "nought is the answer there" };
    case FMATH_LOG2_EDGES:
        return (FMathBudget){ false, 3.5f, 0.0f, 0.0f,
                              "3.5 units in the last place: the logarithm's 3 and the half the "
                              "reference carries, on operands from the smallest normal to "
                              "nearly the largest float with a full mantissa, none inside a half "
                              "to two where the table switches to an absolute figure" };
    case FMATH_SQRT_TINY:
        return (FMathBudget){ false, 5.0f, 0.0f, 0.0f,
                              "5 units in the last place: the square root's figure, on operands "
                              "from the denormals to two to the -96. The console's MUFU flushes "
                              "a denormal operand, so nought is the answer for those" };
    case FMATH_INVERSESQRT_TINY:
        return (FMathBudget){ false, 2.5f, 0.0f, 0.0f,
                              "2.5 units in the last place: 2 from the table and the half the "
                              "reference carries. The console's MUFU flushes a denormal operand, "
                              "so infinity is the answer for those" };
    case FMATH_FLOOR:
    case FMATH_FRACT:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: the answer is a whole number or the "
                              "exact difference between the operand and one, so there is one "
                              "right answer and the table's are exact" };
    case FMATH_ROUND_EVEN:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place: the answer is a whole number, so the "
                              "table's is exact. The sign of a nought is the console's rule: "
                              "its F2F rounds -0.25 and -0.5 to minus nought, and a machine "
                              "that gives plus nought differs from the console" };
    case FMATH_ROUND:
        return (FMathBudget){ false, 0.0f, 0.0f, 0.0f,
                              "0 units in the last place, and a half to even: the language lets "
                              "round go either way there, and the console's binary compiles it "
                              "to the same rounding as roundEven, so a machine that rounds a "
                              "half away from nought differs from the console. So does one that "
                              "rounds -0.25 to plus nought, where the console's F2F keeps the "
                              "sign" };
    case FMATH_MODE_COUNT:
        break;
    }
    return (FMathBudget){ false, 0.0f, 0.0f, 0.0f, "0 units in the last place: unknown" };
}

float FMathAllowedUlps(FMathMode mode, uint32_t x, uint32_t y) {
    const FMathBudget budget = FMathBudgetFor(mode);
    if (budget.absolute) {
        return 0.0f;
    }
    if (budget.ulps_per_argument == 0.0f) {
        return budget.ulps;
    }
    const float argument = FMathArgument(mode, x, y);
    const float magnitude = argument < 0.0f ? -argument : argument;
    return budget.ulps + budget.ulps_per_argument * magnitude;
}

const char *FMathName(FMathMode mode) {
    switch (mode) {
    case FMATH_UNIFORM:
        return "a float travels through the uniform buffer unaltered";
    case FMATH_ADD:
        return "addition is correctly rounded";
    case FMATH_MULTIPLY:
        return "multiplication is correctly rounded";
    case FMATH_MULTIPLY_ADD:
        return "a multiply and an add in one call";
    case FMATH_DIVIDE:
        return "division within two and a half units in the last place";
    case FMATH_SQUARE_ROOT:
        return "the square root";
    case FMATH_EXP2:
        return "two to the power of";
    case FMATH_LOG2:
        return "the logarithm to base two";
    case FMATH_SINE:
        return "the sine, within two to the minus eleven";
    case FMATH_COSINE:
        return "the cosine, within two to the minus eleven";
    case FMATH_FMA_CANCELLATION:
        return "fma keeps the product's rounding error, as the console's FFMA does";
    case FMATH_MIN_MAX_INFINITY:
        return "min and max against infinities";
    case FMATH_MIN_MAX_NAN:
        return "min and max hand back the operand that is not a NaN";
    case FMATH_MIX_INFINITY_NAN:
        return "mix carries infinities and NaNs through";
    case FMATH_DENORMAL:
        return "denormal products, sums and operands flush, as the console's do";
    case FMATH_EXP2_EDGES:
        return "two to the power of -128 to nearly 128";
    case FMATH_LOG2_EDGES:
        return "the logarithm from the smallest normal to nearly the largest float";
    case FMATH_SQRT_TINY:
        return "the square root of denormals and tiny normals";
    case FMATH_INVERSESQRT_TINY:
        return "the inverse square root of denormals and tiny normals";
    case FMATH_FLOOR:
        return "floor of negatives and fractions";
    case FMATH_FRACT:
        return "fract of negatives";
    case FMATH_ROUND:
        return "round of negatives and halves, halves to even as the console's";
    case FMATH_ROUND_EVEN:
        return "roundEven of halves goes to the even neighbour";
    case FMATH_MODE_COUNT:
        break;
    }
    return "unknown";
}
