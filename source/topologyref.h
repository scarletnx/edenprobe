// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The topology suite's rules: which triangles and which line segments a primitive type makes of a
/// list of vertices, which vertex gives a flat output its value, where an index buffer restarts,
/// and the pictures the suite's geometry leaves on a 64 by 64 target.
///
/// Touches no graphics library, so the reference suite pins all of it with no device present.

/// The primitive types the suite draws, in the 3D class's own numbering (deko3d's DkPrimitive).
typedef enum {
    TOPOLOGY_LINES = 1,
    TOPOLOGY_LINE_LOOP = 2,
    TOPOLOGY_LINE_STRIP = 3,
    TOPOLOGY_TRIANGLES = 4,
    TOPOLOGY_TRIANGLE_STRIP = 5,
    TOPOLOGY_TRIANGLE_FAN = 6,
    TOPOLOGY_QUADS = 7,
    TOPOLOGY_QUAD_STRIP = 8,
    TOPOLOGY_POLYGON = 9,
} TopologyKind;

/// One triangle a primitive makes: three vertex numbers, as the draw names them, and the one whose
/// value a flat output takes.
typedef struct {
    uint32_t v[3];
    uint32_t provoking;
} TopologyTriangle;

/// One line segment: the two vertex numbers it joins.
typedef struct {
    uint32_t v[2];
} TopologySegment;

/// A stretch of an index list between two restart indices: where it starts and how many indices
/// it holds.
typedef struct {
    uint32_t first;
    uint32_t count;
} TopologyRun;

/// A position on the target, in pixels: x to the right, y downwards, the first pixel's centre at
/// (0.5, 0.5), as raster.h has it.
typedef struct {
    double x;
    double y;
} TopologyPoint;

/// What the judge holds a pixel to in a case that draws lines.
typedef enum {
    /// Its centre is on a segment, a pixel or more from both its ends: drawn.
    TOPOLOGY_LINE_ON = 0,
    /// Its centre is more than a pixel from every segment: clear.
    TOPOLOGY_LINE_OFF = 1,
    /// Near an end or beside a segment, where line rules may differ: not judged.
    TOPOLOGY_LINE_EITHER = 2,
} TopologyLinePixel;

/// The triangles @p kind makes of the @p count vertex numbers at @p vertices, in the order the
/// primitives come, at most @p max of them into @p out; how many it made. A primitive whose
/// vertices run out is left out, as OpenGL drops an incomplete one. The provoking vertex follows
/// OpenGL's table for @p last (the last vertex convention) or the first vertex one, with quads
/// following the convention and a polygon taking its first vertex either way. Only the triangle
/// kinds, quads and polygons: a line kind makes none.
uint32_t TopologyTriangles(TopologyKind kind, const uint32_t *vertices, uint32_t count, bool last,
                           TopologyTriangle *out, uint32_t max);

/// The segments @p kind draws of the @p count vertex numbers at @p vertices: a line kind's lines,
/// and for a triangle kind, quads or a polygon drawn as lines (the polygon mode Line) each
/// primitive's outline, with no edge inside a quad or a polygon. At most @p max into @p out.
uint32_t TopologySegments(TopologyKind kind, const uint32_t *vertices, uint32_t count,
                          TopologySegment *out, uint32_t max);

/// The runs @p count indices split into at each index equal to @p restart_index, when @p restart
/// is on: the restart indices themselves belong to no run, and an empty run is left out. With
/// restart off the whole list is one run. At most @p max into @p out; how many there are.
uint32_t TopologyRuns(const uint32_t *indices, uint32_t count, bool restart, uint32_t restart_index,
                      TopologyRun *out, uint32_t max);

/// The triangles a whole indexed draw makes: @p indices split into runs by the restart index, each
/// run taken by TopologyTriangles on its own, the vertex numbers being the index values plus
/// @p vertex_offset. At most @p max into @p out.
uint32_t TopologyIndexedTriangles(TopologyKind kind, const uint32_t *indices, uint32_t count,
                                  bool restart, uint32_t restart_index, int32_t vertex_offset,
                                  bool last, TopologyTriangle *out, uint32_t max);

