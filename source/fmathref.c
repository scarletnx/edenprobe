// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>

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

float FMathArgument(FMathMode mode, uint32_t x, uint32_t y) {
    const float a = FMathInputA(x);
    switch (mode) {
    case FMATH_EXP2:
        // Eight times a whole number over 64, less four. Exact in single precision, and the
        // magnitude of it is what the power of two's budget grows with.
        return a * 8.0f - 4.0f;
    case FMATH_LOG2:
        // A quarter of the operand, and the quarter is not decoration. The table bounds the
        // logarithm by units in the last place only outside the range from a half to two, and by
        // an absolute error inside it; a quarter of a number that is at most one is at most a
        // quarter, so every input here is outside that range and one budget covers them all.
        return a * 0.25f;
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
        // The processor's own fused multiply and add, which the language requires to round once.
        // That makes the reference the fused answer; an implementation that multiplies and then
        // adds is still within the budget below, which is why the budget is one and not nought.
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
    case FMATH_MODE_COUNT:
        break;
    }
    return 0.0f;
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
        return (FMathBudget){ false, 1.5f, 0.0f, 0.0f,
                              "1.5 units in the last place: the table inherits this one from a "
                              "multiply and an add, so an implementation that does not fuse them "
                              "rounds twice where the fused reference rounds once. Each of the "
                              "three roundings is at most half a unit of its own result, and "
                              "both operands here are positive so the sum is the largest of the "
                              "three and its unit is the largest, which bounds the lot at one "
                              "and a half of them. The reference itself adds nothing: the "
                              "processor's fused multiply and add is the exact answer" };
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
    case FMATH_MODE_COUNT:
        break;
    }
    return "unknown";
}
