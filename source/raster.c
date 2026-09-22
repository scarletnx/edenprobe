// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>

#include "raster.h"

// The coverage rule, derived rather than recalled, because getting it backwards would make every
// rasterisation case wrong in the same direction. Work in target space, order the vertices so
// the edge function E(p) = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) is positive
// inside for all three edges, and a centre on a shared edge has to go to exactly one triangle or
// that edge draws twice or not at all. The convention the graphics APIs agree on gives it to the
// triangle the edge is a top or a left edge of, and the reference suite pins one hand worked
// point per branch.

static double EdgeFunction(RasterPoint a, RasterPoint b, double x, double y) {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

/// Whether the edge from @p a to @p b takes the centres that land exactly on it.
static bool EdgeTakesItsOwnLine(RasterPoint a, RasterPoint b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    return dy < 0.0 || (dy == 0.0 && dx > 0.0);
}

RasterPoint RasterProject(const RasterViewport *viewport, float clip_x, float clip_y) {
    // The transform deko3d programs: a scale of half the viewport and an offset of its centre,
    // with the vertical scale negated unless the device was made with the lower left origin
    // flag, which this suite does not ask for. So clip space plus one in y is the top row.
    const double half_width = (double)viewport->width * 0.5;
    const double half_height = (double)viewport->height * 0.5;
    RasterPoint point;
    point.x = (double)viewport->x + half_width + (double)clip_x * half_width;
    point.y = (double)viewport->y + half_height - (double)clip_y * half_height;
    return point;
}

void RasterTriangleFromPoints(RasterTriangle *out, const RasterPoint points[3]) {
    out->v[0] = points[0];
    out->v[1] = points[1];
    out->v[2] = points[2];
    out->flipped = false;

    double area = EdgeFunction(out->v[0], out->v[1], out->v[2].x, out->v[2].y);
    if (area < 0.0) {
        // Swapping two vertices puts the interior on the positive side of every edge without
        // moving the triangle, so a case cannot fail over the order its corners arrived in.
        const RasterPoint swap = out->v[1];
        out->v[1] = out->v[2];
        out->v[2] = swap;
        area = -area;
        out->flipped = true;
    }
    out->area = area;
}

void RasterTriangleFromClip(RasterTriangle *out, const RasterViewport *viewport,
                            const float clip[6]) {
    const RasterPoint points[3] = {
        RasterProject(viewport, clip[0], clip[1]),
        RasterProject(viewport, clip[2], clip[3]),
        RasterProject(viewport, clip[4], clip[5]),
    };
    RasterTriangleFromPoints(out, points);
}

bool RasterCovers(const RasterTriangle *triangle, double x, double y) {
    if (!(triangle->area > 0.0)) {
        // Without this the loop below would call every point on the line covered, because every
        // edge function is zero there.
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const RasterPoint a = triangle->v[i];
        const RasterPoint b = triangle->v[(i + 1) % 3];
        const double e = EdgeFunction(a, b, x, y);
        if (e > 0.0) {
            continue;
        }
        if (e < 0.0) {
            return false;
        }
        if (!EdgeTakesItsOwnLine(a, b)) {
            return false;
        }
    }
    return true;
}

double RasterEdgeDistance(const RasterTriangle *triangle, double x, double y) {
    double nearest = -1.0;
    for (int i = 0; i < 3; ++i) {
        const RasterPoint a = triangle->v[i];
        const RasterPoint b = triangle->v[(i + 1) % 3];
        const double length = sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        if (length == 0.0) {
            continue;
        }
        // The edge function is twice the area of the triangle the point makes with the edge, so
        // dividing by the edge's length gives the perpendicular distance in pixels.
        const double distance = fabs(EdgeFunction(a, b, x, y)) / length;
        if (nearest < 0.0 || distance < nearest) {
            nearest = distance;
        }
    }
    return nearest < 0.0 ? 0.0 : nearest;
}

RasterVerdict RasterClassifyPixel(const RasterTriangle *triangle, uint32_t px, uint32_t py,
                                  double margin) {
    // The centre, which is where coverage is decided. The multisample state every pass binds
    // puts the single sample there, so this is not an assumption, it is the state the pass set.
    const double x = (double)px + 0.5;
    const double y = (double)py + 0.5;

    if (margin > 0.0 && RasterEdgeDistance(triangle, x, y) < margin) {
        return RASTER_NEAR_EDGE;
    }
    return RasterCovers(triangle, x, y) ? RASTER_INSIDE : RASTER_OUTSIDE;
}
