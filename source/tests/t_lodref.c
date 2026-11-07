// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "../check.h"
#include "../lodref.h"
#include "../report.h"
#include "tests.h"

// The lod suite's expected answers: the gradients it hands the sampler give whole lambdas, its
// coordinate stays clear of every texel boundary on every level, an offset counted in a level's
// own texels lands somewhere else than one counted in level nought's, and its one blended value
// is a float exactly.

static const char *const SUITE = "reference";

#define COORD (13.0 / 32.0)

void RunReferenceLod(void) {
    uint32_t wrong = 0;
    for (uint32_t k = 0; k < 4u; ++k) {
        const double step = (double)(1u << k);
        const double along_x = LodGradientLambda(step / LOD_WIDTH, 0, 0, 0, LOD_WIDTH, LOD_HEIGHT);
        const double along_y = LodGradientLambda(0, step / LOD_HEIGHT, 0, 0, LOD_WIDTH, LOD_HEIGHT);
        const double second = LodGradientLambda(0, 0, step / LOD_WIDTH, 0, LOD_WIDTH, LOD_HEIGHT);
        // A gradient along y scaled by the width, which is one level up.
        const double misread = LodGradientLambda(0, step / LOD_HEIGHT, 0, 0, LOD_WIDTH, LOD_WIDTH);
        wrong += along_x != (double)k || along_y != (double)k || second != (double)k ||
                 misread != (double)(k + 1u) ||
                 LodNearestLevel(along_x, LOD_LEVELS) != k;
    }
    CHECK_TRUE(SUITE, "gradients of one, two, four and eight texels give lambdas 0 to 3 exactly",
               CHECK_NONE, wrong == 0u, "%u of four levels wrong", (unsigned)wrong);

    const double larger =
        LodGradientLambda(1.0 / LOD_WIDTH, 0, 0, 4.0 / LOD_HEIGHT, LOD_WIDTH, LOD_HEIGHT);
    CHECK_TRUE(SUITE, "one texel along x and four along y give lambda 2", CHECK_NONE,
               larger == 2.0, "got %.17g", larger);

    const uint32_t half = LodNearestLevel(0.5, LOD_LEVELS);
    const uint32_t past = LodNearestLevel(9.0, LOD_LEVELS);
    CHECK_TRUE(SUITE, "nearest mip filtering keeps level 0 up to lambda one half and stops at the "
                      "last level",
               CHECK_NONE, half == 0u && past == LOD_LEVELS - 1u, "got %u and %u",
               (unsigned)half, (unsigned)past);

    const double margin_u = LodCoordMargin(COORD, LOD_WIDTH, LOD_LEVELS);
    const double margin_v = LodCoordMargin(COORD, LOD_HEIGHT, LOD_LEVELS);
    CHECK_TRUE(SUITE, "13/32 lands at least 3/16 of a texel from a boundary on every level",
               CHECK_NONE, margin_u >= 0.1875 && margin_v >= 0.1875, "margins %.4f and %.4f",
               margin_u, margin_v);

    // Offset (1, -1): counted in the level's own texels, against the same offset turned into a
    // coordinate with level nought's size, which a translator that adds offsets to coordinates
    // might do.
    uint32_t same = 0;
    for (uint32_t level = 1; level < 4u; ++level) {
        const float right = LodExpected(level, COORD, COORD, 1, -1);
        const float misread = LodExpected(level, COORD + 1.0 / LOD_WIDTH, COORD - 1.0 / LOD_HEIGHT,
                                          0, 0);
        same += right == misread;
    }
    CHECK_TRUE(SUITE, "an offset in a level's own texels reads another texel than one in level "
                      "nought's on levels 1 to 3",
               CHECK_NONE, same == 0u, "%u levels read the same texel either way", (unsigned)same);

    const double blended = LodExpectedLinear(1.25, COORD, COORD);
    CHECK_TRUE(SUITE, "lambda 1.25 blends level 1's 1019 and level 2's 2001 to 1264.5, a float "
                      "exactly",
               CHECK_NONE, blended == 1264.5 && (double)(float)blended == blended, "got %.17g",
               blended);
}
