// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>

#include "raster.h"
#include "topologyref.h"

// The rules are OpenGL's (4.6 compatibility profile, 10.1 and table 10.2), which the 3D class
// follows for the primitive types it shares with it and Vulkan lacks: quads, quad strips,
// polygons and line loops, and a restart index of any value. Vulkan has no quads, no polygon, no
// line loop, and restarts only at the all-ones index of the index's width, so an emulator makes
// each of these out of something else, and the cases are where that remaking goes wrong.

#define TARGET 64u
/// The longest stretch between two restart indices TopologyIndexedTriangles takes: more than any
/// case draws.
#define MAX_RUN 256u

static uint32_t Put(TopologyTriangle *out, uint32_t made, uint32_t max, uint32_t a, uint32_t b,
                    uint32_t c, uint32_t provoking) {
    if (made < max) {
        out[made].v[0] = a;
        out[made].v[1] = b;
        out[made].v[2] = c;
        out[made].provoking = provoking;
    }
    return made + 1u;
}

uint32_t TopologyTriangles(TopologyKind kind, const uint32_t *vertices, uint32_t count, bool last,
                           TopologyTriangle *out, uint32_t max) {
    const uint32_t *const v = vertices;
    uint32_t made = 0;
    switch (kind) {
    case TOPOLOGY_TRIANGLES:
        for (uint32_t i = 0; i + 3u <= count; i += 3u) {
            made = Put(out, made, max, v[i], v[i + 1u], v[i + 2u], last ? v[i + 2u] : v[i]);
        }
        break;
    case TOPOLOGY_TRIANGLE_STRIP:
        // Triangle i is vertices i to i + 2, every other one wound the other way, which decides
        // nothing here: coverage does not depend on the order a triangle's corners come in.
        for (uint32_t i = 0; i + 3u <= count; ++i) {
            made = Put(out, made, max, v[i], v[i + 1u], v[i + 2u], last ? v[i + 2u] : v[i]);
        }
        break;
    case TOPOLOGY_TRIANGLE_FAN:
        // Triangle i is the hub and rim vertices i + 1 and i + 2; the first vertex convention
        // gives it i + 1, not the hub (table 10.2, and Vulkan's own rule for a fan).
        for (uint32_t i = 0; i + 3u <= count; ++i) {
            made = Put(out, made, max, v[0], v[i + 1u], v[i + 2u], last ? v[i + 2u] : v[i + 1u]);
        }
        break;
    case TOPOLOGY_QUADS:
        // Any split of a convex quad covers the same pixels, so the one here only has to agree
        // with itself: both halves carry the quad's provoking vertex, its fourth or its first.
        for (uint32_t i = 0; i + 4u <= count; i += 4u) {
            const uint32_t provoking = last ? v[i + 3u] : v[i];
            made = Put(out, made, max, v[i], v[i + 1u], v[i + 2u], provoking);
            made = Put(out, made, max, v[i], v[i + 2u], v[i + 3u], provoking);
        }
        break;
    case TOPOLOGY_QUAD_STRIP:
        // Quad i is 2i, 2i + 1, 2i + 3 and 2i + 2 around its edge: the pairs run along the strip.
        for (uint32_t i = 0; i + 4u <= count; i += 2u) {
            const uint32_t provoking = last ? v[i + 3u] : v[i];
            made = Put(out, made, max, v[i], v[i + 1u], v[i + 3u], provoking);
            made = Put(out, made, max, v[i], v[i + 3u], v[i + 2u], provoking);
        }
        break;
    case TOPOLOGY_POLYGON:
        // One convex polygon, as a fan from its first vertex, which is also its provoking vertex
        // under either convention.
        for (uint32_t i = 0; i + 3u <= count; ++i) {
            made = Put(out, made, max, v[0], v[i + 1u], v[i + 2u], v[0]);
        }
        break;
    default:
        break;
    }
    return made < max ? made : max;
}

static uint32_t PutSegment(TopologySegment *out, uint32_t made, uint32_t max, uint32_t a,
                           uint32_t b) {
    if (made < max) {
        out[made].v[0] = a;
        out[made].v[1] = b;
    }
    return made + 1u;
}

