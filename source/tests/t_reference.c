// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../blendref.h"
#include "../check.h"
#include "../intref.h"
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
            const uint32_t quotient = IntMathReference(INTMATH_DIVIDE, px, py, probe);
            const uint32_t remainder = IntMathReference(INTMATH_REMAINDER, px, py, probe);
            if (quotient * INTMATH_DIVISOR + remainder != value || remainder >= INTMATH_DIVISOR) {
                ++division_broken;
            }

            const uint32_t set = IntMathReference(INTMATH_BIT_COUNT, px, py, probe);
            if (set > 32u) {
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

    CHECK_TRUE(SUITE, "quotient times divisor plus remainder is the value again", CHECK_NONE,
               division_broken == 0, "%u of 4096 inputs did not reconstruct",
               (unsigned)division_broken);
    CHECK_TRUE(SUITE, "no value has more than 32 bits set", CHECK_NONE, count_broken == 0,
               "%u of 4096 inputs counted more than 32", (unsigned)count_broken);
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
        uint8_t said[4];
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

void RunReference(void) {
    CheckViewportTransform();
    CheckCoverageRule();
    CheckSharedEdge();
    CheckEdgeDistanceAndMargin();
    CheckIntegerArithmetic();
    CheckBlendArithmetic();
}
