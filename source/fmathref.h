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
/// What the tables say, for the ten operations the first modes cover; the edge modes after them
/// reuse these figures or are exact, and say which in their own budgets:
///
///   addition, multiplication      correctly rounded
///   a multiply and an add         inherited from a multiply and an add, which allows the unfused
///                                 pair; the console's binary fuses it, so the case holds a
///                                 machine to the fused answer
///   division                      2.5 units in the last place
///   the square root               inherited from one over the inverse square root
///   the inverse square root       2 units in the last place
///   two to the power of x         3 plus twice the magnitude of x, in units in the last place
///   the logarithm to base two     3 units outside the range from a half to two, and an
///                                 absolute 2^-21 inside it
///   the sine and the cosine       an absolute 2^-11 inside minus pi to pi, undefined outside
///
/// Where the table allows more than one answer and the console's compiled shader gives exactly
/// one (it flushes denormals in every float instruction, fuses fma into FFMA, and rounds round's
/// halves to even), the case holds a machine to the console's, as the integer suite does for its
/// shifts and its division.
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
    /// A fused multiply and add whose addend nearly cancels the product: the answer keeps the
    /// product's rounding error, which only a fused machine, as the console's is, has.
    FMATH_FMA_CANCELLATION = 10,
    /// The rows of this and the next three modes each ask a different question, one row in four.
    /// min(a, +inf), max(a, -inf), min(a, -inf), max(-a, +inf).
    FMATH_MIN_MAX_INFINITY = 11,
    /// min(a, NaN), min(NaN, a), max(a, NaN), max(NaN, -a): the operand that is not a NaN.
    FMATH_MIN_MAX_NAN = 12,
    /// mix(a, +inf, 0), mix(a, +inf, 0.25), mix(a, -inf, 1), mix(a, b, NaN).
    FMATH_MIX_INFINITY_NAN = 13,
    /// Products that land on denormals, a denormal carried into a normal product, a denormal
    /// added to the smallest normal, and a denormal operand scaled up, all flushed as the
    /// console's binary flushes them.
    FMATH_DENORMAL = 14,
    /// Two to the power of -128 to nearly 128, from denormal answers to nearly the largest float.
    FMATH_EXP2_EDGES = 15,
    /// The logarithm of the smallest normal up to nearly the largest float, none within a half
    /// to two.
    FMATH_LOG2_EDGES = 16,
    /// Square roots and inverse square roots of denormals and of normals up to two to the -96.
    FMATH_SQRT_TINY = 17,
    FMATH_INVERSESQRT_TINY = 18,
    /// floor, fract, round and roundEven on operands from -32 to 32, negatives, fractions and
    /// halves among them.
    FMATH_FLOOR = 19,
    FMATH_FRACT = 20,
    FMATH_ROUND = 21,
    FMATH_ROUND_EVEN = 22,
    FMATH_MODE_COUNT = 23,
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

/// The product the cancellation mode subtracts: a third of the operand, rounded to single
/// precision, with its low twelve mantissa bits cleared.
float FMathCancelledProduct(uint32_t x);

/// The answer the tables would allow and the console's binary never gives, where there is one:
/// the multiply and add done apart, denormals kept, a half rounded away from nought. Never
/// accepted: it only names what a wrong pixel holds. False where there is none.
bool FMathOther(FMathMode mode, uint32_t x, uint32_t y, float *out);
const char *FMathOtherName(FMathMode mode);

/// What the judge decided about one pixel, and the numbers the report quotes.
typedef struct {
    bool within;
    /// True when a wrong pixel holds exactly FMathOther's answer.
    bool other;
    /// How far off it was, in units in the last place, or as a plain difference for the modes
    /// whose budget is one. Nought for a NaN that was expected and got.
    float error;
    /// The allowance at this pixel in units in the last place, nought for an absolute budget.
    float allowed;
} FMathVerdict;

/// The whole rule for one pixel: a NaN expected is met by any NaN, a nought by the same nought,
/// sign and all, and anything else by the budget.
FMathVerdict FMathJudge(FMathMode mode, uint32_t x, uint32_t y, float probe, float got);

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
