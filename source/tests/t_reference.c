// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "../check.h"
#include "../raster.h"
#include "../report.h"
#include "tests.h"

// The suite that checks the expected answers, not the machine.
//
// Everything below is arithmetic a drawing case relies on to know what it should have got, and
// none of it touches a graphics device. It also runs on the console, which is not redundant: the
// same reference code has to produce the same answers there, or two reports that disagree would
// be blamed on the graphics unit when the judge was what differed. Each case is one hand worked
// value or one property that holds across a sweep, never a restatement of the implementation.

static const char *const SUITE = "reference";

/// The viewport the drawing suites use: the whole of a 64 by 64 target.
static const RasterViewport FULL_TARGET = { 0.0f, 0.0f, 64.0f, 64.0f };

static void CheckViewportTransform(void) {
    // Clip space nought is the middle of the target. With a 64 wide viewport that is 32.0, the
    // boundary between pixels 31 and 32, not the centre of either.
    const RasterPoint middle = RasterProject(&FULL_TARGET, 0.0f, 0.0f);
    CHECK_TRUE(SUITE, "clip space origin is the middle of the target", CHECK_NONE,
               middle.x == 32.0 && middle.y == 32.0, "origin landed at (%.4f,%.4f), expected (32,32)",
               middle.x, middle.y);

    // The corner that decides the direction of the vertical axis, and the one thing in this
    // file most likely to be wrong on a machine nobody has run it on yet. Plus one in y is the
    // top row, because the viewport's vertical scale is negative.
    const RasterPoint top_right = RasterProject(&FULL_TARGET, 1.0f, 1.0f);
    CHECK_TRUE(SUITE, "clip space plus y is the top of the target", CHECK_NONE,
               top_right.x == 64.0 && top_right.y == 0.0,
               "(+1,+1) landed at (%.4f,%.4f), expected (64,0)", top_right.x, top_right.y);

    const RasterPoint bottom_left = RasterProject(&FULL_TARGET, -1.0f, -1.0f);
    CHECK_TRUE(SUITE, "clip space minus y is the bottom of the target", CHECK_NONE,
               bottom_left.x == 0.0 && bottom_left.y == 64.0,
               "(-1,-1) landed at (%.4f,%.4f), expected (0,64)", bottom_left.x, bottom_left.y);

    // An offset viewport moves the rectangle and nothing else. Half of one of the drawing cases
    // is that claim, so it is pinned here where it costs nothing to check.
    const RasterViewport offset = { 16.0f, 16.0f, 32.0f, 32.0f };
    const RasterPoint moved = RasterProject(&offset, -1.0f, 1.0f);
    CHECK_TRUE(SUITE, "an offset viewport moves the rectangle", CHECK_NONE,
               moved.x == 16.0 && moved.y == 16.0,
               "(-1,+1) under a viewport at 16,16 landed at (%.4f,%.4f), expected (16,16)",
               moved.x, moved.y);
}

static void CheckCoverageRule(void) {
    // The triangle the rule was derived on, stated in target space: a right angle at the origin
    // with the interior below and to the right of it.
    const RasterPoint corners[3] = { { 0.0, 0.0 }, { 1.0, 0.0 }, { 0.0, 1.0 } };
    RasterTriangle triangle;
    RasterTriangleFromPoints(&triangle, corners);

    CHECK_TRUE(SUITE, "an interior point is covered", CHECK_NONE, RasterCovers(&triangle, 0.25, 0.25),
               "(0.25,0.25) was not covered by the unit triangle");
    CHECK_TRUE(SUITE, "an exterior point is not covered", CHECK_NONE, !RasterCovers(&triangle, 0.9, 0.9),
               "(0.9,0.9) was covered by the unit triangle");

    // The three branches of the top and left rule, one point each. These are the only cases in
    // this file where the answer is a convention rather than a consequence.
    CHECK_TRUE(SUITE, "a point on the top edge is covered", CHECK_NONE, RasterCovers(&triangle, 0.5, 0.0),
               "(0.5,0) on the horizontal edge above the interior was not covered");
    CHECK_TRUE(SUITE, "a point on the left edge is covered", CHECK_NONE, RasterCovers(&triangle, 0.0, 0.5),
               "(0,0.5) on the vertical edge left of the interior was not covered");
    CHECK_TRUE(SUITE, "a point on an edge that is neither top nor left is not covered", CHECK_NONE,
               !RasterCovers(&triangle, 0.5, 0.5),
               "(0.5,0.5) on the hypotenuse was covered");

    // The same three answers with the vertices given the other way round. The rule is about the
    // shape, not about the order the corners arrived in, and a case that wound its triangle the
    // other way must not get a different picture.
    const RasterPoint reversed[3] = { { 0.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 0.0 } };
    RasterTriangle other;
    RasterTriangleFromPoints(&other, reversed);
    const bool same = other.flipped && RasterCovers(&other, 0.25, 0.25) &&
                      RasterCovers(&other, 0.5, 0.0) && RasterCovers(&other, 0.0, 0.5) &&
                      !RasterCovers(&other, 0.5, 0.5) && !RasterCovers(&other, 0.9, 0.9);
    CHECK_TRUE(SUITE, "winding does not change which pixels are covered", CHECK_NONE, same,
               "the reversed winding disagreed with the original (flipped=%d)", (int)other.flipped);

    const RasterPoint flat[3] = { { 0.0, 0.0 }, { 4.0, 4.0 }, { 8.0, 8.0 } };
    RasterTriangle degenerate;
    RasterTriangleFromPoints(&degenerate, flat);
    CHECK_TRUE(SUITE, "a triangle with no area covers nothing", CHECK_NONE,
               !RasterCovers(&degenerate, 2.0, 2.0) && !RasterCovers(&degenerate, 1.0, 5.0),
               "a degenerate triangle claimed a point");
}