/// The outline of the polygon @p corners vertex numbers at @p v make, in order and closed.
static uint32_t Outline(TopologySegment *out, uint32_t made, uint32_t max, const uint32_t *v,
                        uint32_t corners) {
    for (uint32_t i = 0; i < corners; ++i) {
        made = PutSegment(out, made, max, v[i], v[(i + 1u) % corners]);
    }
    return made;
}

uint32_t TopologySegments(TopologyKind kind, const uint32_t *vertices, uint32_t count,
                          TopologySegment *out, uint32_t max) {
    const uint32_t *const v = vertices;
    uint32_t made = 0;
    switch (kind) {
    case TOPOLOGY_LINES:
        for (uint32_t i = 0; i + 2u <= count; i += 2u) {
            made = PutSegment(out, made, max, v[i], v[i + 1u]);
        }
        break;
    case TOPOLOGY_LINE_STRIP:
    case TOPOLOGY_LINE_LOOP:
        for (uint32_t i = 0; i + 2u <= count; ++i) {
            made = PutSegment(out, made, max, v[i], v[i + 1u]);
        }
        if (kind == TOPOLOGY_LINE_LOOP && count >= 2u) {
            made = PutSegment(out, made, max, v[count - 1u], v[0]);
        }
        break;
    case TOPOLOGY_TRIANGLES:
    case TOPOLOGY_TRIANGLE_STRIP:
    case TOPOLOGY_TRIANGLE_FAN: {
        TopologyTriangle triangles[MAX_RUN];
        const uint32_t n = TopologyTriangles(kind, v, count, true, triangles, MAX_RUN);
        for (uint32_t i = 0; i < n; ++i) {
            made = Outline(out, made, max, triangles[i].v, 3u);
        }
        break;
    }
    case TOPOLOGY_QUADS:
        for (uint32_t i = 0; i + 4u <= count; i += 4u) {
            made = Outline(out, made, max, v + i, 4u);
        }
        break;
    case TOPOLOGY_QUAD_STRIP:
        for (uint32_t i = 0; i + 4u <= count; i += 2u) {
            const uint32_t quad[4] = { v[i], v[i + 1u], v[i + 3u], v[i + 2u] };
            made = Outline(out, made, max, quad, 4u);
        }
        break;
    case TOPOLOGY_POLYGON:
        if (count >= 3u) {
            made = Outline(out, made, max, v, count);
        }
        break;
    default:
        break;
    }
    return made < max ? made : max;
}

uint32_t TopologyRuns(const uint32_t *indices, uint32_t count, bool restart, uint32_t restart_index,
                      TopologyRun *out, uint32_t max) {
    uint32_t made = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i <= count; ++i) {
        const bool end = i == count || (restart && indices[i] == restart_index);
        if (!end) {
            continue;
        }
        if (i > first) {
            if (made < max) {
                out[made].first = first;
                out[made].count = i - first;
            }
            ++made;
        }
        first = i + 1u;
    }
    return made < max ? made : max;
}

uint32_t TopologyIndexedTriangles(TopologyKind kind, const uint32_t *indices, uint32_t count,
                                  bool restart, uint32_t restart_index, int32_t vertex_offset,
                                  bool last, TopologyTriangle *out, uint32_t max) {
    TopologyRun runs[MAX_RUN];
    const uint32_t run_count = TopologyRuns(indices, count, restart, restart_index, runs, MAX_RUN);
    uint32_t made = 0;
    for (uint32_t r = 0; r < run_count && made < max; ++r) {
        uint32_t mapped[MAX_RUN];
        const uint32_t length = runs[r].count < MAX_RUN ? runs[r].count : MAX_RUN;
        for (uint32_t i = 0; i < length; ++i) {
            mapped[i] = (uint32_t)((int64_t)indices[runs[r].first + i] + vertex_offset);
        }
        made += TopologyTriangles(kind, mapped, length, last, out + made, max - made);
    }
    return made;
}

static RasterPoint Point(TopologyPoint p) {
    RasterPoint r;
    r.x = p.x;
    r.y = p.y;
    return r;
}

