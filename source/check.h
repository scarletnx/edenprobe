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
/// Compared as sign-magnitude integers, which is what makes "how many representable values
/// apart" a single subtraction. Zeroes of opposite sign count as equal, and anything involving
/// a NaN is never equal.
bool CheckFloatUlp(float produced, float expected, uint32_t ulps);

/// How many representable values apart two floats are.
///
/// The measure the comparison above answers yes or no about, exposed because a case that passes
/// should still be able to say how much room it had, and there should be one implementation of
/// the measure rather than one per reader. Hands back the largest possible count for a NaN on
/// either side, and for one infinity against anything that is not the same infinity, neither of
/// which any budget accepts. No two finite floats are ever that far apart, so the count cannot
/// be mistaken for a real one.
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
