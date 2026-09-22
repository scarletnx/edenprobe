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

bool CheckFloatUlp(float produced, float expected, uint32_t ulps) {
    if (isnan(produced) || isnan(expected)) {
        return false;
    }
    if (produced == expected) {
        return true;
    }
    // An infinity is only equal to the same infinity, which the line above already settled.
    if (isinf(produced) || isinf(expected)) {
        return false;
    }

    const uint32_t a = OrderedBits(produced);
    const uint32_t b = OrderedBits(expected);
    const uint32_t distance = a > b ? a - b : b - a;
    return distance <= ulps;
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
