// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../blendref.h"
#include "../check.h"
#include "../coherref.h"
#include "../fmathref.h"
#include "../formatref.h"
#include "../intref.h"
#include "../raster.h"
#include "../texref.h"
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
               middle.x == 32.0 && middle.y == 32.0,
               "origin landed at (%.4f,%.4f), expected (32,32)", middle.x, middle.y);

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

    CHECK_TRUE(SUITE, "an interior point is covered", CHECK_NONE,
               RasterCovers(&triangle, 0.25, 0.25),
               "(0.25,0.25) was not covered by the unit triangle");
    CHECK_TRUE(SUITE, "an exterior point is not covered", CHECK_NONE,
               !RasterCovers(&triangle, 0.9, 0.9),
               "(0.9,0.9) was covered by the unit triangle");

    // The three branches of the top and left rule, one point each. These are the only cases in
    // this file where the answer is a convention rather than a consequence.
    CHECK_TRUE(SUITE, "a point on the top edge is covered", CHECK_NONE,
               RasterCovers(&triangle, 0.5, 0.0),
               "(0.5,0) on the horizontal edge above the interior was not covered");
    CHECK_TRUE(SUITE, "a point on the left edge is covered", CHECK_NONE,
               RasterCovers(&triangle, 0.0, 0.5),
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

/// Whether the three clip space corners of @p clip land on @p expected under @p viewport.
static bool LandsOn(const float clip[6], const RasterViewport *viewport,
                    const RasterPoint expected[3]) {
    for (int corner = 0; corner < 3; ++corner) {
        const RasterPoint point = RasterProject(viewport, clip[corner * 2], clip[corner * 2 + 1]);
        if (point.x != expected[corner].x || point.y != expected[corner].y) {
            return false;
        }
    }
    return true;
}

/// The geometry the rasterisation suite actually draws, projected through the viewport it
/// actually uses.
///
/// Checking the rule against a triangle written out in target space would prove the rule and
/// leave the geometry unchecked, and the exact cases rest on corners landing on whole pixel
/// boundaries.
static void CheckDrawnGeometry(void) {
    const RasterPoint slanted[3] = { { 9.0, 5.0 }, { 57.0, 19.0 }, { 23.0, 60.0 } };
    CHECK_TRUE(SUITE, "the slanted triangle lands on whole pixel boundaries", CHECK_NONE,
               LandsOn(RASTER_SLANTED, &RASTER_FULL_VIEWPORT, slanted),
               "it landed at (%.4f,%.4f) (%.4f,%.4f) (%.4f,%.4f), expected (9,5) (57,19) (23,60)",
               RasterProject(&RASTER_FULL_VIEWPORT, RASTER_SLANTED[0], RASTER_SLANTED[1]).x,
               RasterProject(&RASTER_FULL_VIEWPORT, RASTER_SLANTED[0], RASTER_SLANTED[1]).y,
               RasterProject(&RASTER_FULL_VIEWPORT, RASTER_SLANTED[2], RASTER_SLANTED[3]).x,
               RasterProject(&RASTER_FULL_VIEWPORT, RASTER_SLANTED[2], RASTER_SLANTED[3]).y,
               RasterProject(&RASTER_FULL_VIEWPORT, RASTER_SLANTED[4], RASTER_SLANTED[5]).x,
               RasterProject(&RASTER_FULL_VIEWPORT, RASTER_SLANTED[4], RASTER_SLANTED[5]).y);

    // The same triangle through the offset viewport the seventh case uses. Half the size means
    // every corner lands on a half pixel boundary, which is still exact and still nowhere near a
    // pixel centre.
    const RasterPoint halved[3] = { { 20.5, 18.5 }, { 44.5, 25.5 }, { 27.5, 46.0 } };
    CHECK_TRUE(SUITE, "the offset viewport halves the triangle and moves it", CHECK_NONE,
               LandsOn(RASTER_SLANTED, &RASTER_HALF_VIEWPORT, halved),
               "it landed at (%.4f,%.4f), expected (20.5,18.5) for the first corner",
               RasterProject(&RASTER_HALF_VIEWPORT, RASTER_SLANTED[0], RASTER_SLANTED[1]).x,
               RasterProject(&RASTER_HALF_VIEWPORT, RASTER_SLANTED[0], RASTER_SLANTED[1]).y);

    const RasterPoint upper[3] = { { 16.0, 16.0 }, { 48.0, 16.0 }, { 48.0, 48.0 } };
    const RasterPoint lower[3] = { { 16.0, 16.0 }, { 48.0, 48.0 }, { 16.0, 48.0 } };
    CHECK_TRUE(SUITE, "the fill rule triangles are the two halves of a whole pixel square",
               CHECK_NONE,
               LandsOn(RASTER_UPPER_RIGHT_HALF, &RASTER_FULL_VIEWPORT, upper) &&
                   LandsOn(RASTER_LOWER_LEFT_HALF, &RASTER_FULL_VIEWPORT, lower),
               "one of the six corners did not land where the fill rule cases need it");
}

static void CheckSharedEdge(void) {
    // The pair the fill rule cases draw, projected from the very clip space values those cases
    // hand the hardware: every centre in the square has to belong to exactly one of them, or a
    // mesh has a seam drawn twice or not at all.
    RasterTriangle upper;
    RasterTriangle lower;
    RasterTriangleFromClip(&upper, &RASTER_FULL_VIEWPORT, RASTER_UPPER_RIGHT_HALF);
    RasterTriangleFromClip(&lower, &RASTER_FULL_VIEWPORT, RASTER_LOWER_LEFT_HALF);

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

/// One pixel's worth of integer arithmetic, worked out by hand and written down.
///
/// Every constant below follows from the mixed value one pixel produces, and each is stated
/// rather than recomputed, so a change to the reference that stayed self consistent would still
/// have to argue with these.
static void CheckIntegerArithmetic(void) {
    const uint32_t x = 37;
    const uint32_t y = 23;
    const uint32_t probe = 0x5A17C0DEu;
    const uint32_t v = IntMathMix(x, y);

    CHECK_TRUE(SUITE, "the mixed value overflows and wraps", CHECK_NONE, v == 0x71A39D6Au,
               "the mix at (37,23) came out 0x%08x, expected 0x71a39d6a", (unsigned)v);

    // 37.5 is 1.171875 times two to the fifth, so the exponent field holds 132 and the mantissa
    // holds 0.171875. 23.5 is 1.46875 times two to the fourth, so 131 and 0.46875. The halves
    // are the point: the fragment coordinate names the centre of the pixel.
    CHECK_TRUE(SUITE, "the fragment coordinate reference carries the half pixel", CHECK_NONE,
               IntMathReference(INTMATH_FRAGCOORD_X, x, y, probe) == 0x42160000u &&
                   IntMathReference(INTMATH_FRAGCOORD_Y, x, y, probe) == 0x41BC0000u,
               "the coordinate bits came out 0x%08x and 0x%08x, expected 0x42160000 and 0x41bc0000",
               (unsigned)IntMathReference(INTMATH_FRAGCOORD_X, x, y, probe),
               (unsigned)IntMathReference(INTMATH_FRAGCOORD_Y, x, y, probe));

    static const struct {
        IntMathMode mode;
        uint32_t expected;
    } BY_HAND[] = {
        { INTMATH_UNIFORM, 0x5A17C0DEu },
        { INTMATH_MIX, 0x71A39D6Au },
        // Thirteen places left, so the bottom thirteen bits are nought and the top thirteen fell
        // off the end.
        { INTMATH_SHIFT_LEFT, 0x73AD4000u },
        // 1906548074 divided by 128 is 14894906, which is 0xE3473A.
        { INTMATH_SHIFT_RIGHT, 0x00E3473Au },
        // The same, because 0x71A39D6A has its top bit clear. The case that separates the two is
        // below, over the whole grid.
        { INTMATH_SHIFT_ARITHMETIC, 0x00E3473Au },
        // Nibble by nibble: 7&F 1&0 A&F 3&0 9&F D&0 6&F A&0.
        { INTMATH_AND, 0x70A09060u },
        { INTMATH_OR, 0x7FAF9F6Fu },
        // 7^A=D 1^5=4 A^A=0 3^5=6 9^A=3 D^5=8 6^A=C A^5=F.
        { INTMATH_XOR, 0xD40638CFu },
        // 0111 0001 1010 0011 1001 1101 0110 1010 has 3+1+2+2+2+3+2+2 bits set.
        { INTMATH_BIT_COUNT, 17u },
        // The low byte is 0x6A, so the lowest set bit is bit one; the top nibble is 7, so the
        // highest is bit thirty. One times 32 plus 30.
        { INTMATH_BIT_SCAN, 62u },
        // 1906 times 1000003 is 1906005718, and 1906548074 minus that is 542356.
        { INTMATH_DIVIDE, 1906u },
        { INTMATH_REMAINDER, 542356u },
    };

    uint32_t wrong = 0;
    IntMathMode first_wrong = INTMATH_MODE_COUNT;
    uint32_t first_got = 0;
    uint32_t first_want = 0;
    for (size_t i = 0; i < sizeof(BY_HAND) / sizeof(BY_HAND[0]); ++i) {
        const uint32_t got = IntMathReference(BY_HAND[i].mode, x, y, probe);
        if (got == BY_HAND[i].expected) {
            continue;
        }
        if (wrong == 0) {
            first_wrong = BY_HAND[i].mode;
            first_got = got;
            first_want = BY_HAND[i].expected;
        }
        ++wrong;
    }
    CHECK_TRUE(SUITE, "the integer reference matches the arithmetic worked out by hand",
               CHECK_NONE, wrong == 0,
               "%u of %u modes disagreed, first was \"%s\" at 0x%08x, expected 0x%08x",
               (unsigned)wrong, (unsigned)(sizeof(BY_HAND) / sizeof(BY_HAND[0])),
               IntMathName(first_wrong), (unsigned)first_got, (unsigned)first_want);

    // Three properties that hold for every input, swept over the same pixels the suite draws.
    // They need no constants, so a reference consistently wrong in one direction still has to
    // satisfy them.
    uint32_t division_broken = 0;
    uint32_t count_broken = 0;
    uint32_t sign_broken = 0;
    uint32_t negatives = 0;
    for (uint32_t py = 0; py < 64; ++py) {
        for (uint32_t px = 0; px < 64; ++px) {
            const uint32_t value = IntMathMix(px, py);

            // A long division done a bit at a time, which shares no line with the reference.
            // Multiplying the quotient back would prove nothing, since the language guarantees
            // that of its own operators whatever the reference did.
            uint32_t quotient_by_hand = 0;
            uint32_t remainder_by_hand = 0;
            for (uint32_t bit = 32u; bit > 0u; --bit) {
                remainder_by_hand = (remainder_by_hand << 1) | ((value >> (bit - 1u)) & 1u);
                quotient_by_hand <<= 1;
                if (remainder_by_hand >= INTMATH_DIVISOR) {
                    remainder_by_hand -= INTMATH_DIVISOR;
                    quotient_by_hand |= 1u;
                }
            }
            if (IntMathReference(INTMATH_DIVIDE, px, py, probe) != quotient_by_hand ||
                IntMathReference(INTMATH_REMAINDER, px, py, probe) != remainder_by_hand) {
                ++division_broken;
            }

            // And the bit count against a table of the sixteen nibbles, which is a different
            // algorithm rather than a restatement of the same loop.
            static const uint32_t NIBBLE_BITS[16] = { 0, 1, 1, 2, 1, 2, 2, 3,
                                                      1, 2, 2, 3, 2, 3, 3, 4 };
            uint32_t count_by_table = 0;
            for (uint32_t nibble = 0; nibble < 8u; ++nibble) {
                count_by_table += NIBBLE_BITS[(value >> (nibble * 4u)) & 0xFu];
            }
            if (IntMathReference(INTMATH_BIT_COUNT, px, py, probe) != count_by_table) {
                ++count_broken;
            }

            // The two right shifts have to agree exactly when the top bit is clear and differ
            // exactly when it is set, which is the entire difference between them.
            const uint32_t logical = IntMathReference(INTMATH_SHIFT_RIGHT, px, py, probe);
            const uint32_t arithmetic = IntMathReference(INTMATH_SHIFT_ARITHMETIC, px, py, probe);
            const bool negative = (value & 0x80000000u) != 0u;
            negatives += negative ? 1u : 0u;
            if (negative == (logical == arithmetic)) {
                ++sign_broken;
            }
        }
    }

    CHECK_TRUE(SUITE, "the division agrees with a long division done a bit at a time", CHECK_NONE,
               division_broken == 0, "%u of 4096 inputs disagreed",
               (unsigned)division_broken);
    CHECK_TRUE(SUITE, "the bit count agrees with a table of nibbles", CHECK_NONE,
               count_broken == 0, "%u of 4096 inputs disagreed", (unsigned)count_broken);
    // The sweep is only worth anything if it saw both kinds of input, so the count of negatives
    // is part of the claim rather than a number in a note.
    CHECK_TRUE(SUITE, "the two right shifts differ exactly on the values with the top bit set",
               CHECK_NONE, sign_broken == 0 && negatives > 0 && negatives < 4096,
               "%u of 4096 inputs disagreed, and %u of them had the top bit set",
               (unsigned)sign_broken, (unsigned)negatives);
}

/// Reads the eight hex digits the case's own sentence ends with.
///
/// The table in blendref.c writes the answer out in words and then in bytes, and this is what
/// makes those words checkable: the sentence a reader trusts and the arithmetic the reference
/// performs are compared against each other rather than both being taken on faith. Returns false
/// if the sentence does not end in a colour, which is itself a failure worth reporting.
static bool ArithmeticSentenceColor(const char *sentence, uint8_t out[4]) {
    const size_t length = strlen(sentence);
    if (length < 8) {
        return false;
    }
    const char *const digits = sentence + (length - 8);
    for (int i = 0; i < 4; ++i) {
        unsigned value = 0;
        for (int half = 0; half < 2; ++half) {
            const char c = digits[i * 2 + half];
            unsigned nibble;
            if (c >= '0' && c <= '9') {
                nibble = (unsigned)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = (unsigned)(c - 'a') + 10u;
            } else {
                return false;
            }
            value = (value << 4) | nibble;
        }
        out[i] = (uint8_t)value;
    }
    return true;
}

static void CheckBlendArithmetic(void) {
    // Every byte has to survive being turned into a normalised float and back, or the colours
    // the blend cases name are not the colours the hardware receives and none of the arithmetic
    // below means anything. This is also what lets the clear and the vertex carry an arbitrary
    // byte rather than only the ends of the range.
    uint32_t round_trip_broken = 0;
    for (uint32_t value = 0; value < 256u; ++value) {
        if (BlendFloatToUnorm(BlendUnormToFloat((uint8_t)value)) != (uint8_t)value) {
            ++round_trip_broken;
        }
    }
    CHECK_TRUE(SUITE, "every byte survives the trip through a normalised float", CHECK_NONE,
               round_trip_broken == 0, "%u of the 256 bytes came back as something else",
               (unsigned)round_trip_broken);

    // The sentence against the arithmetic, case by case.
    uint32_t mismatched = 0;
    const char *first_mismatch = "";
    uint8_t first_said[4] = { 0, 0, 0, 0 };
    uint8_t first_did[4] = { 0, 0, 0, 0 };
    for (size_t index = 0; index < BlendCaseCount(); ++index) {
        const BlendCase *const test = BlendCaseAt(index);
        // Initialised, because the reader below may give up part way through a
        // sentence that does not end in a colour and the failure path copies all four.
        uint8_t said[4] = { 0, 0, 0, 0 };
        uint8_t did[4];
        BlendReference(&test->spec, test->source, test->destination, did);
        if (!ArithmeticSentenceColor(test->arithmetic, said) || memcmp(said, did, 4) != 0) {
            if (mismatched == 0) {
                first_mismatch = test->name;
                memcpy(first_said, said, 4);
                memcpy(first_did, did, 4);
            }
            ++mismatched;
        }
    }
    CHECK_TRUE(SUITE, "each blend case's sentence is the arithmetic the reference does",
               CHECK_NONE, mismatched == 0,
               "%u of %u disagreed, first was \"%s\": the sentence says %02x%02x%02x%02x, the "
               "reference gives %02x%02x%02x%02x",
               (unsigned)mismatched, (unsigned)BlendCaseCount(), first_mismatch, first_said[0],
               first_said[1], first_said[2], first_said[3], first_did[0], first_did[1],
               first_did[2], first_did[3]);

    // No case may give the alpha channel a destination factor of its own, because the graphics
    // library sends the colour one in its place. Held here rather than in a comment, so nobody
    // can add a case without meeting it; the day the blend suite finds the factor arriving, this
    // is the rule to lift.
    uint32_t own_alpha_destination = 0;
    const char *first_own = "";
    for (size_t index = 0; index < BlendCaseCount(); ++index) {
        const BlendCase *const test = BlendCaseAt(index);
        if (test->spec.alpha_destination == test->spec.color_destination) {
            continue;
        }
        if (own_alpha_destination == 0) {
            first_own = test->name;
        }
        ++own_alpha_destination;
    }
    CHECK_TRUE(SUITE, "no blend case asks for an alpha destination factor of its own", CHECK_NONE,
               own_alpha_destination == 0,
               "%u of %u do, first was \"%s\": the graphics library would draw it with the "
               "colour destination factor in both places and the case would accuse a machine "
               "that had done nothing wrong",
               (unsigned)own_alpha_destination, (unsigned)BlendCaseCount(), first_own);

    // Both of the two remaining ways the table tells the alpha state from the colour one have
    // to stay exercised by a case whose answer moves when they are not. Measured by taking each
    // field from the colour state instead, because a later tidy-up could otherwise flatten the
    // last case that separates one and every case would still pass on every machine.
    uint32_t equation_separated = 0;
    uint32_t source_factor_separated = 0;
    const char *equation_case = "";
    const char *source_factor_case = "";
    for (size_t index = 0; index < BlendCaseCount(); ++index) {
        const BlendCase *const test = BlendCaseAt(index);
        uint8_t honest[4];
        uint8_t wired[4];
        BlendReference(&test->spec, test->source, test->destination, honest);

        BlendSpec wired_equation = test->spec;
        wired_equation.alpha_op = test->spec.color_op;
        BlendReference(&wired_equation, test->source, test->destination, wired);
        if (wired[3] != honest[3]) {
            if (equation_separated == 0) {
                equation_case = test->name;
            }
            ++equation_separated;
        }

        BlendSpec wired_source = test->spec;
        wired_source.alpha_source = test->spec.color_source;
        BlendReference(&wired_source, test->source, test->destination, wired);
        if (wired[3] != honest[3]) {
            if (source_factor_separated == 0) {
                source_factor_case = test->name;
            }
            ++source_factor_separated;
        }
    }
    CHECK_TRUE(SUITE, "a blend case catches the alpha equation taken from the colour one",
               CHECK_NONE, equation_separated > 0,
               "no case in the table would give a different alpha, so an implementation that "
               "ran the alpha channel through the colour equation would pass every one of them");
    CHECK_TRUE(SUITE, "a blend case catches the alpha source factor taken from the colour one",
               CHECK_NONE, source_factor_separated > 0,
               "no case in the table would give a different alpha, so an implementation that "
               "took the alpha source factor from the colour state would pass every one of them");
    ReportNote("blend cases that separate the alpha state: %u by the equation, first \"%s\"; "
               "%u by the source factor, first \"%s\"",
               (unsigned)equation_separated, equation_case, (unsigned)source_factor_separated,
               source_factor_case);

    // And the claim that justifies the exact tier: no case's unrounded result lands near the
    // halfway point between two bytes, so the rounding rule the specification leaves open cannot
    // change any answer. Measured rather than asserted, and the worst is reported either way.
    double worst = 0.0;
    const char *worst_case = "";
    for (size_t index = 0; index < BlendCaseCount(); ++index) {
        const BlendCase *const test = BlendCaseAt(index);
        float scaled[4];
        BlendReferenceScaled(&test->spec, test->source, test->destination, scaled);
        for (int channel = 0; channel < 4; ++channel) {
            const double value = (double)scaled[channel];
            const double distance = fabs(value - floor(value + 0.5));
            if (distance > worst) {
                worst = distance;
                worst_case = test->name;
            }
        }
    }
    CHECK_TRUE(SUITE, "no blend case lands near the halfway point between two bytes", CHECK_NONE,
               worst < 0.01,
               "the worst was \"%s\", %.6f of a step from a whole byte and so %.6f from a tie",
               worst_case, worst, 0.5 - worst);

    // Two properties over every pair of bytes, which need no table at all. Adding with the
    // factors one and zero has to hand back the source whatever the destination was, and the
    // minimum and the maximum have to hand back one of the two operands rather than something
    // in between.
    const BlendSpec source_only = { true,
                                    BLEND_OP_ADD,
                                    BLEND_FACTOR_ONE,
                                    BLEND_FACTOR_ZERO,
                                    BLEND_OP_ADD,
                                    BLEND_FACTOR_ONE,
                                    BLEND_FACTOR_ZERO,
                                    { 0.0f, 0.0f, 0.0f, 0.0f } };
    const BlendSpec smaller = { true,
                                BLEND_OP_MIN,
                                BLEND_FACTOR_ONE,
                                BLEND_FACTOR_ONE,
                                BLEND_OP_MIN,
                                BLEND_FACTOR_ONE,
                                BLEND_FACTOR_ONE,
                                { 0.0f, 0.0f, 0.0f, 0.0f } };
    uint32_t source_broken = 0;
    uint32_t minimum_broken = 0;
    for (uint32_t a = 0; a < 256u; ++a) {
        for (uint32_t b = 0; b < 256u; ++b) {
            const uint8_t source[4] = { (uint8_t)a, (uint8_t)a, (uint8_t)a, (uint8_t)a };
            const uint8_t destination[4] = { (uint8_t)b, (uint8_t)b, (uint8_t)b, (uint8_t)b };
            uint8_t out[4];
            BlendReference(&source_only, source, destination, out);
            if (out[0] != (uint8_t)a || out[3] != (uint8_t)a) {
                ++source_broken;
            }
            BlendReference(&smaller, source, destination, out);
            if (out[0] != (uint8_t)(a < b ? a : b)) {
                ++minimum_broken;
            }
        }
    }
    CHECK_TRUE(SUITE, "adding the source at full weight to nothing gives the source back",
               CHECK_NONE, source_broken == 0, "%u of 65536 pairs came out wrong",
               (unsigned)source_broken);
    CHECK_TRUE(SUITE, "the minimum is always one of its two operands", CHECK_NONE,
               minimum_broken == 0, "%u of 65536 pairs came out wrong",
               (unsigned)minimum_broken);
}

/// The wrap modes, written out index by index rather than recomputed.
///
/// A 4 texel texture asked about every index two whole copies either side of it. The three
/// sequences are the entire difference between the modes, and index minus one is where all three
/// answers part company.
static void CheckWrapModes(void) {
    static const int32_t FIRST = -9;
    static const char REPEAT[] = "3012301230123012301230";
    static const char CLAMP[] = "0000000000123333333333";
    static const char MIRROR[] = "0012332100123321001233";
    static const struct {
        TexWrap mode;
        const char *expected;
        const char *name;
    } MODES[] = {
        { TEXWRAP_REPEAT, REPEAT, "repeating" },
        { TEXWRAP_CLAMP_TO_EDGE, CLAMP, "clamping to the edge" },
        { TEXWRAP_MIRRORED_REPEAT, MIRROR, "mirroring" },
    };

    for (size_t m = 0; m < sizeof(MODES) / sizeof(MODES[0]); ++m) {
        const size_t count = strlen(MODES[m].expected);
        uint32_t wrong = 0;
        int32_t first_index = 0;
        int32_t got = 0;
        int32_t want = 0;
        for (size_t i = 0; i < count; ++i) {
            const int32_t index = FIRST + (int32_t)i;
            const int32_t produced = TexWrapIndex(MODES[m].mode, index, 4);
            const int32_t expected = MODES[m].expected[i] - '0';
            if (produced == expected) {
                continue;
            }
            if (wrong == 0) {
                first_index = index;
                got = produced;
                want = expected;
            }
            ++wrong;
        }
        char name[64];
        snprintf(name, sizeof(name), "%s folds every index onto a texel", MODES[m].name);
        CHECK_TRUE(SUITE, name, CHECK_NONE, wrong == 0,
                   "%u of %u indices disagreed, first was %d giving %d, expected %d",
                   (unsigned)wrong, (unsigned)count, (int)first_index, (int)got, (int)want);
    }
}

/// Where a pixel lands in texel units, for the three quad layouts the texture suite draws.
static void CheckTextureCoordinates(void) {
    // The plain layout: the whole texture across the whole target. The fractional part of a
    // coordinate is always an odd number of thirty seconds, so no pixel centre is ever near a
    // texel boundary.
    double closest = 1.0;
    uint32_t wrong = 0;
    for (uint32_t pixel = 0; pixel < 64u; ++pixel) {
        const double coordinate = TexCoordInTexels(0.0, 1.0, 64u, pixel, 4u);
        const double expected = ((double)pixel + 0.5) / 16.0;
        if (fabs(coordinate - expected) > 1e-12) {
            ++wrong;
        }
        if (TexNearestTexel(TEXWRAP_CLAMP_TO_EDGE, coordinate, 4) != (int32_t)(pixel / 16u)) {
            ++wrong;
        }
        const double distance = fabs(coordinate - floor(coordinate + 0.5));
        if (distance < closest) {
            closest = distance;
        }
    }
    CHECK_TRUE(SUITE, "one texture across the target puts sixteen pixels on each texel",
               CHECK_NONE, wrong == 0 && closest >= 1.0 / 32.0,
               "%u of 64 pixels disagreed, and the closest came within %.6f of a texel boundary",
               (unsigned)wrong, closest);

    // Three copies across the target, which is what the wrap cases draw. Same claim: no pixel
    // centre lands near a boundary, so which texel it names is not a matter of rounding.
    closest = 1.0;
    for (uint32_t pixel = 0; pixel < 64u; ++pixel) {
        const double coordinate = TexCoordInTexels(-1.0, 2.0, 64u, pixel, 4u);
        const double distance = fabs(coordinate - floor(coordinate + 0.5));
        if (distance < closest) {
            closest = distance;
        }
    }
    CHECK_TRUE(SUITE, "three copies across the target still miss every texel boundary",
               CHECK_NONE, closest >= 1.0 / 32.0,
               "the closest pixel came within %.6f of a texel boundary", closest);

    // The layout the linear case draws: one texel per pixel, shifted by half a pixel, so the
    // filter is asked for exactly half of each of the two texels either side. Every step of that
    // is exact in binary.
    uint32_t not_half = 0;
    uint32_t wrong_pair = 0;
    double worst_weight_error = 0.0;
    for (uint32_t pixel = 0; pixel < 64u; ++pixel) {
        const double coordinate = TexCoordInTexels(-0.125, 15.875, 64u, pixel, 4u);
        int32_t pair[2];
        double weight;
        TexLinearTexels(TEXWRAP_REPEAT, coordinate, 4, pair, &weight);
        const double error = fabs(weight - 0.5);
        if (error > worst_weight_error) {
            worst_weight_error = error;
        }
        if (weight != 0.5) {
            ++not_half;
        }
        if (pair[0] != TexWrapIndex(TEXWRAP_REPEAT, (int32_t)pixel - 1, 4) ||
            pair[1] != TexWrapIndex(TEXWRAP_REPEAT, (int32_t)pixel, 4)) {
            ++wrong_pair;
        }
    }
    CHECK_TRUE(SUITE, "the linear layout asks for exactly half of each of two texels", CHECK_NONE,
               not_half == 0 && wrong_pair == 0,
               "%u of 64 weights were not exactly one half (worst off by %.9f) and %u pairs were "
               "not the two texels either side",
               (unsigned)not_half, worst_weight_error, (unsigned)wrong_pair);
}

/// The float reference: its inputs, its own arithmetic, and the budgets it hands out.
static void CheckFloatArithmetic(void) {
    // The processor's fused multiply and add has to really be fused, or the multiply and add
    // case compares against the wrong one of the two answers. These three numbers separate them:
    // rounding twice gives nothing at all, and rounding once gives the difference itself.
    const float epsilon = 1.0f / 8388608.0f;
    const float above = 1.0f + epsilon;
    const float below = 1.0f - epsilon;
    const float fused = fmaf(above, below, -1.0f);
    const float unfused = above * below - 1.0f;
    const float difference = -1.0f / 70368744177664.0f;
    CHECK_TRUE(SUITE, "the reference's multiply and add rounds once, not twice", CHECK_NONE,
               fused == difference && unfused == 0.0f,
               "fused gave %.9g and unfused gave %.9g, expected %.9g and 0", (double)fused,
               (double)unfused, (double)difference);

    // Every operand and argument the shader forms has to be exactly representable, or the
    // reference answers a different question from the one the hardware was asked. Checked by
    // forming each one in single and in double precision and requiring the two to agree.
    uint32_t inexact = 0;
    uint32_t log_in_range = 0;
    uint32_t trig_out_of_range = 0;
    uint32_t not_finite = 0;
    for (uint32_t x = 0; x < 64u; ++x) {
        const float a = FMathInputA(x);
        if ((double)a != ((double)x + 1.0) / 64.0) {
            ++inexact;
        }
        if ((double)(a * 8.0f - 4.0f) != (double)a * 8.0 - 4.0) {
            ++inexact;
        }
        if ((double)(a * 6.0f - 3.0f) != (double)a * 6.0 - 3.0) {
            ++inexact;
        }
        // The logarithm's operand has to stay outside the range from a half to two, where the
        // specification swaps its budget for an absolute one, and carry a full mantissa, so a
        // logarithm that drops the operand's low bits is measured.
        for (uint32_t y = 0; y < 64u; ++y) {
            const float operand = FMathArgument(FMATH_LOG2, x, y);
            uint32_t bits;
            memcpy(&bits, &operand, sizeof(bits));
            if (operand >= 0.5f || operand < FLT_MIN || (bits & 1u) == 0u) {
                ++log_in_range;
            }
        }
        // And the sine and the cosine have to stay inside minus pi to pi, which is the only
        // range the specification bounds them over at all.
        const float angle = a * 6.0f - 3.0f;
        if (angle < -3.14159265f || angle > 3.14159265f) {
            ++trig_out_of_range;
        }
    }
    CHECK_TRUE(SUITE, "every float operand and argument is exactly representable", CHECK_NONE,
               inexact == 0, "%u of 192 formed values differed between single and double",
               (unsigned)inexact);
    CHECK_TRUE(SUITE, "the logarithm's operand stays where one budget covers it", CHECK_NONE,
               log_in_range == 0,
               "%u of 4096 operands landed at a half or above, below the smallest normal, or "
               "without the mantissa's lowest bit",
               (unsigned)log_in_range);
    CHECK_TRUE(SUITE, "the sine and cosine angles stay inside minus pi to pi", CHECK_NONE,
               trig_out_of_range == 0, "%u of 64 angles left the range",
               (unsigned)trig_out_of_range);

    // Addition and multiplication of two floats are exact in double, so rounding the double
    // answer back gives what a float operation would have given. Those two cases allow no
    // tolerance at all and that is the reason they can.
    uint32_t add_differs = 0;
    uint32_t multiply_differs = 0;
    for (uint32_t y = 0; y < 64u; ++y) {
        for (uint32_t x = 0; x < 64u; ++x) {
            const float a = FMathInputA(x);
            const float b = FMathInputB(y);
            if (FMathReference(FMATH_ADD, x, y, 0.0f) != a + b) {
                ++add_differs;
            }
            if (FMathReference(FMATH_MULTIPLY, x, y, 0.0f) != a * b) {
                ++multiply_differs;
            }
            // The ten that are about arithmetic on ordinary numbers; the edge modes after them
            // produce infinities and NaNs on purpose and are pinned one value at a time below.
            for (uint32_t mode = 0; mode < FMATH_FMA_CANCELLATION; ++mode) {
                const float value = FMathReference((FMathMode)mode, x, y, 1.0f);
                if (isnan(value) || isinf(value)) {
                    ++not_finite;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, "the reference's addition is the one a float would have done", CHECK_NONE,
               add_differs == 0, "%u of 4096 sums differed", (unsigned)add_differs);
    CHECK_TRUE(SUITE, "the reference's multiplication is the one a float would have done",
               CHECK_NONE, multiply_differs == 0, "%u of 4096 products differed",
               (unsigned)multiply_differs);
    CHECK_TRUE(SUITE, "no ordinary float case produces an infinity or a nan", CHECK_NONE,
               not_finite == 0, "%u of 40960 reference values were not finite",
               (unsigned)not_finite);

    // And every budget, stated here so that loosening one is a change to this file as well as to
    // the one that hands it out. Every one, not a sample: the ones that were wrong when this file
    // pinned five were among the five it did not pin.
    static const struct {
        FMathMode mode;
        bool absolute;
        float constant;
        float per_argument;
    } EXPECTED_BUDGETS[] = {
        { FMATH_UNIFORM, false, 0.0f, 0.0f },
        { FMATH_ADD, false, 0.0f, 0.0f },
        { FMATH_MULTIPLY, false, 0.0f, 0.0f },
        // The console's FFMA rounds once, as the processor's fused reference does.
        { FMATH_MULTIPLY_ADD, false, 0.0f, 0.0f },
        // 2.5 from the core table and a half for the reference's own rounding.
        { FMATH_DIVIDE, false, 3.0f, 0.0f },
        // Inherited from one over the inverse square root: 2 and 2.5, and a half again.
        { FMATH_SQUARE_ROOT, false, 5.0f, 0.0f },
        // Three plus twice the magnitude of the argument, and a half on the constant.
        { FMATH_EXP2, false, 3.5f, 2.0f },
        { FMATH_LOG2, false, 3.5f, 0.0f },
        { FMATH_SINE, true, 1.0f / 2048.0f, 0.0f },
        { FMATH_COSINE, true, 1.0f / 2048.0f, 0.0f },
        // The edge modes. Nought wherever the answer, once the console's flushing, fusing and
        // rounding are applied, is exact or a choice between operands.
        { FMATH_FMA_CANCELLATION, false, 0.0f, 0.0f },
        { FMATH_MIN_MAX_INFINITY, false, 0.0f, 0.0f },
        { FMATH_MIN_MAX_NAN, false, 0.0f, 0.0f },
        { FMATH_MIX_INFINITY_NAN, false, 0.0f, 0.0f },
        { FMATH_DENORMAL, false, 0.0f, 0.0f },
        { FMATH_EXP2_EDGES, false, 3.5f, 2.0f },
        { FMATH_LOG2_EDGES, false, 3.5f, 0.0f },
        { FMATH_SQRT_TINY, false, 5.0f, 0.0f },
        // 2 for the inverse square root in the table and a half for the reference.
        { FMATH_INVERSESQRT_TINY, false, 2.5f, 0.0f },
        { FMATH_FLOOR, false, 0.0f, 0.0f },
        { FMATH_FRACT, false, 0.0f, 0.0f },
        { FMATH_ROUND, false, 0.0f, 0.0f },
        { FMATH_ROUND_EVEN, false, 0.0f, 0.0f },
    };
    CHECK_TRUE(SUITE, "every float mode has its budget written down here", CHECK_NONE,
               sizeof(EXPECTED_BUDGETS) / sizeof(EXPECTED_BUDGETS[0]) == FMATH_MODE_COUNT,
               "%u budgets pinned for %u modes",
               (unsigned)(sizeof(EXPECTED_BUDGETS) / sizeof(EXPECTED_BUDGETS[0])),
               (unsigned)FMATH_MODE_COUNT);
    uint32_t wrong_budgets = 0;
    const char *first_wrong_budget = "";
    double said = 0.0;
    for (size_t i = 0; i < sizeof(EXPECTED_BUDGETS) / sizeof(EXPECTED_BUDGETS[0]); ++i) {
        const FMathBudget got = FMathBudgetFor(EXPECTED_BUDGETS[i].mode);
        const float value = got.absolute ? got.limit : got.ulps;
        if (got.absolute != EXPECTED_BUDGETS[i].absolute ||
            value != EXPECTED_BUDGETS[i].constant ||
            got.ulps_per_argument != EXPECTED_BUDGETS[i].per_argument) {
            if (wrong_budgets == 0) {
                first_wrong_budget = FMathName(EXPECTED_BUDGETS[i].mode);
                said = (double)value;
            }
            ++wrong_budgets;
        }
    }
    CHECK_TRUE(SUITE, "every float budget is the one the tables give", CHECK_NONE,
               wrong_budgets == 0, "%u of %u disagreed, first was \"%s\" at %g",
               (unsigned)wrong_budgets,
               (unsigned)(sizeof(EXPECTED_BUDGETS) / sizeof(EXPECTED_BUDGETS[0])),
               first_wrong_budget, said);

    // And the reason each one gives has to begin with the number it is, built here from the
    // value rather than read from the text, so a budget changed without its explanation fails.
    uint32_t unexplained = 0;
    const char *first_unexplained = "";
    for (uint32_t mode = 0; mode < FMATH_MODE_COUNT; ++mode) {
        const FMathBudget got = FMathBudgetFor((FMathMode)mode);
        char wanted[64];
        if (got.absolute) {
            snprintf(wanted, sizeof(wanted), "%g absolute", (double)got.limit);
        } else if (got.ulps_per_argument != 0.0f) {
            snprintf(wanted, sizeof(wanted), "%g plus %g per unit", (double)got.ulps,
                     (double)got.ulps_per_argument);
        } else {
            snprintf(wanted, sizeof(wanted), "%g units in the last place", (double)got.ulps);
        }
        // strncmp and not a search: a reason that mentioned its number somewhere in the middle,
        // after a different one at the front, would satisfy a search and would still read as the
        // wrong budget to anyone looking at the line.
        if (got.reason == NULL || strncmp(got.reason, wanted, strlen(wanted)) != 0) {
            if (unexplained == 0) {
                first_unexplained = FMathName((FMathMode)mode);
            }
            ++unexplained;
        }
    }
    CHECK_TRUE(SUITE, "every float budget's reason begins with the number it is", CHECK_NONE,
               unexplained == 0, "%u budgets did not, first was \"%s\"", (unsigned)unexplained,
               first_unexplained);

    // And every reason has to fit the note that carries it, measured on the very string the
    // float suite emits: one cut off mid-word leaves the reader half a sentence and no sign that
    // there was more.
    uint32_t too_long = 0;
    const char *first_too_long = "";
    int longest = 0;
    for (uint32_t mode = 0; mode < FMATH_MODE_COUNT; ++mode) {
        const FMathBudget got = FMathBudgetFor((FMathMode)mode);
        const int needed = snprintf(NULL, 0, FMATH_BUDGET_NOTE_FORMAT,
                                    FMathName((FMathMode)mode), got.reason);
        if (needed > longest) {
            longest = needed;
        }
        if (needed > REPORT_NOTE_MAX) {
            if (too_long == 0) {
                first_too_long = FMathName((FMathMode)mode);
            }
            ++too_long;
        }
    }
    CHECK_TRUE(SUITE, "every float budget's reason fits the note that carries it", CHECK_NONE,
               too_long == 0,
               "%u would be cut short, first was \"%s\"; the longest needs %d characters and a "
               "note carries %d",
               (unsigned)too_long, first_too_long, longest, (int)REPORT_NOTE_MAX);

    // The power of two is the one budget that is not a constant, so the allowance it hands out
    // has to move with the argument rather than sit at whatever the middle of the sweep gets.
    const float at_middle = FMathAllowedUlps(FMATH_EXP2, 31, 0);
    const float at_end = FMathAllowedUlps(FMATH_EXP2, 63, 0);
    CHECK_TRUE(SUITE, "the power of two's allowance grows with its argument", CHECK_NONE,
               at_end > at_middle && at_end == 11.5f,
               "the allowance was %g in the middle of the sweep and %g at the end, expected "
               "11.5 at the end",
               (double)at_middle, (double)at_end);

    // The libm answers, written down. Each is the correctly rounded single precision value,
    // computed away from any C library, so a runtime that drifts has to be wrong by more than
    // half a unit to move one. Otherwise it would move the expected value on one machine only,
    // and every float case would report a difference that was never in the graphics unit.
    static const struct {
        FMathMode mode;
        uint32_t x;
        uint32_t expected;
    } LIBM_BY_HAND[] = {
        // the square root of 0.015625, 0.375 and 1
        { FMATH_SQUARE_ROOT, 0, 0x3E000000u },
        { FMATH_SQUARE_ROOT, 23, 0x3F1CC471u },
        { FMATH_SQUARE_ROOT, 63, 0x3F800000u },
        // two to the power of -3.875, -1 and 4
        { FMATH_EXP2, 0, 0x3D8B95C2u },
        { FMATH_EXP2, 23, 0x3F000000u },
        { FMATH_EXP2, 63, 0x41800000u },
        // the logarithm to base two of operands from two to the -9 to just under a half, each
        // with a full mantissa
        { FMATH_LOG2, 0, 0xC103ABF5u },
        { FMATH_LOG2, 23, 0xBF889A7Eu },
        { FMATH_LOG2, 63, 0xBFD1D250u },
        // the sine of -2.90625, -0.75 and 3
        { FMATH_SINE, 0, 0xBE6EC5BEu },
        { FMATH_SINE, 23, 0xBF2E7FE1u },
        { FMATH_SINE, 63, 0x3E1081C3u },
        // the cosine of the same three angles
        { FMATH_COSINE, 0, 0xBF78F178u },
        { FMATH_COSINE, 23, 0x3F3B4FF6u },
        { FMATH_COSINE, 63, 0xBF7D7026u },
    };
    uint32_t libm_wrong = 0;
    const char *first_libm = "";
    uint32_t libm_got = 0;
    uint32_t libm_want = 0;
    for (size_t i = 0; i < sizeof(LIBM_BY_HAND) / sizeof(LIBM_BY_HAND[0]); ++i) {
        const float value =
            FMathReference(LIBM_BY_HAND[i].mode, LIBM_BY_HAND[i].x, 0, 0.0f);
        uint32_t pattern;
        memcpy(&pattern, &value, sizeof(pattern));
        if (pattern != LIBM_BY_HAND[i].expected) {
            if (libm_wrong == 0) {
                first_libm = FMathName(LIBM_BY_HAND[i].mode);
                libm_got = pattern;
                libm_want = LIBM_BY_HAND[i].expected;
            }
            ++libm_wrong;
        }
    }
    CHECK_TRUE(SUITE, "the runtime's transcendentals give the answers written down here",
               CHECK_NONE, libm_wrong == 0,
               "%u of %u differed, first was \"%s\" at 0x%08x, expected 0x%08x",
               (unsigned)libm_wrong,
               (unsigned)(sizeof(LIBM_BY_HAND) / sizeof(LIBM_BY_HAND[0])), first_libm,
               (unsigned)libm_got, (unsigned)libm_want);
}

static int CompareWords(const void *a, const void *b) {
    const uint32_t x = *(const uint32_t *)a;
    const uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static CohImage model_target;
static CohImage model_source;
static CohImage model_copy;

/// The image model the coherence suite builds every expectation from: a rectangle filled, a
/// rectangle moved, the cell grid painted, a region sampled. Hand worked texels at the edges of
/// each rectangle, everything outside untouched, and a refusal for anything not wholly inside.
static void CheckCoherenceModel(void) {
    // A 5 by 3 rectangle from (2,7) of a pattern moved to (40,50) of a sentinel image.
    CohImageInit(&model_source, 32, 32);
    CohFillPattern(&model_source, 9);
    CohImageInit(&model_target, 64, 64);
    CohFill(&model_target, COH_SENTINEL);
    const bool moved =
        CohMoveRect(&model_target, 40, 50, &model_source, 2, 7, 5, 3, COH_MOVE_BYTES);
    uint32_t misplaced = 0;
    for (uint32_t y = 0; y < 64u; ++y) {
        for (uint32_t x = 0; x < 64u; ++x) {
            const bool inside = x >= 40u && x < 45u && y >= 50u && y < 53u;
            uint8_t want[4];
            if (inside) {
                CohPattern(9, x - 40u + 2u, y - 50u + 7u, want);
            } else {
                memcpy(want, COH_SENTINEL, 4);
            }
            misplaced += memcmp(CohTexelAt(&model_target, x, y), want, 4) != 0 ? 1u : 0u;
        }
    }
    // Two corners by hand: (40,50) holds source (2,7), (44,52) holds source (6,9).
    uint8_t corner_first[4];
    uint8_t corner_last[4];
    CohPattern(9, 2, 7, corner_first);
    CohPattern(9, 6, 9, corner_last);
    const bool corners = memcmp(CohTexelAt(&model_target, 40, 50), corner_first, 4) == 0 &&
                         memcmp(CohTexelAt(&model_target, 44, 52), corner_last, 4) == 0 &&
                         memcmp(CohTexelAt(&model_target, 45, 52), COH_SENTINEL, 4) == 0 &&
                         memcmp(CohTexelAt(&model_target, 44, 53), COH_SENTINEL, 4) == 0;
    CHECK_TRUE(SUITE, "a moved rectangle lands at its offset and nothing else changes",
               CHECK_NONE, moved && misplaced == 0 && corners,
               "move %s, %u texels wrong, corners %s", moved ? "accepted" : "refused",
               (unsigned)misplaced, corners ? "right" : "wrong");

    // A move within one image reads the source row before writing it.
    CohImageInit(&model_copy, 8, 1);
    for (uint32_t x = 0; x < 8u; ++x) {
        const uint8_t texel[4] = { (uint8_t)x, 0, 0, 1 };
        memcpy(CohTexelAt(&model_copy, x, 0), texel, 4);
    }
    const bool overlapped = CohMoveRect(&model_copy, 2, 0, &model_copy, 0, 0, 6, 1,
                                        COH_MOVE_BYTES);
    static const uint8_t SHIFTED[8] = { 0, 1, 0, 1, 2, 3, 4, 5 };
    uint32_t shifted_wrong = 0;
    for (uint32_t x = 0; x < 8u; ++x) {
        shifted_wrong += CohTexelAt(&model_copy, x, 0)[0] != SHIFTED[x] ? 1u : 0u;
    }
    CHECK_TRUE(SUITE, "a rectangle moved over itself reads each source texel before it goes",
               CHECK_NONE, overlapped && shifted_wrong == 0, "move %s, %u of 8 texels wrong",
               overlapped ? "accepted" : "refused", (unsigned)shifted_wrong);

    // And down its columns: rows 0 and 1 of a 1 by 3 image holding 10, 11, 12 moved down one
    // row give 10, 10, 11, where walking top down would give 10, 10, 10. Up one row gives 11,
    // 12, 12.
    static const uint8_t DOWN[3] = { 10, 10, 11 };
    static const uint8_t UP[3] = { 11, 12, 12 };
    uint32_t vertical_wrong = 0;
    bool vertical_accepted = true;
    for (int direction = 0; direction < 2; ++direction) {
        CohImageInit(&model_copy, 1, 3);
        for (uint32_t y = 0; y < 3u; ++y) {
            const uint8_t texel[4] = { (uint8_t)(10u + y), 0, 0, 1 };
            memcpy(CohTexelAt(&model_copy, 0, y), texel, 4);
        }
        vertical_accepted =
            vertical_accepted && (direction == 0 ? CohMoveRect(&model_copy, 0, 1, &model_copy, 0,
                                                               0, 1, 2, COH_MOVE_BYTES)
                                                 : CohMoveRect(&model_copy, 0, 0, &model_copy, 0,
                                                               1, 1, 2, COH_MOVE_BYTES));
        const uint8_t *const want = direction == 0 ? DOWN : UP;
        for (uint32_t y = 0; y < 3u; ++y) {
            vertical_wrong += CohTexelAt(&model_copy, 0, y)[0] != want[y] ? 1u : 0u;
        }
    }
    CHECK_TRUE(SUITE, "a rectangle moved down or up over itself keeps every source row",
               CHECK_NONE, vertical_accepted && vertical_wrong == 0,
               "moves %s, %u of 6 texels wrong", vertical_accepted ? "accepted" : "refused",
               (unsigned)vertical_wrong);

    const bool aliased =
        CohMoveRect(&model_target, 0, 0, &model_source, 31, 31, 1, 1, COH_MOVE_ALIASED);
    uint8_t want_aliased[4];
    CohAliasedTexel(CohTexelAt(&model_source, 31, 31), want_aliased);
    CHECK_TRUE(SUITE, "an aliased move writes what the alias shader writes", CHECK_NONE,
               aliased && memcmp(CohTexelAt(&model_target, 0, 0), want_aliased, 4) == 0,
               "move %s", aliased ? "accepted but wrote the wrong bytes" : "refused");

    // Cells 5 and 10 of a 32 by 32 image are the 8 by 8 squares at (8,8) and (16,16).
    CohImageInit(&model_copy, 32, 32);
    CohFill(&model_copy, COH_SENTINEL);
    const bool painted = CohPaintCells(&model_copy, 4, (1u << 5) | (1u << 10));
    uint32_t cells_wrong = 0;
    for (uint32_t y = 0; y < 32u; ++y) {
        for (uint32_t x = 0; x < 32u; ++x) {
            const uint32_t cell = (y / 8u) * 4u + x / 8u;
            uint8_t want[4];
            if (cell == 5u || cell == 10u) {
                CohPattern(4, x, y, want);
            } else {
                memcpy(want, COH_SENTINEL, 4);
            }
            cells_wrong += memcmp(CohTexelAt(&model_copy, x, y), want, 4) != 0 ? 1u : 0u;
        }
    }
    CohImageInit(&model_target, 30, 32);
    const bool uneven_refused = !CohPaintCells(&model_target, 4, 0xFFFFu);
    CHECK_TRUE(SUITE, "painting cells covers exactly the cells in the mask", CHECK_NONE,
               painted && cells_wrong == 0 && uneven_refused,
               "paint %s, %u texels wrong, a width that does not divide by four %s",
               painted ? "accepted" : "refused", (unsigned)cells_wrong,
               uneven_refused ? "refused" : "accepted");

    // One texel per pixel: sampling a region is moving the whole texture to its corner.
    CohImageInit(&model_target, 64, 64);
    CohFill(&model_target, COH_SENTINEL);
    model_copy = model_target;
    const CohRect region = { 32, 16, 32, 32 };
    const bool sampled = CohSampleRegion(&model_target, &region, &model_source, COH_MOVE_BYTES);
    CohMoveRect(&model_copy, 32, 16, &model_source, 0, 0, 32, 32, COH_MOVE_BYTES);
    const CohRect overhanging = { 40, 40, 32, 32 };
    const bool overhang_refused =
        !CohSampleRegion(&model_target, &overhanging, &model_source, COH_MOVE_BYTES);
    CHECK_TRUE(SUITE, "sampling a region one texel per pixel moves the texture to its corner",
               CHECK_NONE,
               sampled && overhang_refused &&
                   memcmp(model_target.texels, model_copy.texels, sizeof(model_copy.texels)) ==
                       0,
               "sample %s, the overhanging region %s, and the two images %s",
               sampled ? "accepted" : "refused", overhang_refused ? "refused" : "accepted",
               memcmp(model_target.texels, model_copy.texels, sizeof(model_copy.texels)) == 0
                   ? "agree"
                   : "differ");

    // Four quadrants of one base colour with green set per quadrant, and the images one, two
    // and three draws behind: corners worked by hand at every lag, the far corner of each
    // quadrant included. A quadrant with no draw that far back keeps the first green.
    static const uint8_t BASE[4] = { 0x40, 0x00, 0xA0, 0xFF };
    static const uint8_t GREENS[4] = { 0x11, 0x55, 0x99, 0xDD };
    static const struct {
        uint32_t x;
        uint32_t y;
        uint8_t green[4]; ///< by lag
    } QUADRANT_CORNERS[] = {
        { 0, 0, { 0x11, 0x11, 0x11, 0x11 } },   { 31, 31, { 0x11, 0x11, 0x11, 0x11 } },
        { 32, 0, { 0x55, 0x11, 0x11, 0x11 } },  { 63, 31, { 0x55, 0x11, 0x11, 0x11 } },
        { 0, 32, { 0x99, 0x55, 0x11, 0x11 } },  { 31, 63, { 0x99, 0x55, 0x11, 0x11 } },
        { 32, 32, { 0xDD, 0x99, 0x55, 0x11 } }, { 63, 63, { 0xDD, 0x99, 0x55, 0x11 } },
    };
    bool laid = true;
    uint32_t quadrant_wrong = 0;
    for (uint32_t lag = 0; lag < 4u; ++lag) {
        CohImageInit(&model_target, 64, 64);
        laid = laid && CohQuadrantColours(&model_target, BASE, 1, GREENS, lag);
        for (size_t i = 0; i < sizeof(QUADRANT_CORNERS) / sizeof(QUADRANT_CORNERS[0]); ++i) {
            const uint8_t *const texel =
                CohTexelAt(&model_target, QUADRANT_CORNERS[i].x, QUADRANT_CORNERS[i].y);
            if (texel[0] != 0x40 || texel[1] != QUADRANT_CORNERS[i].green[lag] ||
                texel[2] != 0xA0 || texel[3] != 0xFF) {
                ++quadrant_wrong;
            }
        }
    }
    CohImageInit(&model_source, 32, 32);
    const bool quadrant_refused = !CohQuadrantColours(&model_source, BASE, 1, GREENS, 0) &&
                                  !CohQuadrantColours(&model_target, BASE, 4, GREENS, 0);
    CohFillPattern(&model_source, 9);
    CHECK_TRUE(SUITE, "four quadrants take their own green, and each lag the green that far back",
               CHECK_NONE, laid && quadrant_wrong == 0 && quadrant_refused,
               "laid %s, %u of 32 hand worked corners wrong, a 32 by 32 image or a fifth "
               "channel %s",
               laid ? "out" : "refused", (unsigned)quadrant_wrong,
               quadrant_refused ? "refused" : "accepted");

    // One texel past the edge on either side, a size large enough to wrap an addition, and a
    // fill hanging off the bottom: every one refused and nothing written.
    CohFill(&model_target, COH_SENTINEL);
    const bool refused =
        !CohMoveRect(&model_target, 60, 0, &model_source, 0, 0, 5, 1, COH_MOVE_BYTES) &&
        !CohMoveRect(&model_target, 0, 0, &model_source, 28, 0, 5, 1, COH_MOVE_BYTES) &&
        !CohMoveRect(&model_target, 8, 0, &model_source, 0, 0, 0xFFFFFFF8u, 1,
                     COH_MOVE_BYTES) &&
        !CohMoveRect(&model_target, 0, 0, &model_source, 0, 0, 0, 1, COH_MOVE_BYTES) &&
        !CohFillRect(&model_target, 0, 62, 1, 3, COH_SENTINEL) &&
        !CohImageInit(&model_copy, 65, 1);
    uint32_t touched = 0;
    for (uint32_t i = 0; i < 64u * 64u; ++i) {
        touched += memcmp(model_target.texels[i], COH_SENTINEL, 4) != 0 ? 1u : 0u;
    }
    CHECK_TRUE(SUITE, "a rectangle not wholly inside is refused and changes nothing",
               CHECK_NONE, refused && touched == 0, "%s, %u texels changed",
               refused ? "all refused" : "one was accepted", (unsigned)touched);
}

/// The coherence suite's rules: the byte order two formats agree on, where the block linear
/// layout puts a byte, which texel a region's pixel samples, and that its patterns can be told
/// apart. Every value by hand, then the sweeps the suite leans on.
static void CheckCoherenceRules(void) {
    static const struct {
        uint8_t rgba[4];
        uint32_t word;
        uint8_t expected[4];
    } ALIAS[] = {
        // 0x04030201 rotated right by four is 0x10403020.
        { { 0x01, 0x02, 0x03, 0x04 }, 0x04030201u, { 0x20, 0x30, 0x40, 0x10 } },
        // 0x0000FFFF becomes 0xF0000FFF: the low nibble of red ends up at the top of alpha.
        { { 0xFF, 0xFF, 0x00, 0x00 }, 0x0000FFFFu, { 0xFF, 0x0F, 0x00, 0xF0 } },
        // 0x12EFCDAB becomes 0xB12EFCDA.
        { { 0xAB, 0xCD, 0xEF, 0x12 }, 0x12EFCDABu, { 0xDA, 0xFC, 0x2E, 0xB1 } },
        // 0xFE000080 becomes 0x0FE00008.
        { { 0x80, 0x00, 0x00, 0xFE }, 0xFE000080u, { 0x08, 0x00, 0xE0, 0x0F } },
    };
    uint32_t alias_wrong = 0;
    for (size_t i = 0; i < sizeof(ALIAS) / sizeof(ALIAS[0]); ++i) {
        uint8_t got[4];
        CohAliasedTexel(ALIAS[i].rgba, got);
        if (CohAliasWord(ALIAS[i].rgba) != ALIAS[i].word ||
            memcmp(got, ALIAS[i].expected, 4) != 0) {
            ++alias_wrong;
        }
    }
    uint32_t round_trip_wrong = 0;
    for (uint32_t i = 0; i < 4096u; ++i) {
        const uint32_t word = i * 0x9E3779B9u;
        uint8_t bytes[4];
        CohAliasBytes(word, bytes);
        if (CohAliasWord(bytes) != word || bytes[0] != (word & 0xFFu)) {
            ++round_trip_wrong;
        }
    }
    CHECK_TRUE(SUITE,
               "an RGBA8 texel read as R32_Uint and rotated is its word least significant first",
               CHECK_NONE, alias_wrong == 0 && round_trip_wrong == 0,
               "%u of %u hand worked texels and %u of 4096 round trips disagreed",
               (unsigned)alias_wrong, (unsigned)(sizeof(ALIAS) / sizeof(ALIAS[0])),
               (unsigned)round_trip_wrong);

    // Hand worked from the gob's shape: 16 byte runs, row pairs 64 apart, 32 byte halves 256
    // apart, gobs 512 apart down a block.
    static const struct {
        uint32_t x_bytes;
        uint32_t y;
        uint32_t width_bytes;
        uint32_t block_height_gobs;
        uint32_t offset;
    } GOB[] = {
        { 0, 0, 64, 1, 0 },       { 5, 0, 64, 1, 5 },     { 16, 0, 64, 1, 32 },
        { 0, 1, 64, 1, 16 },      { 0, 2, 64, 1, 64 },    { 32, 0, 64, 1, 256 },
        { 63, 7, 64, 1, 511 },    { 20, 3, 64, 1, 116 },  { 0, 8, 64, 1, 512 },
        { 64, 0, 128, 1, 512 },   { 0, 8, 128, 1, 1024 }, { 0, 8, 128, 2, 512 },
        { 64, 0, 128, 2, 1024 },  { 64, 17, 128, 2, 3088 },
    };
    uint32_t gob_wrong = 0;
    uint32_t first_gob = 0;
    for (size_t i = 0; i < sizeof(GOB) / sizeof(GOB[0]); ++i) {
        if (CohBlockLinearOffset(GOB[i].x_bytes, GOB[i].y, GOB[i].width_bytes,
                                 GOB[i].block_height_gobs) != GOB[i].offset) {
            if (gob_wrong == 0) {
                first_gob = (uint32_t)i;
            }
            ++gob_wrong;
        }
    }
    CHECK_TRUE(SUITE, "the block linear rule puts hand worked bytes where a gob keeps them",
               CHECK_NONE, gob_wrong == 0,
               "%u of %u disagreed, first was byte %u of row %u, giving %u, expected %u",
               (unsigned)gob_wrong, (unsigned)(sizeof(GOB) / sizeof(GOB[0])),
               (unsigned)GOB[first_gob].x_bytes, (unsigned)GOB[first_gob].y,
               (unsigned)CohBlockLinearOffset(GOB[first_gob].x_bytes, GOB[first_gob].y,
                                              GOB[first_gob].width_bytes,
                                              GOB[first_gob].block_height_gobs),
               (unsigned)GOB[first_gob].offset);

    // The texture the suite writes by hand: 32 by 32 texels of four bytes in one gob blocks.
    // Every texel needs its own four bytes, and together they fill the eight gobs exactly.
    static uint8_t claimed[4096];
    memset(claimed, 0, sizeof(claimed));
    uint32_t collisions = 0;
    uint32_t outside = 0;
    for (uint32_t y = 0; y < 32u; ++y) {
        for (uint32_t x = 0; x < 32u; ++x) {
            const uint32_t offset = CohBlockLinearOffset(x * 4u, y, 128u, 1u);
            if (offset % 4u != 0u || offset + 4u > sizeof(claimed)) {
                ++outside;
                continue;
            }
            for (uint32_t b = 0; b < 4u; ++b) {
                collisions += claimed[offset + b]++ != 0u ? 1u : 0u;
            }
        }
    }
    CHECK_TRUE(SUITE, "the block linear rule gives each texel of the suite's texture its own "
                      "four bytes",
               CHECK_NONE, collisions == 0 && outside == 0,
               "%u bytes claimed twice and %u texels outside the eight gobs or off a word",
               (unsigned)collisions, (unsigned)outside);

    const CohRect region = { 32, 16, 32, 32 };
    uint32_t tx = 99;
    uint32_t ty = 99;
    const bool corner = CohTexelUnder(&region, 32, 32, 32, 16, &tx, &ty) && tx == 0 && ty == 0;
    const bool far = CohTexelUnder(&region, 32, 32, 63, 47, &tx, &ty) && tx == 31 && ty == 31;
    const bool middle = CohTexelUnder(&region, 32, 32, 45, 30, &tx, &ty) && tx == 13 && ty == 14;
    const bool left_out = !CohTexelUnder(&region, 32, 32, 31, 20, &tx, &ty);
    const bool below_out = !CohTexelUnder(&region, 32, 32, 40, 48, &tx, &ty);
    const double closest = CohClosestTexelEdge(&region, 32, 32);
    CHECK_TRUE(SUITE, "a region's pixels sample the texel whose centre they sit on", CHECK_NONE,
               corner && far && middle && left_out && below_out && closest == 0.5,
               "corner %d, far corner %d, middle %d, left outside %d, below outside %d, "
               "closest centre %.6f of a texel from an edge",
               corner, far, middle, left_out, below_out, closest);

    CheckCoherenceModel();

    // Worked by hand from the four sums in coherref.h, the last one past 255 in three channels.
    uint8_t texel[4];
    CohPattern(1, 0, 0, texel);
    const bool origin = texel[0] == 0x01 && texel[1] == 0x03 && texel[2] == 0x05 &&
                        texel[3] == 0x07;
    CohPattern(1, 31, 31, texel);
    const bool far_corner = texel[0] == 0x7D && texel[1] == 0x7F && texel[2] == 0x05 &&
                        texel[3] == 0x45;
    CohPattern(60, 31, 17, texel);
    const bool wrapped = texel[0] == 0xB8 && texel[1] == 0xF8 && texel[2] == 0x64 &&
                         texel[3] == 0xD4;
    CHECK_TRUE(SUITE, "the coherence pattern is the four sums, modulo 256", CHECK_NONE,
               origin && far_corner && wrapped,
               "seed 1 at (0,0) %s, seed 1 at (31,31) %s, seed 60 at (31,17) %s",
               origin ? "right" : "wrong", far_corner ? "right" : "wrong",
               wrapped ? "right" : "wrong");

    // Over every seed the suite uses and the largest image it draws, 64 by 64: no texel is the
    // sentinel, no two texels of one pattern agree, and no two patterns agree at any texel, so a
    // stale image, a shifted one or an untouched target can never pass for the right one. Each
    // pattern sorted rather than compared pair by pair, since this also runs on the console.
    static uint32_t words[64u * 64u];
    uint32_t sentinel_hits = 0;
    uint32_t repeats = 0;
    uint32_t shared = 0;
    for (uint32_t seed = COH_SEED_RENDERED_START; seed < COH_SEED_END; ++seed) {
        for (uint32_t i = 0; i < 64u * 64u; ++i) {
            CohPattern(seed, i % 64u, i / 64u, texel);
            sentinel_hits += memcmp(texel, COH_SENTINEL, 4) == 0 ? 1u : 0u;
            words[i] = CohAliasWord(texel);
            for (uint32_t other = seed + 1u; other < COH_SEED_END; ++other) {
                uint8_t theirs[4];
                CohPattern(other, i % 64u, i / 64u, theirs);
                shared += memcmp(texel, theirs, 4) == 0 ? 1u : 0u;
            }
        }
        qsort(words, 64u * 64u, sizeof(words[0]), CompareWords);
        for (uint32_t i = 1; i < 64u * 64u; ++i) {
            repeats += words[i] == words[i - 1u] ? 1u : 0u;
        }
    }
    CHECK_TRUE(SUITE, "no coherence pattern texel is the sentinel", CHECK_NONE,
               sentinel_hits == 0, "%u texels equal the sentinel", (unsigned)sentinel_hits);
    CHECK_TRUE(SUITE, "no two texels of one coherence pattern are the same", CHECK_NONE,
               repeats == 0, "%u pairs repeat", (unsigned)repeats);
    CHECK_TRUE(SUITE, "no two coherence patterns agree at any texel", CHECK_NONE, shared == 0,
               "%u texels agree between two seeds", (unsigned)shared);
}


/// The integer modes added after the first fourteen: each one worked out at two pixels away from
/// this program's own code, and each rule underneath checked over a sweep against a second
/// algorithm that shares no line with the one in source/intref.c.
static void CheckIntegerArithmeticGrown(void) {
    // Worked out with exact rationals, away from this program's own code: (37,23)
    // has the mix 0x71A39D6A, top bit clear; (2,37) has 0xBC3BF8D5, top bit set, a negative
    // dividend over a negative divisor and a shift count of 33.
    static const struct {
        IntMathMode mode;
        uint32_t x;
        uint32_t y;
        uint32_t expected;
    } BY_HAND[] = {
        // 1906548074 needs 31 bits, so the float keeps 24 of them: 0x71A39D6A rounds up to
        // 0x71A39D80, which is 1.7755 times two to the 30.
        { INTMATH_INT_TO_FLOAT, 37, 23, 0x4EE3473Bu },
        { INTMATH_UINT_TO_FLOAT, 37, 23, 0x4EE3473Bu },
        // (37 - 31.5) * 3.25 + 23 / 8 is 20.75, towards nought 20.
        { INTMATH_FLOAT_TO_INT, 37, 23, 0x00000014u },
        { INTMATH_FLOAT_TO_UINT, 37, 23, 0x71A39D00u },
        // -29/256 clamps to nought; 45, 119 and 193 256ths times 255 are 44.82, 118.54 and
        // 192.25, so 0x2D, 0x77 and 0xC0 above the nought in the low byte.
        { INTMATH_PACK_UNORM4X8, 37, 23, 0xC0772D00u },
        { INTMATH_ROUNDTRIP_UNORM4X8, 37, 23, 0x71A39D6Au },
        // -309/256 clamps to minus one, which packs as -32767, 0x8001; -107/256 times 32767 is
        // -13695.57, and the nearest code is -13696, 0xCA80.
        { INTMATH_PACK_SNORM2X16, 37, 23, 0xCA808001u },
        { INTMATH_ROUNDTRIP_SNORM2X16, 37, 23, 0x71A39D6Au },
        { INTMATH_PACK_HALF2X16, 37, 23, 0x71A39D6Au },
        // 0x9D6A is -0.00528717041015625 and 0x71A3 is 11544: 0xBBAD4000 ^ 0x46346000.
        { INTMATH_UNPACK_HALF2X16, 37, 23, 0xFD992000u },
        // 37 % 3 is 1, so a count of 32: everything shifted out.
        { INTMATH_SHIFT_LEFT_WIDE, 37, 23, 0x00000000u },
        { INTMATH_SHIFT_RIGHT_WIDE, 37, 23, 0x00000000u },
        { INTMATH_SHIFT_ARITHMETIC_WIDE, 37, 23, 0x00000000u },
        { INTMATH_SHIFT_LEFT_BY_REGISTER, 37, 23, 0xB5000000u },
        { INTMATH_SHIFT_RIGHT_BY_REGISTER, 37, 23, 0x000000E3u },
        { INTMATH_SHIFT_ARITHMETIC_BY_REGISTER, 37, 23, 0x000000E3u },
        // 32211 over -854 is -37.72, towards nought -37, and 32211 - 37 * 854 is 613.
        { INTMATH_SIGNED_DIVIDE, 37, 23, 0xFFFFFFDBu },
        { INTMATH_SIGNED_REMAINDER, 37, 23, 0x00000265u },
        { INTMATH_FIND_MSB_UINT, 37, 23, 0x00000019u },
        { INTMATH_FIND_MSB_INT, 37, 23, 0x00000019u },
        { INTMATH_FIND_LSB, 37, 23, 0x00000006u },
        // Offset 5, width 23.
        { INTMATH_BITFIELD_EXTRACT_UINT, 37, 23, 0x000D1CEBu },
        { INTMATH_BITFIELD_EXTRACT_INT, 37, 23, 0x000D1CEBu },
        { INTMATH_BITFIELD_INSERT, 37, 23, 0x7A2AFCCAu },
        { INTMATH_BITFIELD_REVERSE, 37, 23, 0x56B9C58Eu },
        { INTMATH_MIN_SIGNED, 37, 23, 0xBE5157E6u },
        { INTMATH_MAX_UNSIGNED, 37, 23, 0xBE5157E6u },
        { INTMATH_CLAMP_SIGNED, 37, 23, 0x20000000u },
        { INTMATH_CLAMP_UNSIGNED, 37, 23, 0x71A39D6Au },
        // -1136920363 as a float rounds to -1136920320.
        { INTMATH_INT_TO_FLOAT, 2, 37, 0xCE87880Eu },
        { INTMATH_UINT_TO_FLOAT, 2, 37, 0x4F3C3BF9u },
        // (2 - 31.5) * 3.25 + 37 / 8 is -91.25, towards nought -91.
        { INTMATH_FLOAT_TO_INT, 2, 37, 0xFFFFFFA5u },
        { INTMATH_FLOAT_TO_UINT, 2, 37, 0xBC3BF800u },
        { INTMATH_PACK_UNORM4X8, 2, 37, 0x00FFF8AEu },
        { INTMATH_ROUNDTRIP_UNORM4X8, 2, 37, 0xBC3BF8D5u },
        { INTMATH_PACK_SNORM2X16, 2, 37, 0x8001667Fu },
        { INTMATH_ROUNDTRIP_SNORM2X16, 2, 37, 0xBC3BF8D5u },
        { INTMATH_PACK_HALF2X16, 2, 37, 0xBC3BF8D5u },
        { INTMATH_UNPACK_HALF2X16, 2, 37, 0x789DC000u },
        // A count of 33: nought, nought, and the sign all the way down.
        { INTMATH_SHIFT_LEFT_WIDE, 2, 37, 0x00000000u },
        { INTMATH_SHIFT_RIGHT_WIDE, 2, 37, 0x00000000u },
        { INTMATH_SHIFT_ARITHMETIC_WIDE, 2, 37, 0xFFFFFFFFu },
        { INTMATH_SHIFT_LEFT_BY_REGISTER, 2, 37, 0x00000000u },
        { INTMATH_SHIFT_RIGHT_BY_REGISTER, 2, 37, 0x00000000u },
        { INTMATH_SHIFT_ARITHMETIC_BY_REGISTER, 2, 37, 0xFFFFFFFFu },
        // -3033 over -1372 is 2.21, so 2, and the remainder keeps the dividend's sign: -289.
        { INTMATH_SIGNED_DIVIDE, 2, 37, 0x00000002u },
        { INTMATH_SIGNED_REMAINDER, 2, 37, 0xFFFFFEDFu },
        { INTMATH_FIND_MSB_UINT, 2, 37, 0x0000001Du },
        // Negative, so the highest bit that is nought: bit 28 of 0xEF0EFE35.
        { INTMATH_FIND_MSB_INT, 2, 37, 0x0000001Cu },
        { INTMATH_FIND_LSB, 2, 37, 0x00000002u },
        { INTMATH_BITFIELD_EXTRACT_UINT, 2, 37, 0x00000005u },
        { INTMATH_BITFIELD_EXTRACT_INT, 2, 37, 0x00000005u },
        { INTMATH_BITFIELD_INSERT, 2, 37, 0xBC3BF8FDu },
        { INTMATH_BITFIELD_REVERSE, 2, 37, 0xAB1FDC3Du },
        { INTMATH_MIN_SIGNED, 2, 37, 0xBC3BF8D5u },
        { INTMATH_MAX_UNSIGNED, 2, 37, 0xBC3BF8D5u },
        { INTMATH_CLAMP_SIGNED, 2, 37, 0xF0000000u },
        { INTMATH_CLAMP_UNSIGNED, 2, 37, 0xBC3BF8D5u },
    };
    uint32_t wrong = 0;
    uint32_t covered = 0;
    size_t first = 0;
    uint32_t first_got = 0;
    for (size_t i = 0; i < sizeof(BY_HAND) / sizeof(BY_HAND[0]); ++i) {
        covered |= 1u << (BY_HAND[i].mode - INTMATH_INT_TO_FLOAT);
        const uint32_t got = IntMathReference(BY_HAND[i].mode, BY_HAND[i].x, BY_HAND[i].y, 0u);
        if (got != BY_HAND[i].expected) {
            if (wrong == 0) {
                first = i;
                first_got = got;
            }
            ++wrong;
        }
    }
    const uint32_t grown = INTMATH_MODE_COUNT - INTMATH_INT_TO_FLOAT;
    CHECK_TRUE(SUITE, "the grown integer reference matches the values worked out by hand",
               CHECK_NONE, wrong == 0 && covered == (1u << grown) - 1u,
               "%u of %u disagreed, first \"%s\" at (%u,%u) gave 0x%08x, expected 0x%08x; "
               "modes covered 0x%08x",
               (unsigned)wrong, (unsigned)(sizeof(BY_HAND) / sizeof(BY_HAND[0])),
               IntMathName(BY_HAND[first].mode), (unsigned)BY_HAND[first].x,
               (unsigned)BY_HAND[first].y, (unsigned)first_got,
               (unsigned)BY_HAND[first].expected, (unsigned)covered);

    // Sweeps over every pixel the suite draws, each against a second way of saying the rule.
    uint32_t conversion = 0;
    uint32_t unorm_margin = 0;
    uint32_t snorm_margin = 0;
    uint32_t shifts = 0;
    uint32_t division = 0;
    uint32_t scans = 0;
    uint32_t fields = 0;
    uint32_t sign_combinations = 0;
    uint32_t wide_counts = 0;
    uint32_t extract_negative = 0;
    for (uint32_t y = 0; y < 64u; ++y) {
        for (uint32_t x = 0; x < 64u; ++x) {
            const uint32_t v = IntMathMix(x, y);

            // The processor's own conversions, which round to nearest even by default: a
            // different algorithm from the one written out with integers.
            const float as_signed = (float)(int32_t)v;
            const float as_unsigned = (float)v;
            uint32_t bits_signed;
            uint32_t bits_unsigned;
            memcpy(&bits_signed, &as_signed, sizeof(bits_signed));
            memcpy(&bits_unsigned, &as_unsigned, sizeof(bits_unsigned));
            if (IntMathReference(INTMATH_INT_TO_FLOAT, x, y, 0u) != bits_signed ||
                IntMathReference(INTMATH_UINT_TO_FLOAT, x, y, 0u) != bits_unsigned ||
                IntMathReference(INTMATH_FLOAT_TO_INT, x, y, 0u) !=
                    (uint32_t)(int32_t)IntMathSmallFloat(x, y)) {
                ++conversion;
            }

            // Every packed component at least a 256th of a code from a halfway point, which is
            // what makes the language's round have one answer.
            for (uint32_t i = 0; i < 4u; ++i) {
                const double code = (double)IntMathUnormInput(x, y, i) * 255.0;
                const double fraction = code - floor(code);
                if (code > 0.0 && code < 255.0 && fabs(fraction - 0.5) < 1.0 / 256.0) {
                    ++unorm_margin;
                }
            }
            for (uint32_t i = 0; i < 2u; ++i) {
                const double code = (double)IntMathSnormInput(x, y, i) * 32767.0;
                const double fraction = code - floor(code);
                if (fabs(code) < 32767.0 && fabs(fraction - 0.5) < 1.0 / 256.0) {
                    ++snorm_margin;
                }
            }

            // The shifts, with the count clamped: below 32 they are C's, at 32 and above
            // everything is shifted out.
            for (uint32_t count = 0; count < 70u; count += 1u) {
                const uint32_t left = count < 32u ? v << count : 0u;
                const uint32_t right = count < 32u ? v >> count : 0u;
                const uint32_t sign = (v & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u;
                const uint32_t arithmetic =
                    count == 0u ? v : (count < 32u ? (right | (sign << (32u - count))) : sign);
                if (IntMathShiftLeft(v, count) != left || IntMathShiftRight(v, count) != right ||
                    IntMathShiftArithmetic(v, count) != arithmetic) {
                    ++shifts;
                }
            }
            if (31u + x % 3u >= 32u) {
                ++wide_counts;
            }

            // The division's operands keep the float reciprocal honest: under two to the 18,
            // never a multiple, and the quotient a known number of whole divisors.
            const int32_t dividend = IntMathDividend(x, y);
            const int32_t divisor = IntMathDivisor(y);
            const int32_t magnitude = divisor < 0 ? -divisor : divisor;
            const int32_t whole = dividend < 0 ? -dividend : dividend;
            const int32_t sign = (dividend < 0) == (divisor < 0) ? 1 : -1;
            const int32_t remainder = whole - (int32_t)x * magnitude;
            if (whole >= (1 << 18) || remainder < 1 || remainder >= magnitude ||
                (int32_t)IntMathReference(INTMATH_SIGNED_DIVIDE, x, y, 0u) != sign * (int32_t)x ||
                (int32_t)IntMathReference(INTMATH_SIGNED_REMAINDER, x, y, 0u) !=
                    (dividend < 0 ? -remainder : remainder)) {
                ++division;
            }
            sign_combinations |= 1u << ((dividend < 0 ? 2u : 0u) + (divisor < 0 ? 1u : 0u));

            // The scans against the compiler's own count of leading and trailing noughts.
            const uint32_t shifted = v >> (x & 31u);
            const uint32_t msb = shifted == 0u ? 0xFFFFFFFFu : 31u - (uint32_t)__builtin_clz(shifted);
            const uint32_t left_shifted = v << (x & 31u);
            const uint32_t lsb =
                left_shifted == 0u ? 0xFFFFFFFFu : (uint32_t)__builtin_ctz(left_shifted);
            if (IntMathReference(INTMATH_FIND_MSB_UINT, x, y, 0u) != msb ||
                IntMathReference(INTMATH_FIND_LSB, x, y, 0u) != lsb) {
                ++scans;
            }

            // The fields against masks and shifts, where the reference walks bit by bit.
            const uint32_t offset = IntMathFieldOffset(x);
            const uint32_t width = IntMathFieldBits(x, y);
            const uint32_t mask = width >= 32u ? 0xFFFFFFFFu : ((1u << width) - 1u);
            const uint32_t field = (v >> offset) & mask;
            uint32_t extended = field;
            if (width > 0u && width < 32u && ((field >> (width - 1u)) & 1u) != 0u) {
                extended |= ~mask;
                ++extract_negative;
            }
            const uint32_t w = IntMathSecond(x, y);
            const uint32_t inserted = (v & ~(mask << offset)) | ((w & mask) << offset);
            if (offset + width > 32u ||
                IntMathReference(INTMATH_BITFIELD_EXTRACT_UINT, x, y, 0u) != field ||
                IntMathReference(INTMATH_BITFIELD_EXTRACT_INT, x, y, 0u) !=
                    (width == 0u ? 0u : extended) ||
                IntMathReference(INTMATH_BITFIELD_INSERT, x, y, 0u) != inserted ||
                IntMathReverse(IntMathReverse(v)) != v) {
                ++fields;
            }
        }
    }
    CHECK_TRUE(SUITE, "the integer to float conversions agree with the processor's own",
               CHECK_NONE, conversion == 0, "%u of 4096 pixels disagreed", (unsigned)conversion);
    CHECK_TRUE(SUITE, "every packed component is at least a 256th of a code from a half",
               CHECK_NONE, unorm_margin == 0 && snorm_margin == 0,
               "%u unorm and %u snorm components came closer", (unsigned)unorm_margin,
               (unsigned)snorm_margin);
    CHECK_TRUE(SUITE, "the shifts clamp their count to 32 and the sweep reaches past it",
               CHECK_NONE, shifts == 0 && wide_counts > 0,
               "%u of 286720 shifts disagreed; %u pixels had a count of 32 or more",
               (unsigned)shifts, (unsigned)wide_counts);
    CHECK_TRUE(SUITE, "the signed division's operands keep a float reciprocal exact", CHECK_NONE,
               division == 0 && sign_combinations == 0xFu,
               "%u of 4096 pixels broke the construction; sign combinations seen 0x%x of 0xf",
               (unsigned)division, (unsigned)sign_combinations);
    CHECK_TRUE(SUITE, "the bit scans agree with counting leading and trailing noughts",
               CHECK_NONE, scans == 0, "%u of 4096 pixels disagreed", (unsigned)scans);
    CHECK_TRUE(SUITE, "the bitfield modes agree with masks and shifts and see negative fields",
               CHECK_NONE, fields == 0 && extract_negative > 0,
               "%u of 4096 pixels disagreed; %u fields had their top bit set", (unsigned)fields,
               (unsigned)extract_negative);

    // The minimum, maximum and clamps against sixty four bit comparisons, which share nothing
    // with the thirty two bit ones in the reference, and the half precision patterns against the
    // rule they are built by: an exponent of nought gains exactly bit 10, a NaN loses exactly its
    // mantissa, everything else is untouched. Both over every pixel, the ends row included.
    uint32_t ordering = 0;
    uint32_t ends_seen = 0;
    uint32_t patterns = 0;
    uint32_t raised = 0;
    for (uint32_t y = 0; y < 64u; ++y) {
        for (uint32_t x = 0; x < 64u; ++x) {
            uint32_t a;
            uint32_t b;
            IntMathMinMaxOperands(x, y, &a, &b);
            const int64_t sa = (int64_t)(int32_t)a;
            const int64_t sb = (int64_t)(int32_t)b;
            const int64_t signed_min = sa <= sb ? sa : sb;
            const uint64_t unsigned_max = (uint64_t)a >= (uint64_t)b ? a : b;
            int64_t signed_clamp = sa;
            if (signed_clamp < -0x10000000LL) {
                signed_clamp = -0x10000000LL;
            }
            if (signed_clamp > 0x20000000LL) {
                signed_clamp = 0x20000000LL;
            }
            uint64_t unsigned_clamp = a;
            if (unsigned_clamp < 0x40000000ull) {
                unsigned_clamp = 0x40000000ull;
            }
            if (unsigned_clamp > 0xC0000000ull) {
                unsigned_clamp = 0xC0000000ull;
            }
            if (IntMathReference(INTMATH_MIN_SIGNED, x, y, 0u) != (uint32_t)signed_min ||
                IntMathReference(INTMATH_MAX_UNSIGNED, x, y, 0u) != (uint32_t)unsigned_max ||
                IntMathReference(INTMATH_CLAMP_SIGNED, x, y, 0u) != (uint32_t)signed_clamp ||
                IntMathReference(INTMATH_CLAMP_UNSIGNED, x, y, 0u) != (uint32_t)unsigned_clamp) {
                ++ordering;
            }
            if (a == 0x80000000u && b == 0x7FFFFFFFu) {
                ++ends_seen;
            }

            const uint32_t v = IntMathMix(x, y);
            const uint32_t built = IntMathHalfPatterns(x, y);
            for (uint32_t part = 0; part < 2u; ++part) {
                const uint32_t h = (v >> (part * 16u)) & 0xFFFFu;
                const uint32_t exponent = (h >> 10) & 0x1Fu;
                uint32_t want = h;
                if (exponent == 0u) {
                    want = h + 0x400u;
                    ++raised;
                } else if (exponent == 31u) {
                    want = h - (h & 0x3FFu);
                }
                if (((built >> (part * 16u)) & 0xFFFFu) != want) {
                    ++patterns;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, "the minimum, maximum and clamps agree with 64 bit comparisons",
               CHECK_NONE, ordering == 0 && ends_seen > 0,
               "%u of 4096 pixels disagreed; the most negative against the most positive was "
               "seen %u times",
               (unsigned)ordering, (unsigned)ends_seen);
    CHECK_TRUE(SUITE, "the half patterns raise exponent nought and quiet nothing else",
               CHECK_NONE, patterns == 0 && raised > 0,
               "%u of 8192 halves disagreed; %u had an exponent of nought", (unsigned)patterns,
               (unsigned)raised);

    // The two round trips, over every value a half can hold: the snorm one against unpacking
    // and packing in single precision, and the half one against the format reference's
    // converter, which is written differently.
    uint32_t snorm_roundtrip = 0;
    uint32_t minus_32768 = 0;
    uint32_t half_patterns = 0;
    for (uint32_t h = 0; h < 0x10000u; ++h) {
        const int16_t code = (int16_t)h;
        float unpacked = (float)code / 32767.0f;
        unpacked = unpacked < -1.0f ? -1.0f : unpacked;
        const float pair[2] = { unpacked, 0.0f };
        const uint32_t repacked = IntMathPackSnorm2x16(pair) & 0xFFFFu;
        if ((IntMathRoundtripSnorm2x16(h) & 0xFFFFu) != repacked) {
            ++snorm_roundtrip;
        }
        if (h == 0x8000u && repacked == 0x8001u) {
            ++minus_32768;
        }
        const uint32_t exponent = (h >> 10) & 0x1Fu;
        if (exponent != 0u && !(exponent == 31u && (h & 0x3FFu) != 0u)) {
            const uint32_t bits = IntMathFloatFromHalfBits(h);
            float value;
            memcpy(&value, &bits, sizeof(value));
            const float other = FormatFloatFromHalf(h);
            uint32_t other_bits;
            memcpy(&other_bits, &other, sizeof(other_bits));
            if (bits != other_bits || FormatHalfFromFloat(value) != h) {
                ++half_patterns;
            }
        }
    }
    CHECK_TRUE(SUITE, "an snorm round trip returns every half but 0x8000, which becomes 0x8001",
               CHECK_NONE, snorm_roundtrip == 0 && minus_32768 == 1u,
               "%u of 65536 disagreed, and 0x8000 %s", (unsigned)snorm_roundtrip,
               minus_32768 == 1u ? "did become 0x8001" : "did not become 0x8001");
    CHECK_TRUE(SUITE, "the half precision patterns agree with the format reference both ways",
               CHECK_NONE, half_patterns == 0, "%u normal and infinite halves disagreed",
               (unsigned)half_patterns);
}

/// The float edge modes: hand values from exact rationals and 60 digit decimals, and the judge's
/// rule about noughts, NaNs and the answers the console never gives, pixel by pixel.
static void CheckFloatEdges(void) {
    static const struct {
        FMathMode mode;
        uint32_t x;
        uint32_t y;
        bool nan;
        uint32_t expected;
    } BY_HAND[] = {
        // The original logarithm's operands, now with a full mantissa.
        { FMATH_LOG2, 41, 17, false, 0xC0FCEAA1u },
        // The exact product less the rounded one with its low twelve bits cleared.
        { FMATH_FMA_CANCELLATION, 1, 0, false, 0x362AB000u },
        { FMATH_FMA_CANCELLATION, 20, 5, false, 0x31600000u },
        { FMATH_FMA_CANCELLATION, 62, 9, false, 0x32280000u },
        { FMATH_FMA_CANCELLATION, 9, 6, false, 0x36AAB800u },
        { FMATH_MIN_MAX_INFINITY, 9, 0, false, 0x3E200000u },
        { FMATH_MIN_MAX_INFINITY, 9, 1, false, 0x3E200000u },
        { FMATH_MIN_MAX_INFINITY, 9, 2, false, 0xFF800000u },
        { FMATH_MIN_MAX_INFINITY, 9, 3, false, 0x7F800000u },
        { FMATH_MIN_MAX_NAN, 9, 0, false, 0x3E200000u },
        { FMATH_MIN_MAX_NAN, 9, 1, false, 0x3E200000u },
        { FMATH_MIN_MAX_NAN, 9, 2, false, 0x3E200000u },
        { FMATH_MIN_MAX_NAN, 9, 3, false, 0xBE200000u },
        { FMATH_MIX_INFINITY_NAN, 9, 0, true, 0u },
        { FMATH_MIX_INFINITY_NAN, 9, 1, false, 0x7F800000u },
        { FMATH_MIX_INFINITY_NAN, 9, 2, false, 0xFF800000u },
        { FMATH_MIX_INFINITY_NAN, 9, 3, true, 0u },
        // Flushed, as the console flushes: nought, nought, the smallest normal, nought.
        { FMATH_DENORMAL, 5, 0, false, 0x00000000u },
        { FMATH_DENORMAL, 5, 1, false, 0x00000000u },
        { FMATH_DENORMAL, 5, 2, false, 0x00800000u },
        { FMATH_DENORMAL, 5, 3, false, 0x00000000u },
        // Two to the -128 is a denormal and flushes; to the -126 is the smallest normal, kept.
        { FMATH_EXP2_EDGES, 0, 0, false, 0x00000000u },
        { FMATH_EXP2_EDGES, 0, 32, false, 0x00800000u },
        { FMATH_EXP2_EDGES, 31, 0, false, 0x3D800000u },
        { FMATH_EXP2_EDGES, 63, 15, false, 0x7DF5257Du },
        // From just above the smallest normal to nearly the largest float, full mantissas.
        { FMATH_LOG2_EDGES, 0, 0, false, 0xC2FA757Fu },
        { FMATH_LOG2_EDGES, 31, 63, false, 0xBF9412B3u },
        { FMATH_LOG2_EDGES, 32, 0, false, 0x40114B89u },
        { FMATH_LOG2_EDGES, 63, 63, false, 0x42FCBE73u },
        // A denormal operand flushes: the root of nought, and its inverse.
        { FMATH_SQRT_TINY, 0, 0, false, 0x00000000u },
        { FMATH_SQRT_TINY, 1, 63, false, 0x00000000u },
        { FMATH_SQRT_TINY, 2, 0, false, 0x20000000u },
        { FMATH_SQRT_TINY, 63, 63, false, 0x27B44F94u },
        { FMATH_INVERSESQRT_TINY, 0, 0, false, 0x7F800000u },
        { FMATH_INVERSESQRT_TINY, 1, 63, false, 0x7F800000u },
        { FMATH_INVERSESQRT_TINY, 2, 0, false, 0x5EFFFFFFu },
        { FMATH_INVERSESQRT_TINY, 63, 63, false, 0x5735BB09u },
        { FMATH_FLOOR, 0, 0, false, 0xC2000000u },
        { FMATH_FLOOR, 31, 1, false, 0xBF800000u },
        { FMATH_FLOOR, 32, 63, false, 0x00000000u },
        { FMATH_FLOOR, 10, 17, false, 0xC1B00000u },
        { FMATH_FRACT, 31, 1, false, 0x3C800000u },
        { FMATH_FRACT, 10, 17, false, 0x3E880000u },
        { FMATH_FRACT, 40, 32, false, 0x3F000000u },
        // -2.5 to even is -2 under both; 4.5 is 4; -0.25 is minus nought.
        { FMATH_ROUND, 29, 2, false, 0xC0000000u },
        { FMATH_ROUND, 35, 2, false, 0x40800000u },
        { FMATH_ROUND, 30, 1, false, 0xC0000000u },
        { FMATH_ROUND, 33, 3, false, 0x40000000u },
        { FMATH_ROUND, 31, 3, false, 0x80000000u },
        { FMATH_ROUND_EVEN, 29, 2, false, 0xC0000000u },
        { FMATH_ROUND_EVEN, 35, 2, false, 0x40800000u },
        { FMATH_ROUND_EVEN, 30, 2, false, 0xC0000000u },
        { FMATH_ROUND_EVEN, 33, 2, false, 0x40000000u },
        { FMATH_ROUND_EVEN, 31, 3, false, 0x80000000u },
    };
    uint32_t wrong = 0;
    size_t first = 0;
    uint32_t first_got = 0;
    for (size_t i = 0; i < sizeof(BY_HAND) / sizeof(BY_HAND[0]); ++i) {
        const float value = FMathReference(BY_HAND[i].mode, BY_HAND[i].x, BY_HAND[i].y, 0.0f);
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        const bool right = BY_HAND[i].nan ? isnan(value) : bits == BY_HAND[i].expected;
        if (!right) {
            if (wrong == 0) {
                first = i;
                first_got = bits;
            }
            ++wrong;
        }
    }
    CHECK_TRUE(SUITE, "the float edge reference matches the values worked out by hand",
               CHECK_NONE, wrong == 0,
               "%u of %u disagreed, first \"%s\" at (%u,%u) gave 0x%08x, expected 0x%08x",
               (unsigned)wrong, (unsigned)(sizeof(BY_HAND) / sizeof(BY_HAND[0])),
               FMathName(BY_HAND[first].mode), (unsigned)BY_HAND[first].x,
               (unsigned)BY_HAND[first].y, (unsigned)first_got,
               (unsigned)BY_HAND[first].expected);

    // The judge, one decision at a time: the console's answer and nothing else, noughts by sign.
    static const struct {
        FMathMode mode;
        uint32_t x;
        uint32_t y;
        uint32_t got;
        bool within;
        const char *why;
    } JUDGED[] = {
        { FMATH_FMA_CANCELLATION, 1, 0, 0x362AB000u, true, "the fused answer" },
        { FMATH_FMA_CANCELLATION, 1, 0, 0x00000000u, false, "nought for a residual" },
        { FMATH_FMA_CANCELLATION, 1, 0, 0x362AB001u, false, "a unit off" },
        { FMATH_MIX_INFINITY_NAN, 9, 0, 0x7FC00000u, true, "a NaN where one is due" },
        { FMATH_MIX_INFINITY_NAN, 9, 0, 0xFFC12345u, true, "any NaN where one is due" },
        { FMATH_MIX_INFINITY_NAN, 9, 0, 0x7F800000u, false, "infinity for a NaN" },
        { FMATH_MIN_MAX_NAN, 9, 0, 0x7FC00000u, false, "a NaN from min" },
        { FMATH_DENORMAL, 5, 0, 0x00000000u, true, "a flushed product" },
        { FMATH_DENORMAL, 5, 0, 0x80000000u, false, "minus nought for a flushed positive" },
        { FMATH_DENORMAL, 5, 0, 0x0000C000u, false, "the denormal kept" },
        { FMATH_DENORMAL, 5, 2, 0x00800000u, true, "a flushed operand" },
        { FMATH_EXP2_EDGES, 0, 0, 0x00000000u, true, "a flushed denormal answer" },
        { FMATH_EXP2_EDGES, 0, 0, 0x80000000u, false, "minus nought for a flushed answer" },
        { FMATH_EXP2_EDGES, 31, 0, 0x00000000u, false, "nought for a normal answer" },
        { FMATH_SQRT_TINY, 0, 0, 0x00000000u, true, "the root of a flushed operand" },
        { FMATH_SQRT_TINY, 2, 0, 0x00000000u, false, "nought for a normal operand" },
        { FMATH_INVERSESQRT_TINY, 0, 0, 0x7F800000u, true, "infinity for a flushed operand" },
        { FMATH_ROUND, 29, 2, 0xC0000000u, true, "-2.5 to -2" },
        { FMATH_ROUND, 29, 2, 0xC0400000u, false, "-2.5 to -3" },
        { FMATH_ROUND, 31, 3, 0x80000000u, true, "-0.25 to minus nought" },
        { FMATH_ROUND, 31, 3, 0x00000000u, false, "-0.25 to nought" },
        { FMATH_ROUND, 30, 1, 0xBF800000u, false, "-1.75 to -1" },
        { FMATH_FLOOR, 31, 1, 0x00000000u, false, "floor towards nought" },
        { FMATH_FLOOR, 32, 63, 0x80000000u, false, "floor of 0.98 to minus nought" },
    };
    uint32_t misjudged = 0;
    const char *first_misjudged = "";
    for (size_t i = 0; i < sizeof(JUDGED) / sizeof(JUDGED[0]); ++i) {
        float got;
        memcpy(&got, &JUDGED[i].got, sizeof(got));
        const FMathVerdict verdict =
            FMathJudge(JUDGED[i].mode, JUDGED[i].x, JUDGED[i].y, 0.0f, got);
        if (verdict.within != JUDGED[i].within) {
            if (misjudged == 0) {
                first_misjudged = JUDGED[i].why;
            }
            ++misjudged;
        }
    }
    CHECK_TRUE(SUITE, "the float judge takes the console's answer, noughts by their sign",
               CHECK_NONE, misjudged == 0, "%u of %u decisions were wrong, first \"%s\"",
               (unsigned)misjudged, (unsigned)(sizeof(JUDGED) / sizeof(JUDGED[0])),
               first_misjudged);

    // And over every pixel: wherever the answer the console's binary does not give differs from
    // the console's, the judge refuses it and names it, so no machine that fuses nothing, keeps
    // its denormals or rounds halves away passes one of these cases by being that machine.
    uint32_t taken = 0;
    uint32_t differing = 0;
    for (uint32_t mode = 0; mode < FMATH_MODE_COUNT; ++mode) {
        for (uint32_t y = 0; y < 64u; ++y) {
            for (uint32_t x = 0; x < 64u; ++x) {
                float other;
                if (!FMathOther((FMathMode)mode, x, y, &other)) {
                    continue;
                }
                const float want = FMathReference((FMathMode)mode, x, y, 0.0f);
                uint32_t other_bits;
                uint32_t want_bits;
                memcpy(&other_bits, &other, sizeof(other_bits));
                memcpy(&want_bits, &want, sizeof(want_bits));
                if (other_bits == want_bits) {
                    continue;
                }
                ++differing;
                const FMathVerdict verdict = FMathJudge((FMathMode)mode, x, y, 0.0f, other);
                if (verdict.within || !verdict.other) {
                    ++taken;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, "the answer the console never gives is refused and named at every pixel",
               CHECK_NONE, taken == 0 && differing > 0,
               "%u of %u such pixels were taken or not named", (unsigned)taken,
               (unsigned)differing);

    // Every operand the edge modes build from bits is what its comment says: the logarithm's
    // never inside a half to two and never below the smallest normal, the power's exact, and the
    // roots' denormal in exactly the first two columns.
    uint32_t misplaced = 0;
    uint32_t halves = 0;
    for (uint32_t y = 0; y < 64u; ++y) {
        for (uint32_t x = 0; x < 64u; ++x) {
            const float log_operand = FMathArgument(FMATH_LOG2_EDGES, x, y);
            if ((log_operand >= 0.5f && log_operand < 2.0f) || log_operand < FLT_MIN ||
                isinf(log_operand)) {
                ++misplaced;
            }
            const float power = FMathArgument(FMATH_EXP2_EDGES, x, y);
            if ((double)power != (double)x * 4.0 - 128.0 + (double)y / 16.0 || power >= 128.0f) {
                ++misplaced;
            }
            const float root = FMathArgument(FMATH_SQRT_TINY, x, y);
            if ((root < FLT_MIN) != (x < 2u) || root == 0.0f) {
                ++misplaced;
            }
            const float quarter = FMathArgument(FMATH_ROUND, x, y);
            if (quarter - floorf(quarter) == 0.5f) {
                ++halves;
            }
        }
    }
    CHECK_TRUE(SUITE, "the edge operands are where their budgets say", CHECK_NONE,
               misplaced == 0 && halves == 1024u,
               "%u operands out of place; %u of 4096 rounding operands on a half, 1024 expected",
               (unsigned)misplaced, (unsigned)halves);
}

/// What a format stores: whole pixels worked out by hand, the rules one at a time, and the
/// properties that make every normalised case exact.
static void CheckFormats(void) {
    CHECK_TRUE(SUITE, "the format table has the sixty renderable colour formats", CHECK_NONE,
               FORMAT_COUNT == 60u, "%u formats", (unsigned)FORMAT_COUNT);

    // Every format's clear and drawn pixel and its defined bits, worked out from the format's
    // name alone and the value rules restated, by tools/format-pixels.py, which shares nothing
    // with source/formatref.c and prints this table. BGR565 drawn, for one:
    // red 25 in bits 11 to 15, green 7 in 5 to 10, blue 31 in 0 to 4, so 0xC8FF.
    static const struct {
        const char *format;
        uint8_t clear[FORMAT_MAX_BYTES];
        uint8_t drawn[FORMAT_MAX_BYTES];
        uint8_t mask[FORMAT_MAX_BYTES];
    } BY_HAND[] = {
        { "R8_Unorm", { 0x33 },
          { 0xCC },
          { 0xFF } },
        { "R8_Snorm", { 0x2A },
          { 0x9B },
          { 0xFF } },
        { "R8_Uint", { 0xB9 },
          { 0x46 },
          { 0xFF } },
        { "R8_Sint", { 0xB9 },
          { 0x46 },
          { 0xFF } },
        { "R16_Float", { 0x00, 0x38 },
          { 0x00, 0x3E },
          { 0xFF, 0xFF } },
        { "R16_Unorm", { 0x33, 0x33 },
          { 0xCC, 0xCC },
          { 0xFF, 0xFF } },
        { "R16_Snorm", { 0xAA, 0x2A },
          { 0x9B, 0x99 },
          { 0xFF, 0xFF } },
        { "R16_Uint", { 0xB9, 0x79 },
          { 0x46, 0x86 },
          { 0xFF, 0xFF } },
        { "R16_Sint", { 0xB9, 0x79 },
          { 0x46, 0x86 },
          { 0xFF, 0xFF } },
        { "R32_Float", { 0x00, 0x00, 0x00, 0x3F },
          { 0xDB, 0x0F, 0x49, 0x40 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "R32_Uint", { 0xB9, 0x79, 0x37, 0x9E },
          { 0x46, 0x86, 0xC8, 0x61 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "R32_Sint", { 0xB9, 0x79, 0x37, 0x9E },
          { 0x46, 0x86, 0xC8, 0x61 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG8_Unorm", { 0x33, 0x66 },
          { 0xCC, 0x1C },
          { 0xFF, 0xFF } },
        { "RG8_Snorm", { 0x2A, 0xC1 },
          { 0x9B, 0x12 },
          { 0xFF, 0xFF } },
        { "RG8_Uint", { 0xB9, 0x72 },
          { 0x46, 0x8D },
          { 0xFF, 0xFF } },
        { "RG8_Sint", { 0xB9, 0x72 },
          { 0x46, 0x8D },
          { 0xFF, 0xFF } },
        { "RG16_Float", { 0x00, 0x38, 0x00, 0xBD },
          { 0x00, 0x3E, 0x80, 0xC0 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG16_Unorm", { 0x33, 0x33, 0x66, 0x66 },
          { 0xCC, 0xCC, 0x71, 0x1C },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG16_Snorm", { 0xAA, 0x2A, 0x01, 0xC0 },
          { 0x9B, 0x99, 0x49, 0x12 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG16_Uint", { 0xB9, 0x79, 0x72, 0xF3 },
          { 0x46, 0x86, 0x8D, 0x0C },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG16_Sint", { 0xB9, 0x79, 0x72, 0xF3 },
          { 0x46, 0x86, 0x8D, 0x0C },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG32_Float", { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0xA0, 0xBF },
          { 0xDB, 0x0F, 0x49, 0x40, 0xCA, 0xF2, 0x49, 0xF1 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG32_Uint", { 0xB9, 0x79, 0x37, 0x9E, 0x72, 0xF3, 0x6E, 0x3C },
          { 0x46, 0x86, 0xC8, 0x61, 0x8D, 0x0C, 0x91, 0xC3 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG32_Sint", { 0xB9, 0x79, 0x37, 0x9E, 0x72, 0xF3, 0x6E, 0x3C },
          { 0x46, 0x86, 0xC8, 0x61, 0x8D, 0x0C, 0x91, 0xC3 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA8_Unorm", { 0x33, 0x66, 0x99, 0xFF },
          { 0xCC, 0x1C, 0xFF, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA8_Snorm", { 0x2A, 0xC1, 0x81, 0x7F },
          { 0x9B, 0x12, 0x7F, 0x81 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA8_Uint", { 0xB9, 0x72, 0x2B, 0xE4 },
          { 0x46, 0x8D, 0xD4, 0x1B },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA8_Sint", { 0xB9, 0x72, 0x2B, 0xE4 },
          { 0x46, 0x8D, 0xD4, 0x1B },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA16_Float", { 0x00, 0x38, 0x00, 0xBD, 0x00, 0x64, 0x80, 0xC3 },
          { 0x00, 0x3E, 0x80, 0xC0, 0xFF, 0x7B, 0x00, 0x04 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA16_Unorm", { 0x33, 0x33, 0x66, 0x66, 0x99, 0x99, 0xFF, 0xFF },
          { 0xCC, 0xCC, 0x71, 0x1C, 0xFF, 0xFF, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA16_Snorm", { 0xAA, 0x2A, 0x01, 0xC0, 0x01, 0x80, 0xFF, 0x7F },
          { 0x9B, 0x99, 0x49, 0x12, 0xFF, 0x7F, 0x01, 0x80 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA16_Uint", { 0xB9, 0x79, 0x72, 0xF3, 0x2B, 0x6D, 0xE4, 0xE6 },
          { 0x46, 0x86, 0x8D, 0x0C, 0xD4, 0x92, 0x1B, 0x19 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA16_Sint", { 0xB9, 0x79, 0x72, 0xF3, 0x2B, 0x6D, 0xE4, 0xE6 },
          { 0x46, 0x86, 0x8D, 0x0C, 0xD4, 0x92, 0x1B, 0x19 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA32_Float", { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0xA0, 0xBF, 0x00, 0x00, 0x80, 0x44, 0x00, 0x00, 0x70, 0xC0 },
          { 0xDB, 0x0F, 0x49, 0x40, 0xCA, 0xF2, 0x49, 0xF1, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x7F },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA32_Uint", { 0xB9, 0x79, 0x37, 0x9E, 0x72, 0xF3, 0x6E, 0x3C, 0x2B, 0x6D, 0xA6, 0xDA, 0xE4, 0xE6, 0xDD, 0x78 },
          { 0x46, 0x86, 0xC8, 0x61, 0x8D, 0x0C, 0x91, 0xC3, 0xD4, 0x92, 0x59, 0x25, 0x1B, 0x19, 0x22, 0x87 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBA32_Sint", { 0xB9, 0x79, 0x37, 0x9E, 0x72, 0xF3, 0x6E, 0x3C, 0x2B, 0x6D, 0xA6, 0xDA, 0xE4, 0xE6, 0xDD, 0x78 },
          { 0x46, 0x86, 0xC8, 0x61, 0x8D, 0x0C, 0x91, 0xC3, 0xD4, 0x92, 0x59, 0x25, 0x1B, 0x19, 0x22, 0x87 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBX8_Unorm_sRGB", { 0x00, 0xFF, 0x00, 0x00 },
          { 0xBC, 0x3C, 0xFF, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0x00 } },
        { "RGBA8_Unorm_sRGB", { 0x00, 0xFF, 0x00, 0xFF },
          { 0xBC, 0x3C, 0xFF, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGB10A2_Unorm", { 0xCC, 0x64, 0x56, 0xE6 },
          { 0x33, 0xC7, 0xF1, 0x3F },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGB10A2_Uint", { 0xB9, 0xC9, 0xBD, 0x12 },
          { 0x46, 0x36, 0x42, 0xED },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RG11B10_Float", { 0x80, 0x83, 0x1E, 0x84 },
          { 0xE0, 0x03, 0x2B, 0x68 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "RGBX8_Unorm", { 0x33, 0x66, 0x99, 0x00 },
          { 0xCC, 0x1C, 0xFF, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0x00 } },
        { "RGBX8_Snorm", { 0x2A, 0xC1, 0x81, 0x00 },
          { 0x9B, 0x12, 0x7F, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0x00 } },
        { "RGBX8_Uint", { 0xB9, 0x72, 0x2B, 0x00 },
          { 0x46, 0x8D, 0xD4, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0x00 } },
        { "RGBX8_Sint", { 0xB9, 0x72, 0x2B, 0x00 },
          { 0x46, 0x8D, 0xD4, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0x00 } },
        { "RGBX16_Float", { 0x00, 0x38, 0x00, 0xBD, 0x00, 0x64, 0x00, 0x00 },
          { 0x00, 0x3E, 0x80, 0xC0, 0xFF, 0x7B, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 } },
        { "RGBX16_Unorm", { 0x33, 0x33, 0x66, 0x66, 0x99, 0x99, 0x00, 0x00 },
          { 0xCC, 0xCC, 0x71, 0x1C, 0xFF, 0xFF, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 } },
        { "RGBX16_Snorm", { 0xAA, 0x2A, 0x01, 0xC0, 0x01, 0x80, 0x00, 0x00 },
          { 0x9B, 0x99, 0x49, 0x12, 0xFF, 0x7F, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 } },
        { "RGBX16_Uint", { 0xB9, 0x79, 0x72, 0xF3, 0x2B, 0x6D, 0x00, 0x00 },
          { 0x46, 0x86, 0x8D, 0x0C, 0xD4, 0x92, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 } },
        { "RGBX16_Sint", { 0xB9, 0x79, 0x72, 0xF3, 0x2B, 0x6D, 0x00, 0x00 },
          { 0x46, 0x86, 0x8D, 0x0C, 0xD4, 0x92, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 } },
        { "RGBX32_Float", { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0xA0, 0xBF, 0x00, 0x00, 0x80, 0x44, 0x00, 0x00, 0x00, 0x00 },
          { 0xDB, 0x0F, 0x49, 0x40, 0xCA, 0xF2, 0x49, 0xF1, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 } },
        { "RGBX32_Uint", { 0xB9, 0x79, 0x37, 0x9E, 0x72, 0xF3, 0x6E, 0x3C, 0x2B, 0x6D, 0xA6, 0xDA, 0x00, 0x00, 0x00, 0x00 },
          { 0x46, 0x86, 0xC8, 0x61, 0x8D, 0x0C, 0x91, 0xC3, 0xD4, 0x92, 0x59, 0x25, 0x00, 0x00, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 } },
        { "RGBX32_Sint", { 0xB9, 0x79, 0x37, 0x9E, 0x72, 0xF3, 0x6E, 0x3C, 0x2B, 0x6D, 0xA6, 0xDA, 0x00, 0x00, 0x00, 0x00 },
          { 0x46, 0x86, 0xC8, 0x61, 0x8D, 0x0C, 0x91, 0xC3, 0xD4, 0x92, 0x59, 0x25, 0x00, 0x00, 0x00, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 } },
        { "BGR565_Unorm", { 0x32, 0x33 },
          { 0xFF, 0xC8 },
          { 0xFF, 0xFF } },
        { "BGR5_Unorm", { 0x92, 0x19 },
          { 0x7F, 0x64 },
          { 0xFF, 0x7F } },
        { "BGR5A1_Unorm", { 0x92, 0x99 },
          { 0x7F, 0x64 },
          { 0xFF, 0xFF } },
        { "BGRX8_Unorm", { 0x99, 0x66, 0x33, 0x00 },
          { 0xFF, 0x1C, 0xCC, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0x00 } },
        { "BGRA8_Unorm", { 0x99, 0x66, 0x33, 0xFF },
          { 0xFF, 0x1C, 0xCC, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
        { "BGRX8_Unorm_sRGB", { 0x00, 0xFF, 0x00, 0x00 },
          { 0xFF, 0x3C, 0xBC, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0x00 } },
        { "BGRA8_Unorm_sRGB", { 0x00, 0xFF, 0x00, 0xFF },
          { 0xFF, 0x3C, 0xBC, 0x00 },
          { 0xFF, 0xFF, 0xFF, 0xFF } },
    };
    uint32_t wrong = 0;
    uint32_t found = 0;
    const char *first_wrong = "";
    for (size_t i = 0; i < sizeof(BY_HAND) / sizeof(BY_HAND[0]); ++i) {
        const FormatInfo *format = NULL;
        for (uint32_t f = 0; f < FORMAT_COUNT; ++f) {
            if (strcmp(FORMAT_TABLE[f].name, BY_HAND[i].format) == 0) {
                format = &FORMAT_TABLE[f];
            }
        }
        bool right = false;
        if (format != NULL) {
            ++found;
            uint8_t clear[FORMAT_MAX_BYTES];
            uint8_t drawn[FORMAT_MAX_BYTES];
            uint8_t mask[FORMAT_MAX_BYTES];
            const FormatValue clear_value = FormatClearValue(format);
            const FormatValue drawn_value = FormatDrawValue(format);
            FormatPack(format, &clear_value, clear, mask);
            FormatPack(format, &drawn_value, drawn, mask);
            right = memcmp(clear, BY_HAND[i].clear, format->bytes) == 0 &&
                    memcmp(drawn, BY_HAND[i].drawn, format->bytes) == 0 &&
                    memcmp(mask, BY_HAND[i].mask, format->bytes) == 0;
        }
        if (!right) {
            if (wrong == 0) {
                first_wrong = BY_HAND[i].format;
            }
            ++wrong;
        }
    }
    CHECK_TRUE(SUITE, "every format packs the pixels worked out from its name", CHECK_NONE,
               wrong == 0 && found == FORMAT_COUNT,
               "%u of %u disagreed, first %s; %u of the %u formats were in the list",
               (unsigned)wrong, (unsigned)(sizeof(BY_HAND) / sizeof(BY_HAND[0])), first_wrong,
               (unsigned)found, (unsigned)FORMAT_COUNT);

    // Over every format: the fields fit the pixel and do not overlap, the defined bits are the
    // fields' bits and nothing else, every unorm and snorm value times its largest code is a
    // code in single precision and every sRGB one within a thousandth of one, and the clear and
    // the drawn value differ in every channel the format stores.
    uint32_t layout = 0;
    uint32_t margin = 0;
    uint32_t same = 0;
    double worst_margin = 0.0;
    const char *first_bad = "";
    for (uint32_t f = 0; f < FORMAT_COUNT; ++f) {
        const FormatInfo *format = &FORMAT_TABLE[f];
        uint8_t claimed[FORMAT_MAX_BYTES] = { 0 };
        uint32_t field_bits = 0;
        bool bad = format->bytes == 0u || format->bytes > FORMAT_MAX_BYTES;
        for (uint32_t c = 0; c < 5u; ++c) {
            const FormatField *field = c < 4u ? &format->channel[c] : &format->undefined;
            if (field->kind == FORMAT_UNUSED) {
                continue;
            }
            for (uint32_t bit = field->shift; bit < (uint32_t)field->shift + field->bits; ++bit) {
                if (bit >= format->bytes * 8u || (claimed[bit / 8u] >> (bit % 8u)) & 1u) {
                    bad = true;
                    break;
                }
                claimed[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
            }
            if (c < 4u) {
                field_bits += field->bits;
            }
        }
        const FormatValue clear = FormatClearValue(format);
        const FormatValue drawn = FormatDrawValue(format);
        uint8_t bytes[FORMAT_MAX_BYTES];
        uint8_t mask[FORMAT_MAX_BYTES];
        FormatPack(format, &clear, bytes, mask);
        uint32_t mask_bits = 0;
        for (uint32_t i = 0; i < FORMAT_MAX_BYTES; ++i) {
            mask_bits += (uint32_t)__builtin_popcount(mask[i]);
        }
        if (mask_bits != field_bits) {
            bad = true;
        }
        if (bad) {
            if (layout == 0) {
                first_bad = format->name;
            }
            ++layout;
        }
        for (uint32_t c = 0; c < 4u; ++c) {
            const FormatField *field = &format->channel[c];
            if (field->kind == FORMAT_UNUSED) {
                continue;
            }
            for (uint32_t which = 0; which < 2u; ++which) {
                const uint32_t word = which == 0u ? clear.word[c] : drawn.word[c];
                if (field->kind == FORMAT_SRGB) {
                    const double off =
                        FormatCodeMargin((FormatKind)field->kind, field->bits, word);
                    if (off > worst_margin) {
                        worst_margin = off;
                    }
                    if (off > 0.001) {
                        ++margin;
                    }
                } else if (!FormatCodeExact((FormatKind)field->kind, field->bits, word)) {
                    ++margin;
                }
            }
            if (FormatEncodeField((FormatKind)field->kind, field->bits, clear.word[c]) ==
                FormatEncodeField((FormatKind)field->kind, field->bits, drawn.word[c])) {
                ++same;
            }
        }
    }
    CHECK_TRUE(SUITE, "every format's fields fit its pixel and the mask is exactly them",
               CHECK_NONE, layout == 0, "%u formats broke it, first %s", (unsigned)layout,
               first_bad);
    CHECK_TRUE(SUITE, "every normalised value times its largest code is a code in single "
               "precision", CHECK_NONE, margin == 0,
               "%u values were not; the worst sRGB one was %.6f of a code from its code",
               (unsigned)margin, worst_margin);
    CHECK_TRUE(SUITE, "the clear and the drawn value differ in every stored channel",
               CHECK_NONE, same == 0, "%u channels store the same code for both",
               (unsigned)same);

    // The conversions on their own, against their inverses over every code they have.
    uint32_t half = 0;
    for (uint32_t h = 0; h < 0x10000u; ++h) {
        const float value = FormatFloatFromHalf(h);
        if (!isnan(value) && FormatHalfFromFloat(value) != h) {
            ++half;
        }
    }
    uint32_t small = 0;
    for (uint32_t bits = 10u; bits <= 11u; ++bits) {
        for (uint32_t code = 0; code < (1u << bits); ++code) {
            const float value = FormatFloatFromUfloat(code, bits);
            if (!isnan(value) && FormatUfloatFromFloat(value, bits) != code) {
                ++small;
            }
        }
    }
    uint32_t srgb = 0;
    for (uint32_t code = 0; code < 256u; ++code) {
        const double linear = FormatSrgbDecode((double)code / 255.0);
        if (FormatEncodeField(FORMAT_SRGB, 8u, 0u) != 0u ||
            FormatUnorm(FormatSrgbEncode((double)(float)linear), 8u) != code) {
            ++srgb;
        }
    }
    // Rounding at the ends: the largest half and a value past it, a half-way value to even, and
    // minus one and below it in snorm. Each one named, so a runtime that disagrees says where.
    const struct {
        bool held;
        const char *what;
    } ENDS[] = {
        { FormatHalfFromFloat(65504.0f) == 0x7BFFu, "65504 is the largest half" },
        { FormatHalfFromFloat(65520.0f) == 0x7C00u, "65520 rounds up to infinity" },
        { FormatHalfFromFloat(1.0f + 1.0f / 2048.0f) == 0x3C00u, "a tie rounds down to even" },
        { FormatHalfFromFloat(1.0f + 3.0f / 2048.0f) == 0x3C02u, "a tie rounds up to even" },
        { FormatSnorm(-1.0, 8u) == 0x81u, "minus one is -127" },
        { FormatSnorm(-2.0, 8u) == 0x81u, "minus two clamps to -127" },
        { FormatSnorm(1.0, 16u) == 0x7FFFu, "one is 32767" },
        { FormatUnorm(1.5, 5u) == 31u, "1.5 clamps to the largest code" },
        { FormatUfloatFromFloat(-1.0f, 11u) == 0u, "a negative small float is nought" },
    };
    uint32_t ends_broken = 0;
    const char *first_end = "";
    for (size_t i = 0; i < sizeof(ENDS) / sizeof(ENDS[0]); ++i) {
        if (!ENDS[i].held) {
            if (ends_broken == 0) {
                first_end = ENDS[i].what;
            }
            ++ends_broken;
        }
    }
    CHECK_TRUE(SUITE, "the format conversions invert over every code and round at the ends",
               CHECK_NONE, half == 0 && small == 0 && srgb == 0 && ends_broken == 0,
               "%u halves, %u small floats and %u sRGB codes failed to come back; %u end cases "
               "did not hold, first \"%s\"",
               (unsigned)half, (unsigned)small, (unsigned)srgb, (unsigned)ends_broken, first_end);

    // The quad, projected by the rasterisation rule with a margin that no legal implementation
    // could argue with, covers exactly the rectangle the judge expects the drawn value in.
    float clip[12];
    FormatQuadClip(clip);
    const RasterViewport viewport = { 0.0f, 0.0f, (float)FORMAT_TARGET_SIZE,
                                      (float)FORMAT_TARGET_SIZE };
    RasterTriangle first_half;
    RasterTriangle second_half;
    RasterTriangleFromClip(&first_half, &viewport, clip);
    RasterTriangleFromClip(&second_half, &viewport, clip + 6);
    uint32_t misplaced = 0;
    for (uint32_t y = 0; y < FORMAT_TARGET_SIZE; ++y) {
        for (uint32_t x = 0; x < FORMAT_TARGET_SIZE; ++x) {
            const RasterVerdict a = RasterClassifyPixel(&first_half, x, y, 0.25);
            const RasterVerdict b = RasterClassifyPixel(&second_half, x, y, 0.25);
            const bool inside = a == RASTER_INSIDE || b == RASTER_INSIDE;
            // A centre on the diagonal the two halves share is inside one of them by the fill
            // rule; anywhere else near an edge would be a pixel the judge could not claim.
            const bool near_outer_edge =
                (a == RASTER_NEAR_EDGE && b == RASTER_OUTSIDE) ||
                (b == RASTER_NEAR_EDGE && a == RASTER_OUTSIDE);
            const bool covered = inside || RasterCovers(&first_half, x + 0.5, y + 0.5) ||
                                 RasterCovers(&second_half, x + 0.5, y + 0.5);
            if (near_outer_edge || covered != FormatInsideQuad(x, y)) {
                ++misplaced;
            }
        }
    }
    CHECK_TRUE(SUITE, "the format quad covers exactly its rectangle, no centre near its edges",
               CHECK_NONE, misplaced == 0, "%u of %u pixels disagreed", (unsigned)misplaced,
               (unsigned)(FORMAT_TARGET_SIZE * FORMAT_TARGET_SIZE));
}

void RunReference(void) {
    CheckViewportTransform();
    CheckCoverageRule();
    CheckDrawnGeometry();
    CheckSharedEdge();
    CheckEdgeDistanceAndMargin();
    CheckIntegerArithmetic();
    CheckIntegerArithmeticGrown();
    CheckBlendArithmetic();
    CheckWrapModes();
    CheckTextureCoordinates();
    CheckCoherenceRules();
    CheckFloatArithmetic();
    CheckFloatEdges();
    CheckFormats();
    RunReferenceNet();
    RunReferenceCpu();
    RunReferenceRaw();
    RunReferenceAstc();
    RunReferenceEac();
    RunReferenceEtc2();
    RunReferenceBcn();
}
