// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>

#include "fmathref.h"

// The multiplier the multiply and add case uses. Written the same way in both files, and it is
// the nearest float to a third either way: the shading language divides two float literals and
// the processor rounds a double quotient, and both land on the same 24 bits.
#define ONE_THIRD ((float)(1.0 / 3.0))

float FMathInputA(uint32_t x) {
    return ((float)x + 1.0f) / 64.0f;
}

float FMathInputB(uint32_t y) {
    return ((float)y + 1.0f) / 64.0f;
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
        // The processor's own fused multiply and add, which the language requires to round once.
        // That makes the reference the fused answer; an implementation that multiplies and then
        // adds is still within the budget below, which is why the budget is one and not nought.
        return fmaf(a, ONE_THIRD, b);
    case FMATH_DIVIDE:
        return (float)((double)a / (double)b);
    case FMATH_SQUARE_ROOT:
        return (float)sqrt((double)a);
    case FMATH_EXP2:
        // The argument is built the way the shader builds it, in single precision, where it is
        // exact: eight times a whole number over 64, less four.
        return (float)exp2((double)(a * 8.0f - 4.0f));
    case FMATH_LOG2:
        // A quarter of the operand, and the quarter is not decoration. The specification bounds
        // the logarithm by units in the last place only outside the range from a half to two,
        // and by an absolute error inside it; a quarter of a number that is at most one is at
        // most a quarter, so every input here is outside that range and one budget covers them
        // all.
        return (float)log2((double)(a * 0.25f));
    case FMATH_SINE:
        return (float)sin((double)(a * 6.0f - 3.0f));
    case FMATH_COSINE:
        return (float)cos((double)(a * 6.0f - 3.0f));
    case FMATH_MODE_COUNT:
        break;
    }
    return 0.0f;
}

FMathBudget FMathBudgetFor(FMathMode mode) {
    switch (mode) {
    case FMATH_UNIFORM:
        return (FMathBudget){ false, 0u, 0.0f, "a value copied, not computed" };
    case FMATH_ADD:
    case FMATH_MULTIPLY:
        return (FMathBudget){ false, 0u, 0.0f,
                              "the specification requires addition and multiplication to be "
                              "correctly rounded, and the reference is exact" };
    case FMATH_MULTIPLY_ADD:
        return (FMathBudget){ false, 1u, 0.0f,
                              "one, not nought, because fusing the multiply into the add is only "
                              "guaranteed for a result the shader marks precise, and an unfused "
                              "implementation is a unit away from the fused reference" };
    case FMATH_DIVIDE:
        return (FMathBudget){ false, 3u, 0.0f,
                              "2.5 from the specification plus the half the reference may carry "
                              "for being a double precision answer rounded to single" };
    case FMATH_SQUARE_ROOT:
        return (FMathBudget){ false, 3u, 0.0f,
                              "the specification derives the square root's bound from the "
                              "inverse square root's; three is the transcendental budget and "
                              "conservative for this one" };
    case FMATH_EXP2:
    case FMATH_LOG2:
        return (FMathBudget){ false, 3u, 0.0f,
                              "3 from the specification plus the half the reference may carry" };
    case FMATH_SINE:
    case FMATH_COSINE:
        // Two to the minus eleven, which is what the specification asks for over the range where
        // it asks for anything at all. The reference's own error is under a ten millionth, more
        // than a thousand times smaller, so it does not need adding on.
        return (FMathBudget){ true, 0u, 1.0f / 2048.0f,
                              "the specification bounds the sine and the cosine by an absolute "
                              "error of two to the minus eleven, not by units in the last "
                              "place" };
    case FMATH_MODE_COUNT:
        break;
    }
    return (FMathBudget){ false, 0u, 0.0f, "unknown" };
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
    case FMATH_MODE_COUNT:
        break;
    }
    return "unknown";
}
