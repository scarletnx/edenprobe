// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the float fragment shader is supposed to produce, and how far off it is allowed to be.
///
/// The only suite where two correct machines may legitimately disagree, so the budget is as much
/// a part of the reference as the arithmetic is, and one too tight fails a correct machine. Every
/// budget below comes from one place, the Vulkan precision tables, core for the operations that
/// are core instructions and GLSL.std.450 for the rest: it is the only single source that covers
/// all ten, since the shading language's own leaves the trigonometric functions undefined.
///
/// What the tables say, for the ten operations here:
///
///   addition, multiplication      correctly rounded
///   a multiply and an add         inherited from a multiply and an add, which this suite reads
///                                 as the unfused pair, since a table that meant the fused
///                                 instruction would have no inheriting to do
///   division                      2.5 units in the last place
///   the square root               inherited from one over the inverse square root
///   the inverse square root       2 units in the last place
///   two to the power of x         3 plus twice the magnitude of x, in units in the last place
///   the logarithm to base two     3 units outside the range from a half to two, and an
///                                 absolute 2^-21 inside it
///   the sine and the cosine       an absolute 2^-11 inside minus pi to pi, undefined outside
///
/// On top of the table's number, each budget carries half a unit for the reference itself, which
/// is a double precision answer rounded to single. The three that carry nothing say why in their
/// own reason.
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

/// The tolerance a case allows, in whichever of the three shapes the tables state it.
typedef struct {
    /// True when the limit below is an absolute difference rather than a number of units in the
    /// last place.
    bool absolute;
    /// The part of the budget that does not depend on the operand, in units in the last place.
    /// Not a whole number, because the tables' figures are not whole numbers either.
    float ulps;
    /// Added per unit of the magnitude of the argument. Only the power of two needs it, and a
    /// budget that could not say it would fail a correct machine at the ends of the range.
    float ulps_per_argument;
    /// The absolute budget, when `absolute`.
    float limit;
    /// Why the number is what it is, for the report. It has to begin with the number itself,
    /// spelled the way the reference suite spells it, so that changing one without the other
    /// fails a case rather than going unnoticed.
    const char *reason;
} FMathBudget;

/// The two operands, built from the fragment's coordinate. Both are a whole number over 64, so
/// both are exact and the processor can reproduce the shader's inputs rather than approximate
/// them.
float FMathInputA(uint32_t x);
float FMathInputB(uint32_t y);

/// The answer for the pixel at @p x, @p y, with @p probe as whatever the uniform buffer carries.
float FMathReference(FMathMode mode, uint32_t x, uint32_t y, float probe);

/// The value the operation is applied to, for the modes that transform their operand first.
///
/// Written once and read both by the reference and by the budget, so the two cannot end up
/// disagreeing about what the argument was. The operand itself for the modes that use it
/// unaltered.
float FMathArgument(FMathMode mode, uint32_t x, uint32_t y);

/// The shape of the line the float suite writes for each budget.
///
/// Here rather than in the suite, so the reference suite can measure the very string that gets
/// emitted against what a note carries whole.
#define FMATH_BUDGET_NOTE_FORMAT "fmath budget for \"%s\": %s"

/// The whole budget for one pixel, in units in the last place.
///
/// The constant part plus whatever the argument adds. Zero, and meaningless, for the two modes
/// whose budget is an absolute difference instead.
float FMathAllowedUlps(FMathMode mode, uint32_t x, uint32_t y);

FMathBudget FMathBudgetFor(FMathMode mode);
const char *FMathName(FMathMode mode);

#ifdef __cplusplus
}
#endif
