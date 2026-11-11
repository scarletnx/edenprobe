// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../report.h"
#include "../topologyref.h"
#include "tests.h"

// The topology suite's rules, against answers worked out by hand from OpenGL's table 10.2 and
// section 10.1: the triangles and segments each primitive type makes of a few vertices and the
// vertex each triangle takes its colour from, where an index list restarts, and on the suite's
// own geometry, that no pixel centre sits near an edge where the colour changes and that every
// mistake a case is there for moves pixels it judges.

static const char *const SUITE = "reference";

#define TARGET 64u

static const uint32_t IDENTITY[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/// Whether @p got holds the @p count triangles at @p want, three vertices and the provoking one
/// each, in order.
static bool SameTriangles(const TopologyTriangle *got, uint32_t got_count,
                          const uint32_t (*want)[4], uint32_t count) {
    if (got_count != count) {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (got[i].v[0] != want[i][0] || got[i].v[1] != want[i][1] || got[i].v[2] != want[i][2] ||
            got[i].provoking != want[i][3]) {
            return false;
        }
    }
    return true;
}

static void Triangles(void) {
    TopologyTriangle t[16];
    // Seven vertices as triangles: two, the seventh left over.
    static const uint32_t LIST_LAST[2][4] = { { 0, 1, 2, 2 }, { 3, 4, 5, 5 } };
    static const uint32_t LIST_FIRST[2][4] = { { 0, 1, 2, 0 }, { 3, 4, 5, 3 } };
    const bool list =
        SameTriangles(t, TopologyTriangles(TOPOLOGY_TRIANGLES, IDENTITY, 7, true, t, 16), LIST_LAST,
                      2) &&
        SameTriangles(t, TopologyTriangles(TOPOLOGY_TRIANGLES, IDENTITY, 7, false, t, 16),
                      LIST_FIRST, 2);
    // Five vertices as a strip: 012, 123, 234; the last vertex provokes 2, 3, 4, the first 0, 1, 2.
    static const uint32_t STRIP_LAST[3][4] = { { 0, 1, 2, 2 }, { 1, 2, 3, 3 }, { 2, 3, 4, 4 } };
    static const uint32_t STRIP_FIRST[3][4] = { { 0, 1, 2, 0 }, { 1, 2, 3, 1 }, { 2, 3, 4, 2 } };
    const bool strip =
        SameTriangles(t, TopologyTriangles(TOPOLOGY_TRIANGLE_STRIP, IDENTITY, 5, true, t, 16),
                      STRIP_LAST, 3) &&
        SameTriangles(t, TopologyTriangles(TOPOLOGY_TRIANGLE_STRIP, IDENTITY, 5, false, t, 16),
                      STRIP_FIRST, 3);
    // Five as a fan: 012, 023, 034; the last provokes 2, 3, 4, the first the rim's 1, 2, 3.
    static const uint32_t FAN_LAST[3][4] = { { 0, 1, 2, 2 }, { 0, 2, 3, 3 }, { 0, 3, 4, 4 } };
    static const uint32_t FAN_FIRST[3][4] = { { 0, 1, 2, 1 }, { 0, 2, 3, 2 }, { 0, 3, 4, 3 } };
    const bool fan =
        SameTriangles(t, TopologyTriangles(TOPOLOGY_TRIANGLE_FAN, IDENTITY, 5, true, t, 16),
                      FAN_LAST, 3) &&
        SameTriangles(t, TopologyTriangles(TOPOLOGY_TRIANGLE_FAN, IDENTITY, 5, false, t, 16),
                      FAN_FIRST, 3);
    // Ten as quads: two quads, two left over, each half carrying its quad's fourth or first.
    static const uint32_t QUADS_LAST[4][4] = {
        { 0, 1, 2, 3 }, { 0, 2, 3, 3 }, { 4, 5, 6, 7 }, { 4, 6, 7, 7 }
    };
    static const uint32_t QUADS_FIRST[4][4] = {
        { 0, 1, 2, 0 }, { 0, 2, 3, 0 }, { 4, 5, 6, 4 }, { 4, 6, 7, 4 }
    };
    const bool quads =
        SameTriangles(t, TopologyTriangles(TOPOLOGY_QUADS, IDENTITY, 10, true, t, 16), QUADS_LAST,
                      4) &&
        SameTriangles(t, TopologyTriangles(TOPOLOGY_QUADS, IDENTITY, 10, false, t, 16), QUADS_FIRST,
                      4);
    // Seven as a quad strip: quads 0 1 3 2 and 2 3 5 4, the seventh left over; the last vertex
    // provokes 3 and 5, the first 0 and 2.
    static const uint32_t STRIP4_LAST[4][4] = {
        { 0, 1, 3, 3 }, { 0, 3, 2, 3 }, { 2, 3, 5, 5 }, { 2, 5, 4, 5 }
    };
    static const uint32_t STRIP4_FIRST[4][4] = {
        { 0, 1, 3, 0 }, { 0, 3, 2, 0 }, { 2, 3, 5, 2 }, { 2, 5, 4, 2 }
    };
    const bool quad_strip =
        SameTriangles(t, TopologyTriangles(TOPOLOGY_QUAD_STRIP, IDENTITY, 7, true, t, 16),
                      STRIP4_LAST, 4) &&
        SameTriangles(t, TopologyTriangles(TOPOLOGY_QUAD_STRIP, IDENTITY, 7, false, t, 16),
                      STRIP4_FIRST, 4);
    // Five as a polygon: a fan from 0, which provokes under either convention.
    static const uint32_t POLYGON[3][4] = { { 0, 1, 2, 0 }, { 0, 2, 3, 0 }, { 0, 3, 4, 0 } };
    const bool polygon =
        SameTriangles(t, TopologyTriangles(TOPOLOGY_POLYGON, IDENTITY, 5, true, t, 16), POLYGON,
                      3) &&
        SameTriangles(t, TopologyTriangles(TOPOLOGY_POLYGON, IDENTITY, 5, false, t, 16), POLYGON,
                      3);
    // A line kind makes no triangles, and a quad of three vertices none either.
    const bool none = TopologyTriangles(TOPOLOGY_LINE_LOOP, IDENTITY, 5, true, t, 16) == 0u &&
                      TopologyTriangles(TOPOLOGY_QUADS, IDENTITY, 3, true, t, 16) == 0u &&
                      TopologyTriangles(TOPOLOGY_QUAD_STRIP, IDENTITY, 3, true, t, 16) == 0u;
    CHECK_TRUE(SUITE,
               "lists, strips, fans, quads, quad strips and polygons make the triangles and "
               "provoking vertices OpenGL's table gives",
               CHECK_NONE, list && strip && fan && quads && quad_strip && polygon && none,
               "list %d, strip %d, fan %d, quads %d, quad strip %d, polygon %d, none %d", list,
               strip, fan, quads, quad_strip, polygon, none);
}

static bool SameSegments(const TopologySegment *got, uint32_t got_count, const uint32_t (*want)[2],
                         uint32_t count) {
    if (got_count != count) {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (got[i].v[0] != want[i][0] || got[i].v[1] != want[i][1]) {
            return false;
        }
    }
    return true;
}

static void Segments(void) {
    TopologySegment s[16];
    static const uint32_t LOOP[4][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
    static const uint32_t STRIP[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 } };
    static const uint32_t LINES[2][2] = { { 0, 1 }, { 2, 3 } };
    // Two triangles 012 and 023 as lines: every edge of each, the diagonal 0 2 twice.
    static const uint32_t TWO[6][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 0 }, { 0, 2 }, { 2, 3 }, { 3, 0 }
    };
    static const uint32_t TWO_INDICES[6] = { 0, 1, 2, 0, 2, 3 };
    const bool loop =
        SameSegments(s, TopologySegments(TOPOLOGY_LINE_LOOP, IDENTITY, 4, s, 16), LOOP, 4);
    const bool strip =
        SameSegments(s, TopologySegments(TOPOLOGY_LINE_STRIP, IDENTITY, 4, s, 16), STRIP, 3);
    const bool lines =
        SameSegments(s, TopologySegments(TOPOLOGY_LINES, IDENTITY, 5, s, 16), LINES, 2);
    // A quad and a polygon of four corners as lines: their four sides, no diagonal.
    const bool quad =
        SameSegments(s, TopologySegments(TOPOLOGY_QUADS, IDENTITY, 4, s, 16), LOOP, 4);
    const bool polygon =
        SameSegments(s, TopologySegments(TOPOLOGY_POLYGON, IDENTITY, 4, s, 16), LOOP, 4);
    const bool two =
        SameSegments(s, TopologySegments(TOPOLOGY_TRIANGLES, TWO_INDICES, 6, s, 16), TWO, 6);
    CHECK_TRUE(SUITE,
               "line loops close, line strips do not, and quads and polygons drawn as lines show "
               "their outlines alone",
               CHECK_NONE, loop && strip && lines && quad && polygon && two,
               "loop %d, strip %d, lines %d, quad %d, polygon %d, two triangles %d", loop, strip,
               lines, quad, polygon, two);
}

