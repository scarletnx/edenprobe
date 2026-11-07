// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixedref.h"

#include <math.h>

const char *FixedCompareName(FixedCompare op) {
    static const char *const NAMES[FIXED_COMPARE_COUNT] = {
        "never", "less than", "equal", "less than or equal", "greater than", "not equal",
        "greater than or equal", "always",
    };
    return (unsigned)op < FIXED_COMPARE_COUNT ? NAMES[op] : "unknown";
}

bool FixedAlphaPasses(FixedCompare op, float alpha, float reference) {
    switch (op) {
    case FIXED_NEVER:
        return false;
    case FIXED_LESS:
        return alpha < reference;
    case FIXED_EQUAL:
        return alpha == reference;
    case FIXED_LEQUAL:
        return alpha <= reference;
    case FIXED_GREATER:
        return alpha > reference;
    case FIXED_NOTEQUAL:
        return alpha != reference;
    case FIXED_GEQUAL:
        return alpha >= reference;
    case FIXED_ALWAYS:
    case FIXED_COMPARE_COUNT:
        break;
    }
    return op == FIXED_ALWAYS;
}

bool FixedBoundsPass(float stored, float min_bound, float max_bound) {
    return min_bound <= stored && stored <= max_bound;
}

float FixedWindowDepth(float z, bool minus_one_to_one) {
    return minus_one_to_one ? z * 0.5f + 0.5f : z;
}

FixedRect FixedPointRect(double cx, double cy, double size) {
    // Pixel i's centre is i + 1/2; it is inside when it lies strictly between the two edges.
    const double half = size * 0.5;
    FixedRect rect;
    rect.x0 = (int32_t)floor(cx - half - 0.5) + 1;
    rect.y0 = (int32_t)floor(cy - half - 0.5) + 1;
    rect.x1 = (int32_t)ceil(cx + half - 0.5);
    rect.y1 = (int32_t)ceil(cy + half - 0.5);
    return rect;
}

static bool Whole(double value) {
    return value == floor(value);
}

bool FixedPointEdgeFree(double cx, double cy, double size) {
    const double half = size * 0.5;
    return !Whole(cx - half - 0.5) && !Whole(cx + half - 0.5) && !Whole(cy - half - 0.5) &&
           !Whole(cy + half - 0.5);
}

void FixedProject(const FixedTransform *transform, double clip_x, double clip_y, double *x,
                  double *y) {
    double sx = transform->swap_xy ? clip_y : clip_x;
    double sy = transform->swap_xy ? clip_x : clip_y;
    if (transform->negate_y) {
        sy = -sy;
    }
    const double half_width = (double)transform->width * 0.5;
    const double half_height = (double)transform->height * 0.5;
    *x = half_width + sx * half_width;
    *y = half_height + (transform->scale_y_positive ? sy : -sy) * half_height;
}

FixedRect FixedProjectRect(const FixedTransform *transform, double x0, double y0, double x1,
                           double y1) {
    double ax, ay, bx, by;
    FixedProject(transform, x0, y0, &ax, &ay);
    FixedProject(transform, x1, y1, &bx, &by);
    FixedRect rect;
    rect.x0 = (int32_t)lround(fmin(ax, bx));
    rect.x1 = (int32_t)lround(fmax(ax, bx));
    rect.y0 = (int32_t)lround(fmin(ay, by));
    rect.y1 = (int32_t)lround(fmax(ay, by));
    return rect;
}

bool FixedFrontFacing(const FixedTransform *transform, bool front_ccw) {
    // A counter clockwise triangle in clip space, y upwards.
    static const double CLIP[3][2] = { { 0.0, 0.0 }, { 0.5, 0.0 }, { 0.0, 0.5 } };
    double x[3], y[3];
    for (int i = 0; i < 3; ++i) {
        FixedProject(transform, CLIP[i][0], CLIP[i][1], &x[i], &y[i]);
    }
    const double area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    const bool counter_clockwise = (area < 0.0) != transform->flip_winding;
    return front_ccw ? counter_clockwise : !counter_clockwise;
}