int32_t TopologyCoveringTriangle(const TopologyPoint *points, const TopologyTriangle *triangles,
                                 uint32_t count, uint32_t x, uint32_t y) {
    int32_t found = -1;
    for (uint32_t i = 0; i < count; ++i) {
        const RasterPoint corners[3] = { Point(points[triangles[i].v[0]]),
                                         Point(points[triangles[i].v[1]]),
                                         Point(points[triangles[i].v[2]]) };
        RasterTriangle triangle;
        RasterTriangleFromPoints(&triangle, corners);
        if (RasterCovers(&triangle, (double)x + 0.5, (double)y + 0.5)) {
            found = (int32_t)i;
        }
    }
    return found;
}

/// The distance from (@p x, @p y) to the segment from @p a to @p b, and in @p along how far its
/// foot is from the nearer end, negative when it falls outside the segment.
static double SegmentDistance(TopologyPoint a, TopologyPoint b, double x, double y, double *along) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = sqrt(dx * dx + dy * dy);
    if (length == 0.0) {
        *along = -1.0;
        return sqrt((x - a.x) * (x - a.x) + (y - a.y) * (y - a.y));
    }
    const double t = ((x - a.x) * dx + (y - a.y) * dy) / length;
    if (t < 0.0) {
        *along = t;
        return sqrt((x - a.x) * (x - a.x) + (y - a.y) * (y - a.y));
    }
    if (t > length) {
        *along = length - t;
        return sqrt((x - b.x) * (x - b.x) + (y - b.y) * (y - b.y));
    }
    *along = t < length - t ? t : length - t;
    return fabs((x - a.x) * dy - (y - a.y) * dx) / length;
}

static bool SamePoint(TopologyPoint a, TopologyPoint b) {
    return a.x == b.x && a.y == b.y;
}

/// Whether triangle @p t's edge from corner @p e to the next is also an edge of another triangle
/// whose provoking vertex has the same colour, so the picture does not change across it.
static bool EdgeHidden(const TopologyPoint *points, const uint32_t *colours,
                       const TopologyTriangle *triangles, uint32_t count, uint32_t t, uint32_t e) {
    const TopologyPoint a = points[triangles[t].v[e]];
    const TopologyPoint b = points[triangles[t].v[(e + 1u) % 3u]];
    for (uint32_t u = 0; u < count; ++u) {
        if (u == t || colours[triangles[u].provoking] != colours[triangles[t].provoking]) {
            continue;
        }
        for (uint32_t f = 0; f < 3u; ++f) {
            const TopologyPoint c = points[triangles[u].v[f]];
            const TopologyPoint d = points[triangles[u].v[(f + 1u) % 3u]];
            if ((SamePoint(a, c) && SamePoint(b, d)) || (SamePoint(a, d) && SamePoint(b, c))) {
                return true;
            }
        }
    }
    return false;
}

double TopologyColourMargin(const TopologyPoint *points, const uint32_t *colours,
                            const TopologyTriangle *triangles, uint32_t count) {
    double nearest = 1.0e9;
    for (uint32_t t = 0; t < count; ++t) {
        for (uint32_t e = 0; e < 3u; ++e) {
            if (EdgeHidden(points, colours, triangles, count, t, e)) {
                continue;
            }
            const TopologyPoint a = points[triangles[t].v[e]];
            const TopologyPoint b = points[triangles[t].v[(e + 1u) % 3u]];
            for (uint32_t y = 0; y < TARGET; ++y) {
                for (uint32_t x = 0; x < TARGET; ++x) {
                    double along;
                    const double d = SegmentDistance(a, b, x + 0.5, y + 0.5, &along);
                    nearest = d < nearest ? d : nearest;
                }
            }
        }
    }
    return nearest;
}

// A one pixel line: OpenGL's diamond exit rule, Vulkan's Bresenham and its rectangular lines all
// draw a pixel whose centre is within a quarter of a pixel of the line, away from its ends (the
// diamond's inscribed circle has a radius of 0.35, and an x major line draws the centre in each
// column within half a pixel vertically, which a quarter perpendicular always is for a slope up to
// one), and none draws a pixel whose centre is more than a pixel away. What lies between is left
// to each rule.
#define LINE_ON 0.25
#define LINE_OFF 1.0

