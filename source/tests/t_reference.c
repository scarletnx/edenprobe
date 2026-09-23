// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../blendref.h"
#include "../check.h"
#include "../coherref.h"
#include "../fmathref.h"
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
        if ((double)(a * 0.25f) != (double)a * 0.25) {
            ++inexact;
        }
        if ((double)(a * 6.0f - 3.0f) != (double)a * 6.0 - 3.0) {
            ++inexact;
        }
        // The logarithm's operand has to stay outside the range from a half to two, where the
        // specification swaps its budget for an absolute one. That is what the quarter is for.
        if (a * 0.25f >= 0.5f) {
            ++log_in_range;
        }
        // And the sine and the cosine have to stay inside minus pi to pi, which is the only
        // range the specification bounds them over at all.
        const float angle = a * 6.0f - 3.0f;
        if (angle < -3.14159265f || angle > 3.14159265f) {
            ++trig_out_of_range;
        }
    }
    CHECK_TRUE(SUITE, "every float operand and argument is exactly representable", CHECK_NONE,
               inexact == 0, "%u of 256 formed values differed between single and double",
               (unsigned)inexact);
    CHECK_TRUE(SUITE, "the logarithm's operand stays where one budget covers it", CHECK_NONE,
               log_in_range == 0, "%u of 64 operands landed inside the range from a half to two",
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
            for (uint32_t mode = 0; mode < FMATH_MODE_COUNT; ++mode) {
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
    CHECK_TRUE(SUITE, "no float case produces an infinity or a nan", CHECK_NONE, not_finite == 0,
               "%u of 40960 reference values were not finite", (unsigned)not_finite);

    // And every budget, stated here so that loosening one is a change to this file as well as to
    // the one that hands it out. All ten, not a sample: the ones that were wrong when this file
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
        // Three roundings against the fused reference's one, each at most half a unit of a
        // result that is larger than either operand.
        { FMATH_MULTIPLY_ADD, false, 1.5f, 0.0f },
        // 2.5 from the core table and a half for the reference's own rounding.
        { FMATH_DIVIDE, false, 3.0f, 0.0f },
        // Inherited from one over the inverse square root: 2 and 2.5, and a half again.
        { FMATH_SQUARE_ROOT, false, 5.0f, 0.0f },
        // Three plus twice the magnitude of the argument, and a half on the constant.
        { FMATH_EXP2, false, 3.5f, 2.0f },
        { FMATH_LOG2, false, 3.5f, 0.0f },
        { FMATH_SINE, true, 1.0f / 2048.0f, 0.0f },
        { FMATH_COSINE, true, 1.0f / 2048.0f, 0.0f },
    };
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
        // the logarithm to base two of 0.00390625, 0.09375 and 0.25
        { FMATH_LOG2, 0, 0xC1000000u },
        { FMATH_LOG2, 23, 0xC05A8FF9u },
        { FMATH_LOG2, 63, 0xC0000000u },
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

void RunReference(void) {
    CheckViewportTransform();
    CheckCoverageRule();
    CheckDrawnGeometry();
    CheckSharedEdge();
    CheckEdgeDistanceAndMargin();
    CheckIntegerArithmetic();
    CheckBlendArithmetic();
    CheckWrapModes();
    CheckTextureCoordinates();
    CheckCoherenceRules();
    CheckFloatArithmetic();
}
