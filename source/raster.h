// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Where a clip space triangle lands on the target, and which pixels it covers.
///
/// Touches no graphics library, so the reference suite can exercise it with no device present.

/// The rectangle a clip space position is mapped into, in the same coordinates the fragment
/// coordinate uses: x to the right, y downwards, the first pixel's centre at (0.5, 0.5).
typedef struct {
    float x;
    float y;
    float width;
    float height;
} RasterViewport;

typedef struct {
    double x;
    double y;
} RasterPoint;

/// A triangle in that space, with its vertices already in the order the coverage rule wants.
typedef struct {
    RasterPoint v[3];
    /// Twice the signed area as the vertices were given. Zero means degenerate, and a
    /// degenerate triangle covers nothing.
    double area;
    /// True when the vertices had to be swapped to put the interior on the positive side of
    /// every edge function. Carried so a caller can see which way round the input was.
    bool flipped;
} RasterTriangle;

typedef enum {
    /// The centre is outside, and far enough from every edge that no legal implementation can
    /// disagree. Checked exactly.
    RASTER_OUTSIDE = 0,
    /// The centre is inside, by the same margin. Checked exactly.
    RASTER_INSIDE = 1,
    /// Within the margin of an edge. The right answer here is whatever the hardware's sub pixel
    /// precision produces, which is a limit each implementation reports rather than a constant,
    /// so the case allows either and counts how many pixels it let through.
    RASTER_NEAR_EDGE = 2,
} RasterVerdict;

/// The geometry the rasterisation suite draws, in clip space, as three x and y pairs.
///
/// Here rather than in the suite, so the reference suite projects the very values a draw carries
/// rather than a restatement of where they ought to land.
extern const float RASTER_SLANTED[6];
extern const float RASTER_UPPER_RIGHT_HALF[6];
extern const float RASTER_LOWER_LEFT_HALF[6];

/// The two viewports those are drawn through: the whole of a 64 by 64 target, and half of it
/// offset into the middle.
extern const RasterViewport RASTER_FULL_VIEWPORT;
extern const RasterViewport RASTER_HALF_VIEWPORT;

/// Maps one clip space position with w of one into target space.
RasterPoint RasterProject(const RasterViewport *viewport, float clip_x, float clip_y);

/// Builds the target space triangle three clip space corners land on.
///
/// @p clip is six floats, three x and y pairs. The vertices come back in the order the coverage
/// rule needs, which may not be the order they were given in.
void RasterTriangleFromClip(RasterTriangle *out, const RasterViewport *viewport,
                            const float clip[6]);

/// Builds one straight from target space coordinates, for the cases that state them directly.
void RasterTriangleFromPoints(RasterTriangle *out, const RasterPoint points[3]);

/// Whether the point is covered, by the rule the hardware uses.
bool RasterCovers(const RasterTriangle *triangle, double x, double y);

/// How far the point is from the nearest of the three edge lines, in pixels.
double RasterEdgeDistance(const RasterTriangle *triangle, double x, double y);

/// What can be claimed about the pixel at @p px, @p py, judged at its centre.
///
/// @p margin is how close to an edge a centre has to be before the answer stops being something
/// every correct implementation must agree on. Zero means every pixel is judged exactly, which
/// is only honest when the geometry was chosen so that no centre sits near an edge by accident.
RasterVerdict RasterClassifyPixel(const RasterTriangle *triangle, uint32_t px, uint32_t py,
                                  double margin);

#ifdef __cplusplus
}
#endif