static void CheckSharedEdge(void) {
    // The pair the fill rule cases draw: a 32 by 32 square split along the diagonal that runs
    // through 32 pixel centres exactly. If the rule is right, every centre in the square belongs
    // to exactly one of them. If it is wrong in either direction the square has a seam drawn
    // twice or not at all, which is the visible symptom this whole rule exists to prevent.
    const RasterPoint upper_points[3] = { { 16.0, 16.0 }, { 48.0, 16.0 }, { 48.0, 48.0 } };
    const RasterPoint lower_points[3] = { { 16.0, 16.0 }, { 48.0, 48.0 }, { 16.0, 48.0 } };
    RasterTriangle upper;
    RasterTriangle lower;
    RasterTriangleFromPoints(&upper, upper_points);
    RasterTriangleFromPoints(&lower, lower_points);

    uint32_t both = 0;
    uint32_t neither = 0;
    uint32_t on_diagonal_upper = 0;
    for (uint32_t y = 16; y < 48; ++y) {
        for (uint32_t x = 16; x < 48; ++x) {
            const double cx = (double)x + 0.5;
            const double cy = (double)y + 0.5;
            const bool in_upper = RasterCovers(&upper, cx, cy);
            const bool in_lower = RasterCovers(&lower, cx, cy);
            if (in_upper && in_lower) {
                ++both;
            }
            if (!in_upper && !in_lower) {
                ++neither;
            }
            if (x == y && in_upper) {
                ++on_diagonal_upper;
            }
        }
    }

    CHECK_TRUE(SUITE, "a shared edge is drawn exactly once", CHECK_NONE, both == 0 && neither == 0,
               "%u centres were claimed by both triangles and %u by neither", (unsigned)both,
               (unsigned)neither);
    // Which of the two gets them is the half of the rule a reader will want stated out loud: the
    // diagonal is a left edge of the upper triangle, so the upper one takes all 32.
    CHECK_TRUE(SUITE, "the triangle whose left edge it is takes the shared centres", CHECK_NONE,
               on_diagonal_upper == 32,
               "the upper triangle claimed %u of the 32 centres on the diagonal",
               (unsigned)on_diagonal_upper);
}

static void CheckEdgeDistanceAndMargin(void) {
    // A right triangle whose edges are axis aligned, so the distances are readable by eye.
    const RasterPoint corners[3] = { { 0.0, 0.0 }, { 40.0, 0.0 }, { 0.0, 40.0 } };
    RasterTriangle triangle;
    RasterTriangleFromPoints(&triangle, corners);

    // (4, 10) is 4 from the vertical edge, 10 from the horizontal one, and further still from
    // the hypotenuse, so the nearest is 4.
    const double distance = RasterEdgeDistance(&triangle, 4.0, 10.0);
    CHECK_TRUE(SUITE, "edge distance is the perpendicular one in pixels", CHECK_NONE,
               distance > 3.999999 && distance < 4.000001,
               "(4,10) measured %.6f from the nearest edge, expected 4", distance);

    // Pixel 0 of row 4 has its centre at (0.5, 4.5), which is half a pixel from the vertical
    // edge: the edge runs along the pixel's own left boundary. A margin of three quarters of a
    // pixel holds it back.
    CHECK_TRUE(SUITE, "a margin holds back the pixels an edge runs through", CHECK_NONE,
               RasterClassifyPixel(&triangle, 0, 4, 0.75) == RASTER_NEAR_EDGE,
               "the pixel whose centre is half a pixel from an edge was not held back");
    // The comparison is strict, which is what lets the drawing cases use a margin of exactly
    // half a pixel and still judge the pixels an edge passes exactly between.
    CHECK_TRUE(SUITE, "a centre exactly a margin away is still judged", CHECK_NONE,
               RasterClassifyPixel(&triangle, 0, 4, 0.5) == RASTER_INSIDE,
               "the pixel exactly half a pixel from the edge was held back by that same margin");
    CHECK_TRUE(SUITE, "with no margin every pixel is judged", CHECK_NONE,
               RasterClassifyPixel(&triangle, 0, 4, 0.0) == RASTER_INSIDE,
               "the same pixel was not called inside when nothing was held back");
    CHECK_TRUE(SUITE, "a pixel well outside is called outside", CHECK_NONE,
               RasterClassifyPixel(&triangle, 50, 50, 0.5) == RASTER_OUTSIDE,
               "a pixel far outside the triangle was not called outside");
}

void RunReference(void) {
    CheckViewportTransform();
    CheckCoverageRule();
    CheckSharedEdge();
    CheckEdgeDistanceAndMargin();
}