TopologyLinePixel TopologyLineJudge(const TopologyPoint *points, const TopologySegment *segments,
                                    uint32_t count, uint32_t x, uint32_t y) {
    double nearest = 1.0e9;
    for (uint32_t i = 0; i < count; ++i) {
        double along;
        const double d = SegmentDistance(points[segments[i].v[0]], points[segments[i].v[1]],
                                         x + 0.5, y + 0.5, &along);
        if (d <= LINE_ON && along >= 1.0) {
            return TOPOLOGY_LINE_ON;
        }
        nearest = d < nearest ? d : nearest;
    }
    return nearest > LINE_OFF ? TOPOLOGY_LINE_OFF : TOPOLOGY_LINE_EITHER;
}

uint32_t TopologyStrayPixels(const TopologyPoint *points, const TopologySegment *segments,
                             uint32_t count, TopologyPoint a, TopologyPoint b) {
    uint32_t stray = 0;
    for (uint32_t y = 0; y < TARGET; ++y) {
        for (uint32_t x = 0; x < TARGET; ++x) {
            double along;
            const double d = SegmentDistance(a, b, x + 0.5, y + 0.5, &along);
            if (d <= LINE_ON && along >= 1.0 &&
                TopologyLineJudge(points, segments, count, x, y) == TOPOLOGY_LINE_OFF) {
                ++stray;
            }
        }
    }
    return stray;
}

const TopologyPoint TOPOLOGY_QUADS_AT[TOPOLOGY_QUAD_POINTS] = {
    { 8.0, 8.0 },   { 28.0, 8.0 },  { 28.0, 24.0 }, { 8.0, 24.0 },
    { 36.0, 32.0 }, { 56.0, 32.0 }, { 56.0, 56.0 }, { 36.0, 56.0 },
};

const TopologyPoint TOPOLOGY_QUAD_STRIP_AT[TOPOLOGY_QUAD_STRIP_POINTS] = {
    { 4.0, 12.0 },  { 4.0, 52.0 },  { 20.0, 12.0 }, { 20.0, 52.0 },
    { 36.0, 12.0 }, { 36.0, 52.0 }, { 52.0, 12.0 }, { 52.0, 52.0 },
};

const TopologyPoint TOPOLOGY_POLYGON_AT[TOPOLOGY_POLYGON_POINTS] = {
    { 12.0, 16.0 }, { 32.0, 16.0 }, { 52.0, 16.0 }, { 52.0, 48.0 }, { 12.0, 48.0 }, { 12.0, 32.0 },
};

// An edge through whole pixel corners with a direction of p across and q down, p and q sharing no
// factor, passes q * i - p * j + (q - p) / 2 from the line through a centre, in units of the
// direction's length: half of one over that length when q - p is odd, and through centres when
// it is even. The diagonals here go 1 across for 2 down (a fifth of a pixel and more), and the
// fan's 2 across for 1 down and 2 for 3 (0.139, one over twice the square root of 13).
const TopologyPoint TOPOLOGY_STRIP_AT[TOPOLOGY_STRIP_POINTS] = {
    { 4.0, 8.0 }, { 4.0, 56.0 }, { 28.0, 8.0 }, { 28.0, 56.0 }, { 52.0, 8.0 }, { 52.0, 56.0 },
};

const TopologyPoint TOPOLOGY_FAN_AT[TOPOLOGY_FAN_POINTS] = {
    { 32.0, 20.0 }, { 8.0, 8.0 }, { 56.0, 8.0 }, { 56.0, 56.0 }, { 8.0, 56.0 }, { 8.0, 8.0 },
};

const TopologyPoint TOPOLOGY_RESTART_AT[TOPOLOGY_RESTART_POINTS] = {
    { 8.0, 8.0 },   { 8.0, 28.0 },  { 28.0, 8.0 },  { 28.0, 28.0 }, { 36.0, 38.0 },
    { 36.0, 56.0 }, { 56.0, 38.0 }, { 56.0, 56.0 }, { 60.0, 8.0 },
};

const TopologyPoint TOPOLOGY_OUTLINE_AT[TOPOLOGY_OUTLINE_POINTS] = {
    { 12.5, 16.5 },
    { 51.5, 16.5 },
    { 51.5, 47.5 },
    { 12.5, 47.5 },
};
