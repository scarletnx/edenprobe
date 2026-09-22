// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "check.h"

/// Maps a float onto the integer whose ordering matches the float's, so that subtracting two of
/// them counts the representable values between the originals. Negatives are mirrored, which is
/// what makes -0.0 and +0.0 land one apart rather than a whole sign bit apart.
static uint32_t OrderedBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x80000000u) != 0u) {
        return 0x80000000u - (bits & 0x7FFFFFFFu);
    }
    return bits + 0x80000000u;
}

uint32_t CheckFloatUlpDistance(float produced, float expected) {
    if (isnan(produced) || isnan(expected)) {
        return UINT32_MAX;
    }
    if (produced == expected) {
        return 0u;
    }
    // An infinity is only equal to the same infinity, which the line above already settled.
    // Without this, an infinity would measure one step from the largest finite float, because
    // that is where it sits in the ordering below.
    if (isinf(produced) || isinf(expected)) {
        return UINT32_MAX;
    }

    const uint32_t a = OrderedBits(produced);
    const uint32_t b = OrderedBits(expected);
    return a > b ? a - b : b - a;
}

bool CheckFloatUlp(float produced, float expected, uint32_t ulps) {
    const uint32_t distance = CheckFloatUlpDistance(produced, expected);
    // The sentinel is refused whatever the budget, which matters because a budget of every
    // possible value would otherwise let a NaN through.
    return distance != UINT32_MAX && distance <= ulps;
}

bool CheckFloatAbs(float produced, float expected, float limit) {
    if (isnan(produced) || isnan(expected)) {
        return false;
    }
    if (produced == expected) {
        return true;
    }
    if (isinf(produced) || isinf(expected)) {
        return false;
    }
    const float difference = produced - expected;
    return (difference < 0.0f ? -difference : difference) <= limit;
}

bool CheckChannel(uint8_t produced, uint8_t expected, uint8_t threshold) {
    const int difference = (int)produced - (int)expected;
    const int magnitude = difference < 0 ? -difference : difference;
    return magnitude <= (int)threshold;
}