static void Restarts(void) {
    TopologyRun r[8];
    static const uint32_t LIST[7] = { 0, 1, 8, 8, 2, 3, 8 };
    const uint32_t on = TopologyRuns(LIST, 7, true, 8, r, 8);
    const bool split =
        on == 2u && r[0].first == 0u && r[0].count == 2u && r[1].first == 4u && r[1].count == 2u;
    const uint32_t off = TopologyRuns(LIST, 7, false, 8, r, 8);
    const bool whole = off == 1u && r[0].first == 0u && r[0].count == 7u;
    static const uint32_t LEADING[4] = { 8, 0, 1, 2 };
    const uint32_t lead = TopologyRuns(LEADING, 4, true, 8, r, 8);
    const bool leading = lead == 1u && r[0].first == 1u && r[0].count == 3u;

    TopologyTriangle t[16];
    // The suite's two strips: four triangles with restart at 8; seven without, three through 8.
    static const uint32_t STRIPS[9] = { 0, 1, 2, 3, 8, 4, 5, 6, 7 };
    const uint32_t restarted =
        TopologyIndexedTriangles(TOPOLOGY_TRIANGLE_STRIP, STRIPS, 9, true, 8, 0, true, t, 16);
    const bool strips = restarted == 4u && t[1].v[2] == 3u && t[2].v[0] == 4u;
    const uint32_t bridged =
        TopologyIndexedTriangles(TOPOLOGY_TRIANGLE_STRIP, STRIPS, 9, false, 8, 0, true, t, 16);
    const bool bridges = bridged == 7u && t[2].v[2] == 8u && t[4].v[0] == 8u;
    // Quads restart into two quads; offset by 2, the vertex numbers move by 2.
    const uint32_t quads =
        TopologyIndexedTriangles(TOPOLOGY_QUADS, STRIPS, 9, true, 8, 2, true, t, 16);
    const bool offset = quads == 4u && t[0].v[0] == 2u && t[1].provoking == 5u && t[2].v[0] == 6u &&
                        t[3].provoking == 9u;
    CHECK_TRUE(SUITE,
               "an index list splits at its restart index, and indexed triangles add the vertex "
               "offset",
               CHECK_NONE, split && whole && leading && strips && bridges && offset,
               "split %d (%u runs), whole %d, leading %d, strips %d (%u), bridges %d (%u), "
               "offset %d (%u)",
               split, (unsigned)on, whole, leading, strips, (unsigned)restarted, bridges,
               (unsigned)bridged, offset, (unsigned)quads);
}

