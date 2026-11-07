// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../fixedref.h"
#include "../report.h"
#include "tests.h"

// The fixed suite's rules, against answers worked out by hand on a 64 by 64 target: where a
// point's square lands, where a rectangle lands through each viewport the suite sets, which face
// a counter clockwise triangle shows there, the window depth of each z, and which references and
// bounds pass.

static const char *const SUITE = "reference";

static bool SameRect(FixedRect got, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    return got.x0 == x0 && got.y0 == y0 && got.x1 == x1 && got.y1 == y1;
}

static void PointRects(void) {
    // Size 1 at the centre of pixel (10, 10); 3 at (42, 10)'s; 4 and 8 on the corners (16, 44)
    // and (44, 44): the suite's four points.
    const FixedRect one = FixedPointRect(10.5, 10.5, 1.0);
    const FixedRect three = FixedPointRect(42.5, 10.5, 3.0);
    const FixedRect four = FixedPointRect(16.0, 44.0, 4.0);
    const FixedRect eight = FixedPointRect(44.0, 44.0, 8.0);
    CHECK_TRUE(SUITE, "points of size 1, 3, 4 and 8 cover their squares of pixels", CHECK_NONE,
               SameRect(one, 10, 10, 11, 11) && SameRect(three, 41, 9, 44, 12) &&
                   SameRect(four, 14, 42, 18, 46) && SameRect(eight, 40, 40, 48, 48),
               "size 1 %d,%d-%d,%d; 3 %d,%d-%d,%d; 4 %d,%d-%d,%d; 8 %d,%d-%d,%d", one.x0, one.y0,
               one.x1, one.y1, three.x0, three.y0, three.x1, three.y1, four.x0, four.y0, four.x1,
               four.y1, eight.x0, eight.y0, eight.x1, eight.y1);
    // A size 1 point on a pixel corner, or an even one on a centre, puts centres on its edges.
    CHECK_TRUE(SUITE, "the fixed suite's points put no pixel centre on an edge", CHECK_NONE,
               FixedPointEdgeFree(10.5, 10.5, 1.0) && FixedPointEdgeFree(42.5, 10.5, 3.0) &&
                   FixedPointEdgeFree(16.0, 44.0, 4.0) && FixedPointEdgeFree(44.0, 44.0, 8.0) &&
                   !FixedPointEdgeFree(10.0, 10.0, 1.0) && !FixedPointEdgeFree(10.5, 10.5, 4.0),
               "the edge rule disagrees with a hand-worked point");
}

static void Projections(void) {
    FixedTransform t;
    memset(&t, 0, sizeof(t));
    t.width = 64;
    t.height = 64;
    // The upper left device: clip x -0.75 to -0.25 is 8 to 24, clip y 0.75 to 0.25 rows 8 to 24.
    const FixedRect plain = FixedProjectRect(&t, -0.75, 0.25, -0.25, 0.75);
    t.scale_y_positive = true;
    const FixedRect mirrored = FixedProjectRect(&t, -0.75, 0.25, -0.25, 0.75);
    t.scale_y_positive = false;
    t.negate_y = true;
    const FixedRect negated = FixedProjectRect(&t, -0.75, 0.25, -0.25, 0.75);
    t.negate_y = false;
    t.swap_xy = true;
    // x from clip y 0.25 to 0.75 is 40 to 56; y from clip x -0.75 to -0.25 is rows 40 to 56.
    const FixedRect swapped = FixedProjectRect(&t, -0.75, 0.25, -0.25, 0.75);
    CHECK_TRUE(SUITE, "a rectangle lands where each viewport of the fixed suite puts it",
               CHECK_NONE,
               SameRect(plain, 8, 8, 24, 24) && SameRect(mirrored, 8, 40, 24, 56) &&
                   SameRect(negated, 8, 40, 24, 56) && SameRect(swapped, 40, 40, 56, 56),
               "plain %d,%d-%d,%d; positive scale %d,%d-%d,%d; NegativeY %d,%d-%d,%d; swapped "
               "%d,%d-%d,%d",
               plain.x0, plain.y0, plain.x1, plain.y1, mirrored.x0, mirrored.y0, mirrored.x1,
               mirrored.y1, negated.x0, negated.y0, negated.x1, negated.y1, swapped.x0,
               swapped.y0, swapped.x1, swapped.y1);
}

