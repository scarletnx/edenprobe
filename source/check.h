// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "report.h"

#ifdef __cplusplus
extern "C" {
#endif

// CheckTier, the tolerance a comparison is allowed, is declared in report.h rather than here,
// because every case reports the tier it claimed next to its result. Which one a case uses is a
// claim about the hardware and belongs in the case.

/// Records a case that passes when @p condition holds, reporting @p tier as the tolerance this
/// comparison was held to. The detail is only built on failure, so a passing run pays nothing
/// for the formatting.
#define CHECK_TRUE(suite, name, tier, condition, fmt, ...)                                       \
    do {                                                                                         \
        if (condition) {                                                                         \
            ReportCase((suite), (name), (tier), true, NULL);                                     \
        } else {                                                                                 \
            char detail_[512];                                                                   \
            snprintf(detail_, sizeof(detail_), (fmt), ##__VA_ARGS__);                            \
            ReportCase((suite), (name), (tier), false, detail_);                                 \
        }                                                                                        \
    } while (0)

/// Whether two 32-bit floats agree to within @p ulps units in the last place.
///
/// The budget is a distance, the step between neighbouring representable values at the expected
/// answer multiplied out, and deliberately not a count of those values: below a power of two
/// they are half as far apart as above it, and a comparator that counted would fail a machine
/// that was inside its budget. A float and not a whole number, because the specification's
/// figures are not whole numbers either.
///
/// Zeroes of opposite sign count as equal, anything involving a NaN is never equal, and an
/// infinity is only equal to the same infinity.
bool CheckFloatUlp(float produced, float expected, float ulps);

/// How far apart two floats are, measured in units in the last place at @p expected.
///
/// The same measure the budget above uses, as a number rather than a verdict, so a case can say
/// how much room it had in the units its budget is written in. Hands back infinity for a NaN on
/// either side and for one infinity against anything that is not the same infinity.
float CheckFloatUlpError(float produced, float expected);

/// How many representable values apart two floats are.
///
/// A different measure from the budget above, which is a distance: the two agree away from a
/// power of two and differ by a factor of two at one. For a reader's intuition and for the cases
/// that pin that difference; a budget is never written in it. Hands back the largest possible
/// count for a NaN on either side and for mismatched infinities, which no two finite floats can
/// reach.
uint32_t CheckFloatUlpDistance(float produced, float expected);

/// Whether two 32-bit floats agree to within @p limit measured as a plain difference.
///
/// The shape the specification uses for the sine and the cosine, and it has to: near a zero
/// crossing the answer's own magnitude goes to nothing, so any relative budget there is a
/// tighter claim than the specification makes. Same two refusals as the comparison above.
bool CheckFloatAbs(float produced, float expected, float limit);

/// Whether two 8-bit channel values agree to within @p threshold.
bool CheckChannel(uint8_t produced, uint8_t expected, uint8_t threshold);

#ifdef __cplusplus
}
#endif