/// How many pixels of the target are covered by the triangles @p a and not by @p b, or the other
/// way round.
static uint32_t Differ(const TopologyPoint *points, const TopologyTriangle *a, uint32_t a_count,
                       const TopologyTriangle *b, uint32_t b_count) {
    uint32_t differ = 0;
    for (uint32_t y = 0; y < TARGET; ++y) {
        for (uint32_t x = 0; x < TARGET; ++x) {
            const bool in_a = TopologyCoveringTriangle(points, a, a_count, x, y) >= 0;
            const bool in_b = TopologyCoveringTriangle(points, b, b_count, x, y) >= 0;
            differ += in_a != in_b;
        }
    }
    return differ;
}

static uint32_t Covered(const TopologyPoint *points, const TopologyTriangle *t, uint32_t count) {
    return Differ(points, t, count, t, 0);
}

static void Coverage(void) {
    TopologyTriangle t[16], u[16];
    // The two quads: 20 by 16 and 20 by 24, 800 pixels; drawn with six vertices, the first alone.
    const uint32_t quads = TopologyTriangles(TOPOLOGY_QUADS, IDENTITY, 8, true, t, 16);
    const uint32_t one = TopologyTriangles(TOPOLOGY_QUADS, IDENTITY, 6, true, u, 16);
    const uint32_t quad_pixels = Covered(TOPOLOGY_QUADS_AT, t, quads);
    const uint32_t one_pixels = Covered(TOPOLOGY_QUADS_AT, u, one);
    const bool corners = TopologyCoveringTriangle(TOPOLOGY_QUADS_AT, t, quads, 8, 8) >= 0 &&
                         TopologyCoveringTriangle(TOPOLOGY_QUADS_AT, t, quads, 28, 8) < 0 &&
                         TopologyCoveringTriangle(TOPOLOGY_QUADS_AT, t, quads, 55, 55) >= 0 &&
                         TopologyCoveringTriangle(TOPOLOGY_QUADS_AT, t, quads, 56, 56) < 0;
    // The quad strip's three quads: 48 by 40; the polygon's rectangle: 40 by 32; the fan's 48
    // by 48.
    const uint32_t strip4 = TopologyTriangles(TOPOLOGY_QUAD_STRIP, IDENTITY, 8, true, t, 16);
    const uint32_t strip4_pixels = Covered(TOPOLOGY_QUAD_STRIP_AT, t, strip4);
    const uint32_t polygon = TopologyTriangles(TOPOLOGY_POLYGON, IDENTITY, 6, true, t, 16);
    const uint32_t polygon_pixels = Covered(TOPOLOGY_POLYGON_AT, t, polygon);
    const uint32_t fan = TopologyTriangles(TOPOLOGY_TRIANGLE_FAN, IDENTITY, 6, true, t, 16);
    const uint32_t fan_pixels = Covered(TOPOLOGY_FAN_AT, t, fan);
    // A polygon taken as a strip instead of a fan leaves part of its rectangle out.
    const uint32_t as_strip = TopologyTriangles(TOPOLOGY_TRIANGLE_STRIP, IDENTITY, 6, true, u, 16);
    const uint32_t strip_misses = Differ(TOPOLOGY_POLYGON_AT, t, polygon, u, as_strip);
    CHECK_TRUE(SUITE,
               "the topology suite's quads, quad strip, polygon and fan cover exactly their "
               "rectangles",
               CHECK_NONE,
               quad_pixels == 800u && one_pixels == 320u && corners && strip4_pixels == 1920u &&
                   polygon_pixels == 1280u && fan_pixels == 2304u && strip_misses >= 100u,
               "quads %u (800), six vertices %u (320), corners %d, quad strip %u (1920), polygon "
               "%u (1280), fan %u (2304), the polygon as a strip differs at %u (100 or more)",
               (unsigned)quad_pixels, (unsigned)one_pixels, corners, (unsigned)strip4_pixels,
               (unsigned)polygon_pixels, (unsigned)fan_pixels, (unsigned)strip_misses);
}