/// The last of the @p count triangles whose interior holds the centre of pixel (@p x, @p y), by
/// raster.c's coverage rule, with each triangle's corners at @p points; -1 for none.
int32_t TopologyCoveringTriangle(const TopologyPoint *points, const TopologyTriangle *triangles,
                                 uint32_t count, uint32_t x, uint32_t y);

/// How close a pixel centre of a 64 by 64 target comes to an edge where the picture changes: an
/// edge of one of the @p count triangles that no other of them shares with the same @p colours
/// value on its provoking vertex. The cases are exact only where this is well clear of nought.
double TopologyColourMargin(const TopologyPoint *points, const uint32_t *colours,
                            const TopologyTriangle *triangles, uint32_t count);

/// What pixel (@p x, @p y) is held to when the @p count segments are drawn with corners at
/// @p points, one pixel wide.
TopologyLinePixel TopologyLineJudge(const TopologyPoint *points, const TopologySegment *segments,
                                    uint32_t count, uint32_t x, uint32_t y);

/// How many pixel centres of a 64 by 64 target lie within a quarter of a pixel of the segment from
/// @p a to @p b and are judged clear when the @p count segments are drawn: the pixels a stray
/// diagonal would show up on.
uint32_t TopologyStrayPixels(const TopologyPoint *points, const TopologySegment *segments,
                             uint32_t count, TopologyPoint a, TopologyPoint b);

// The suite's geometry, in target pixels, here so the reference suite checks the very positions
// the draws carry.

/// Two quads, their corners in the order a quad is wound: (8, 8) to (28, 24) and (36, 32) to
/// (56, 56).
#define TOPOLOGY_QUAD_POINTS 8u
extern const TopologyPoint TOPOLOGY_QUADS_AT[TOPOLOGY_QUAD_POINTS];

/// A quad strip of three quads, its pairs top then bottom at x 4, 20, 36 and 52, rows 12 to 52.
#define TOPOLOGY_QUAD_STRIP_POINTS 8u
extern const TopologyPoint TOPOLOGY_QUAD_STRIP_AT[TOPOLOGY_QUAD_STRIP_POINTS];

/// A polygon of six corners around the rectangle (12, 16) to (52, 48): its four corners and a
/// point in the middle of its top and its left edges.
#define TOPOLOGY_POLYGON_POINTS 6u
extern const TopologyPoint TOPOLOGY_POLYGON_AT[TOPOLOGY_POLYGON_POINTS];

/// A triangle strip of four triangles over (4, 8) to (52, 56), its vertices top then bottom at x
/// 4, 28 and 52: the diagonals inside run two rows for each column, so none passes within a fifth
/// of a pixel of a centre.
#define TOPOLOGY_STRIP_POINTS 6u
extern const TopologyPoint TOPOLOGY_STRIP_AT[TOPOLOGY_STRIP_POINTS];

/// A triangle fan of four triangles over (8, 8) to (56, 56), its hub at (32, 20) and its rim the
/// four corners with the first repeated.
#define TOPOLOGY_FAN_POINTS 6u
extern const TopologyPoint TOPOLOGY_FAN_AT[TOPOLOGY_FAN_POINTS];

/// The restart cases' vertices: two strips of two triangles, (8, 8) to (28, 28) from vertices 0
/// to 3 and (36, 38) to (56, 56) from 4 to 7, and vertex 8 at (60, 8), which every bridge a
/// missed restart makes reaches for. The bridges' outer edges keep a thirteenth of a pixel from
/// every centre too, so the case with restart off is exact as well.
#define TOPOLOGY_RESTART_POINTS 9u
extern const TopologyPoint TOPOLOGY_RESTART_AT[TOPOLOGY_RESTART_POINTS];

/// A rectangle with its corners on pixel centres, (12.5, 16.5) to (51.5, 47.5), for the line cases:
/// as a loop, a strip, and the outline of a quad or a polygon drawn as lines.
#define TOPOLOGY_OUTLINE_POINTS 4u
extern const TopologyPoint TOPOLOGY_OUTLINE_AT[TOPOLOGY_OUTLINE_POINTS];

#ifdef __cplusplus
}
#endif
