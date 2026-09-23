// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
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

/// The distance between neighbouring representable values at @p value, which is what a unit in
/// the last place means.
///
/// Measured on the side away from zero. At a power of two the two sides differ by a factor of
/// two, and taking the larger is the conservative choice: a budget of three units then allows
/// three of the wider steps in either direction, which is no tighter than any reading of the
/// specification, and so cannot turn a machine that is inside its budget into a failure.
static float UnitInTheLastPlace(float value) {
    const float magnitude = value < 0.0f ? -value : value;
    const float above = nextafterf(magnitude, INFINITY);
    if (isinf(above)) {
        // Only at the very top of the range, where there is no step above to measure.
        return magnitude - nextafterf(magnitude, 0.0f);
    }
    return above - magnitude;
}

float CheckFloatUlpError(float produced, float expected) {
    if (isnan(produced) || isnan(expected)) {
        return INFINITY;
    }
    if (produced == expected) {
        return 0.0f;
    }
    if (isinf(produced) || isinf(expected)) {
        return INFINITY;
    }
    const float difference = produced - expected;
    const float magnitude = difference < 0.0f ? -difference : difference;
    return magnitude / UnitInTheLastPlace(expected);
}

bool CheckFloatUlp(float produced, float expected, float ulps) {
    return CheckFloatUlpError(produced, expected) <= ulps;
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
