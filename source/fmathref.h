// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the float fragment shader is supposed to produce, and how far off it is allowed to be.
///
/// This is the only suite where two correct machines may legitimately disagree, so the budget is
/// as much a part of the reference as the arithmetic is. Each one below is the graphics
/// specification's own number for that operation, plus the half a unit the reference itself may
/// carry because it is a double precision answer rounded to single precision. A budget that is a
/// little loose costs a little sensitivity; a budget that is too tight turns a correct machine
/// into a failure, which is worse.
typedef enum {
    /// The value out of the uniform buffer, unaltered. The guard case: everything below reads
    /// its operation number out of that same buffer.
    FMATH_UNIFORM = 0,
    FMATH_ADD = 1,
    FMATH_MULTIPLY = 2,
    /// A multiply and an add in one call, with a product that does not fit exactly, so a fused
    /// implementation and an unfused one can differ.
    FMATH_MULTIPLY_ADD = 3,
    FMATH_DIVIDE = 4,
    FMATH_SQUARE_ROOT = 5,
    FMATH_EXP2 = 6,
    FMATH_LOG2 = 7,
    FMATH_SINE = 8,
    FMATH_COSINE = 9,
    FMATH_MODE_COUNT = 10,
} FMathMode;

/// The tolerance a case allows, in whichever of the two shapes the specification states it.
typedef struct {
    /// True when the limit below is an absolute difference rather than a count of representable
    /// values.
    bool absolute;
    uint32_t ulps;
    float limit;
    /// Why the number is what it is, for the report.
    const char *reason;
} FMathBudget;

/// The two operands, built from the fragment's coordinate. Both are a whole number over 64, so
/// both are exact and the processor can reproduce the shader's inputs rather than approximate
/// them.
float FMathInputA(uint32_t x);
float FMathInputB(uint32_t y);

/// The answer for the pixel at @p x, @p y, with @p probe as whatever the uniform buffer carries.
float FMathReference(FMathMode mode, uint32_t x, uint32_t y, float probe);

FMathBudget FMathBudgetFor(FMathMode mode);
const char *FMathName(FMathMode mode);

#ifdef __cplusplus
}
#endif
