// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "../check.h"
#include "../report.h"
#include "tests.h"

// The suite checking itself. These touch no hardware and must pass identically on the console
// and on any emulator: if one of them fails, the machinery that judges every other test is
// broken, and nothing below it can be believed.

static const char *const SUITE = "selfcheck";

void RunSelfcheck(void) {
    // Sum of 1..100, which nobody needs a graphics processor to agree on. If this fails, the
    // program is not running the code it was built from.
    int sum = 0;
    for (int i = 1; i <= 100; ++i) {
        sum += i;
    }
    CHECK_TRUE(SUITE, "integer arithmetic", sum == 5050, "sum(1..100)=%d expected 5050", sum);

    // The ULP comparison is what every float test leans on, so it is pinned here rather than
    // trusted. 1.0f and the next value above it are one apart by construction.
    const float one = 1.0f;
    const float next_above = 1.0000001192092896f;
    CHECK_TRUE(SUITE, "ulp neighbours are one apart", CheckFloatUlp(one, next_above, 1),
               "1.0 and its successor did not compare within 1 ulp");
    CHECK_TRUE(SUITE, "ulp zero tolerance rejects a neighbour",
               !CheckFloatUlp(one, next_above, 0),
               "a zero ulp budget accepted two different values");
    CHECK_TRUE(SUITE, "signed zeroes compare equal", CheckFloatUlp(0.0f, -0.0f, 0),
               "positive and negative zero did not compare equal");

    // A NaN is never equal to anything, including itself. A comparison that says otherwise
    // would let a shader that produced garbage pass.
    const float nan_value = 0.0f / 0.0f;
    CHECK_TRUE(SUITE, "nan never compares equal", !CheckFloatUlp(nan_value, nan_value, 1000000),
               "a NaN compared equal to itself");

    CHECK_TRUE(SUITE, "channel threshold accepts within", CheckChannel(128, 130, 2),
               "128 and 130 were not within a threshold of 2");
    CHECK_TRUE(SUITE, "channel threshold rejects beyond", !CheckChannel(128, 131, 2),
               "128 and 131 passed a threshold of 2");
}
