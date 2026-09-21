// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "report.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The tolerance a comparison is allowed.
///
/// Three tiers, because one number cannot serve all three and pretending it can is how a suite
/// becomes noise. Which tier a test uses is a claim about the hardware, and belongs in the test.
typedef enum {
    /// Bit for bit. Legal only where the result is defined exactly: integer render targets,
    /// integer arithmetic, bit patterns copied around. A float shader never qualifies, whatever
    /// the numbers look like on one machine.
    CHECK_EXACT,
    /// Within a stated number of units in the last place. For float work, where two correct
    /// implementations legitimately differ: the graphics API only requires division within
    /// 2.5 ULP, and the transcendentals are looser still.
    CHECK_ULP,
    /// The right answer, possibly a pixel or two away. For rasterisation coverage, where sub
    /// pixel precision is an implementation-reported limit rather than a constant.
    CHECK_POSITION,
} CheckTier;

/// Records a case that passes when @p condition holds. The detail is only built on failure, so
/// a passing run pays nothing for the formatting.
#define CHECK_TRUE(suite, name, condition, fmt, ...)                                             \
    do {                                                                                         \
        if (condition) {                                                                         \
            ReportCase((suite), (name), true, NULL);                                             \
        } else {                                                                                 \
            char detail_[512];                                                                    \
            snprintf(detail_, sizeof(detail_), (fmt), ##__VA_ARGS__);                            \
            ReportCase((suite), (name), false, detail_);                                          \
        }                                                                                        \
    } while (0)

/// Whether two 32-bit floats agree to within @p ulps units in the last place.
///
/// Compared as sign-magnitude integers, which is what makes "how many representable values
/// apart" a single subtraction. Zeroes of opposite sign count as equal, and anything involving
/// a NaN is never equal.
bool CheckFloatUlp(float produced, float expected, uint32_t ulps);

/// Whether two 8-bit channel values agree to within @p threshold.
bool CheckChannel(uint8_t produced, uint8_t expected, uint8_t threshold);

#ifdef __cplusplus
}
#endif