static void Faces(void) {
    FixedTransform t;
    memset(&t, 0, sizeof(t));
    t.width = 64;
    t.height = 64;
    // deko3d's upper left device: the viewport flips y, the winding is not flipped, and a counter
    // clockwise triangle is front with the front counter clockwise.
    const bool plain = FixedFrontFacing(&t, true);
    const bool plain_cw = FixedFrontFacing(&t, false);
    t.scale_y_positive = true;
    const bool scaled = FixedFrontFacing(&t, true);
    t.negate_y = true;
    const bool both = FixedFrontFacing(&t, true);
    t.scale_y_positive = false;
    const bool negated = FixedFrontFacing(&t, true);
    t.negate_y = false;
    // The lower left device: the viewport left alone and the winding flipped.
    t.scale_y_positive = true;
    t.flip_winding = true;
    const bool lower_left = FixedFrontFacing(&t, true);
    CHECK_TRUE(SUITE, "a counter clockwise triangle's face under each viewport of the fixed suite",
               CHECK_NONE,
               plain && !plain_cw && !scaled && !negated && both && lower_left,
               "front: plain %d, front clockwise %d, positive scale %d, NegativeY %d, both %d, "
               "lower left %d; expected 1 0 0 0 1 1",
               plain, plain_cw, scaled, negated, both, lower_left);
}

static void Depths(void) {
    const float a = FixedWindowDepth(-1.0f, true), b = FixedWindowDepth(-0.5f, true);
    const float c = FixedWindowDepth(0.0f, true), d = FixedWindowDepth(0.5f, true);
    const float e = FixedWindowDepth(0.25f, false);
    CHECK_TRUE(SUITE, "z of -1, -0.5, 0 and 0.5 are depths 0, 0.25, 0.5 and 0.75 from minus one",
               CHECK_NONE, a == 0.0f && b == 0.25f && c == 0.5f && d == 0.75f && e == 0.25f,
               "got %.9g %.9g %.9g %.9g, and %.9g for 0.25 from nought", (double)a, (double)b,
               (double)c, (double)d, (double)e);
    // The bounds hold the buffer's 0.5 with both ends included.
    CHECK_TRUE(SUITE, "depth bounds keep a stored 0.5 at either end and reject it outside",
               CHECK_NONE,
               FixedBoundsPass(0.5f, 0.375f, 0.625f) && FixedBoundsPass(0.5f, 0.5f, 0.75f) &&
                   FixedBoundsPass(0.5f, 0.25f, 0.5f) && !FixedBoundsPass(0.5f, 0.125f, 0.375f) &&
                   !FixedBoundsPass(0.5f, 0.625f, 0.875f),
               "a bound disagrees with the hand-worked answer");
}

static void Alpha(void) {
    // Alpha 0.5 against 0.25, 0.5 and 0.75, a bit each, the first reference in bit 2.
    static const uint32_t WANT[FIXED_COMPARE_COUNT] = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u };
    static const float REFERENCES[3] = { 0.25f, 0.5f, 0.75f };
    uint32_t got[FIXED_COMPARE_COUNT];
    bool right = true, apart = true;
    for (int op = 0; op < FIXED_COMPARE_COUNT; ++op) {
        got[op] = 0;
        for (int i = 0; i < 3; ++i) {
            got[op] |= (uint32_t)FixedAlphaPasses((FixedCompare)op, 0.5f, REFERENCES[i])
                       << (2 - i);
        }
        right = right && got[op] == WANT[op];
        for (int other = 0; other < op; ++other) {
            apart = apart && got[other] != got[op];
        }
    }
    CHECK_TRUE(SUITE, "an alpha of 0.5 at three references tells the eight alpha tests apart",
               CHECK_NONE, right && apart,
               "patterns never..always %u %u %u %u %u %u %u %u, expected 0 to 7 in order",
               (unsigned)got[0], (unsigned)got[1], (unsigned)got[2], (unsigned)got[3],
               (unsigned)got[4], (unsigned)got[5], (unsigned)got[6], (unsigned)got[7]);
    CHECK_TRUE(SUITE, "the alpha tests' names read as the fixed suite's cases do", CHECK_NONE,
               strcmp(FixedCompareName(FIXED_GEQUAL), "greater than or equal") == 0 &&
                   strcmp(FixedCompareName(FIXED_NOTEQUAL), "not equal") == 0,
               "got \"%s\" and \"%s\"", FixedCompareName(FIXED_GEQUAL),
               FixedCompareName(FIXED_NOTEQUAL));
}

void RunReferenceFixed(void) {
    PointRects();
    Projections();
    Faces();
    Depths();
    Alpha();
}
