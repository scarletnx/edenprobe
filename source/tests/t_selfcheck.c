// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "../check.h"
#include "../report.h"
#include "tests.h"

// The suite checking itself. These touch no hardware and must pass identically on the console
// and on any emulator: if one of them fails, the machinery that judges every other test is
// broken.
//
// Every case reports CHECK_NONE, because a tier is a claim about what the hardware is allowed to
// get wrong and nothing here asked the hardware anything.

static const char *const SUITE = "selfcheck";

void RunSelfcheck(void) {
    // Sum of 1..100, which nobody needs a graphics processor to agree on. If this fails, the
    // program is not running the code it was built from.
    int sum = 0;
    for (int i = 1; i <= 100; ++i) {
        sum += i;
    }
    CHECK_TRUE(SUITE, "integer arithmetic", CHECK_NONE, sum == 5050,
               "sum(1..100)=%d expected 5050", sum);

    // The ULP comparison is what every float test leans on, so it is pinned here rather than
    // trusted. 1.0f and the next value above it are one apart by construction.
    const float one = 1.0f;
    const float next_above = 1.0000001192092896f;
    CHECK_TRUE(SUITE, "ulp neighbours are one apart", CHECK_NONE, CheckFloatUlp(one, next_above, 1),
               "1.0 and its successor did not compare within 1 ulp");
    CHECK_TRUE(SUITE, "ulp zero tolerance rejects a neighbour", CHECK_NONE,
               !CheckFloatUlp(one, next_above, 0),
               "a zero ulp budget accepted two different values");
    // The count of representable values, which the float suite reports next to every result. It
    // is a different measure from the budget, and the two cases after this one are what make
    // that difference visible rather than a sentence in a header.
    CHECK_TRUE(SUITE, "the ulp measure counts one between neighbours", CHECK_NONE,
               CheckFloatUlpDistance(one, next_above) == 1u,
               "1.0 and its successor measured %u apart",
               (unsigned)CheckFloatUlpDistance(one, next_above));

    // Below a power of two the representable values are half as far apart as above it, so three
    // units in the last place below 1.0 is six of them and a budget of three has to accept that.
    // This suite's operands are whole numbers over 64, so its answers land on powers of two
    // often.
    float below = 1.0f;
    for (int step = 0; step < 6; ++step) {
        below = nextafterf(below, 0.0f);
    }
    CHECK_TRUE(SUITE, "three units below a power of two is six representable values", CHECK_NONE,
               CheckFloatUlp(below, 1.0f, 3) && CheckFloatUlpDistance(below, 1.0f) == 6u,
               "six values below 1.0 measured %u apart and %s the budget of three",
               (unsigned)CheckFloatUlpDistance(below, 1.0f),
               CheckFloatUlp(below, 1.0f, 3) ? "passed" : "failed");
    CHECK_TRUE(SUITE, "and the seventh is outside it", CHECK_NONE,
               !CheckFloatUlp(nextafterf(below, 0.0f), 1.0f, 3),
               "seven values below 1.0 passed a budget of three units");

    // Above the same power of two, three units is three values, and the fourth is outside. The
    // budget is not simply doubled: it is a distance, and the distance is what differs.
    float above = 1.0f;
    for (int step = 0; step < 3; ++step) {
        above = nextafterf(above, 2.0f);
    }
    CHECK_TRUE(SUITE, "three units above a power of two is three representable values",
               CHECK_NONE,
               CheckFloatUlp(above, 1.0f, 3) && !CheckFloatUlp(nextafterf(above, 2.0f), 1.0f, 3),
               "the budget did not fall between the third and the fourth value above 1.0");
    CHECK_TRUE(SUITE, "signed zeroes compare equal", CHECK_NONE, CheckFloatUlp(0.0f, -0.0f, 0),
               "positive and negative zero did not compare equal");

    // A NaN is never equal to anything, including itself. A comparison that says otherwise
    // would let a shader that produced garbage pass.
    const float nan_value = 0.0f / 0.0f;
    CHECK_TRUE(SUITE, "nan never compares equal", CHECK_NONE,
               !CheckFloatUlp(nan_value, nan_value, 1000000),
               "a NaN compared equal to itself");
    // Including against a budget of every value there is, which is the one way a comparison
    // built on a distance and a limit could have let one through.
    CHECK_TRUE(SUITE, "no budget is large enough to accept a nan", CHECK_NONE,
               !CheckFloatUlp(nan_value, 1.0f, UINT32_MAX),
               "a NaN passed a budget of every representable value");

    // The absolute comparison, which is what the sine and the cosine are judged by. Two to the
    // minus eleven is the budget those cases use, so the two values below straddle it.
    const float budget = 1.0f / 2048.0f;
    CHECK_TRUE(SUITE, "absolute tolerance accepts within", CHECK_NONE,
               CheckFloatAbs(0.5f, 0.5f + budget * 0.5f, budget),
               "half the budget was rejected by it");
    CHECK_TRUE(SUITE, "absolute tolerance rejects beyond", CHECK_NONE,
               !CheckFloatAbs(0.5f, 0.5f + budget * 2.0f, budget),
               "twice the budget was accepted by it");
    // The same refusal the other comparison makes, restated here because a shader that produced
    // nothing but NaN would otherwise pass every case judged this way.
    CHECK_TRUE(SUITE, "absolute tolerance never accepts a nan", CHECK_NONE,
               !CheckFloatAbs(nan_value, nan_value, 1.0f), "a NaN compared equal to itself");
    // And the one thing an absolute budget has to get right that a relative one does not: a
    // difference either side of the expected value is the same distance.
    CHECK_TRUE(SUITE, "absolute tolerance is symmetric", CHECK_NONE,
               CheckFloatAbs(0.5f - budget, 0.5f, budget) &&
                   CheckFloatAbs(0.5f + budget, 0.5f, budget),
               "the budget was not the same on both sides of the expected value");

    CHECK_TRUE(SUITE, "channel threshold accepts within", CHECK_NONE, CheckChannel(128, 130, 2),
               "128 and 130 were not within a threshold of 2");
    CHECK_TRUE(SUITE, "channel threshold rejects beyond", CHECK_NONE, !CheckChannel(128, 131, 2),
               "128 and 131 passed a threshold of 2");
}