static void Margins(void) {
    // Each vertex its own colour, as the cases that tell vertices apart draw them.
    static const uint32_t EACH[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const uint32_t ONE[16] = { 0 };
    TopologyTriangle t[16];
    double nearest = 1.0e9;
    const char *where = "";
#define MARGIN(name, kind, points, n, last, colours)                                               \
    do {                                                                                           \
        const uint32_t count = TopologyTriangles(kind, IDENTITY, n, last, t, 16);                  \
        const double m = TopologyColourMargin(points, colours, t, count);                          \
        if (m < nearest) {                                                                         \
            nearest = m;                                                                           \
            where = name;                                                                          \
        }                                                                                          \
    } while (0)
    MARGIN("quads", TOPOLOGY_QUADS, TOPOLOGY_QUADS_AT, 8, true, EACH);
    MARGIN("quads, first", TOPOLOGY_QUADS, TOPOLOGY_QUADS_AT, 8, false, EACH);
    MARGIN("quad strip", TOPOLOGY_QUAD_STRIP, TOPOLOGY_QUAD_STRIP_AT, 8, true, EACH);
    MARGIN("polygon", TOPOLOGY_POLYGON, TOPOLOGY_POLYGON_AT, 6, true, ONE);
    MARGIN("fan", TOPOLOGY_TRIANGLE_FAN, TOPOLOGY_FAN_AT, 6, true, EACH);
    MARGIN("fan, first", TOPOLOGY_TRIANGLE_FAN, TOPOLOGY_FAN_AT, 6, false, EACH);
    MARGIN("strip", TOPOLOGY_TRIANGLE_STRIP, TOPOLOGY_STRIP_AT, 6, true, EACH);
    MARGIN("strip, first", TOPOLOGY_TRIANGLE_STRIP, TOPOLOGY_STRIP_AT, 6, false, EACH);
#undef MARGIN
    // The restart vertices with restart off: one strip through vertex 8, one colour.
    static const uint32_t STRIPS[9] = { 0, 1, 2, 3, 8, 4, 5, 6, 7 };
    const uint32_t bridged =
        TopologyIndexedTriangles(TOPOLOGY_TRIANGLE_STRIP, STRIPS, 9, false, 8, 0, true, t, 16);
    const double bridge = TopologyColourMargin(TOPOLOGY_RESTART_AT, ONE, t, bridged);
    if (bridge < nearest) {
        nearest = bridge;
        where = "strip through vertex 8";
    }
    // A twentieth of a pixel is thirteen steps of the 3D class's eight bit sub pixel grid.
    CHECK_TRUE(SUITE,
               "every edge where the topology suite's colour changes keeps a twentieth of a pixel "
               "from every pixel centre",
               CHECK_NONE, nearest >= 0.05, "the nearest centre is %.4f px from an edge of the %s",
               nearest, where);
}

static void Lines(void) {
    TopologySegment s[16];
    const uint32_t loop = TopologySegments(TOPOLOGY_LINE_LOOP, IDENTITY, 4, s, 16);
    // The closing side runs down column 12: drawn in the loop's middle, judged neither way at its
    // corners, and clear for the strip; a pixel well inside is clear for both.
    const bool loop_side =
        TopologyLineJudge(TOPOLOGY_OUTLINE_AT, s, loop, 12, 30) == TOPOLOGY_LINE_ON &&
        TopologyLineJudge(TOPOLOGY_OUTLINE_AT, s, loop, 12, 16) == TOPOLOGY_LINE_EITHER &&
        TopologyLineJudge(TOPOLOGY_OUTLINE_AT, s, loop, 30, 30) == TOPOLOGY_LINE_OFF &&
        TopologyLineJudge(TOPOLOGY_OUTLINE_AT, s, loop, 30, 16) == TOPOLOGY_LINE_ON &&
        TopologyLineJudge(TOPOLOGY_OUTLINE_AT, s, loop, 30, 15) == TOPOLOGY_LINE_EITHER;
    const uint32_t strip = TopologySegments(TOPOLOGY_LINE_STRIP, IDENTITY, 4, s, 16);
    const bool strip_side =
        TopologyLineJudge(TOPOLOGY_OUTLINE_AT, s, strip, 12, 30) == TOPOLOGY_LINE_OFF;
    // A quad drawn as lines: the pixels its diagonal would draw are judged clear; two triangles
    // drawn as lines judge none of them clear, since the diagonal is theirs.
    const uint32_t quad = TopologySegments(TOPOLOGY_QUADS, IDENTITY, 4, s, 16);
    const uint32_t stray = TopologyStrayPixels(TOPOLOGY_OUTLINE_AT, s, quad, TOPOLOGY_OUTLINE_AT[0],
                                               TOPOLOGY_OUTLINE_AT[2]);
    static const uint32_t TWO[6] = { 0, 1, 2, 0, 2, 3 };
    const uint32_t two = TopologySegments(TOPOLOGY_TRIANGLES, TWO, 6, s, 16);
    const uint32_t shared = TopologyStrayPixels(TOPOLOGY_OUTLINE_AT, s, two, TOPOLOGY_OUTLINE_AT[0],
                                                TOPOLOGY_OUTLINE_AT[2]);
    CHECK_TRUE(SUITE,
               "a line loop's closing side is judged drawn, a line strip's clear, and a quad's "
               "diagonal clear when drawn as lines",
               CHECK_NONE, loop_side && strip_side && stray >= 20u && shared == 0u,
               "loop %d, strip %d, the quad's diagonal %u pixels judged clear (20 or more), the "
               "two triangles' %u (none)",
               loop_side, strip_side, (unsigned)stray, (unsigned)shared);
}

static void Bridges(void) {
    // What a missed restart and a quad draw that ran past its count would add to the picture.
    static const uint32_t STRIPS[9] = { 0, 1, 2, 3, 8, 4, 5, 6, 7 };
    TopologyTriangle on[16], off[16];
    const uint32_t restarted =
        TopologyIndexedTriangles(TOPOLOGY_TRIANGLE_STRIP, STRIPS, 9, true, 8, 0, true, on, 16);
    const uint32_t bridged =
        TopologyIndexedTriangles(TOPOLOGY_TRIANGLE_STRIP, STRIPS, 9, false, 8, 0, true, off, 16);
    const uint32_t bridge = Differ(TOPOLOGY_RESTART_AT, on, restarted, off, bridged);
    static const uint32_t FANS[9] = { 0, 1, 3, 2, 8, 4, 5, 7, 6 };
    const uint32_t fans =
        TopologyIndexedTriangles(TOPOLOGY_TRIANGLE_FAN, FANS, 9, true, 8, 0, true, on, 16);
    const uint32_t fan_bridged =
        TopologyIndexedTriangles(TOPOLOGY_TRIANGLE_FAN, FANS, 9, false, 8, 0, true, off, 16);
    const uint32_t fan_bridge = Differ(TOPOLOGY_RESTART_AT, on, fans, off, fan_bridged);
    const uint32_t fan_squares = Covered(TOPOLOGY_RESTART_AT, on, fans);
    CHECK_TRUE(SUITE,
               "a missed restart in the topology suite's strips and fans draws pixels the cases "
               "judge clear",
               CHECK_NONE, bridge >= 200u && fan_bridge >= 200u && fan_squares == 760u,
               "the strips' bridges cover %u pixels (200 or more), the fans' %u (200 or more); "
               "the two fans cover %u (760)",
               (unsigned)bridge, (unsigned)fan_bridge, (unsigned)fan_squares);
}

void RunReferenceTopology(void) {
    Triangles();
    Segments();
    Restarts();
    Coverage();
    Margins();
    Lines();
    Bridges();
}
