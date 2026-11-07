// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lodref.h"

#include <math.h>

float LodTexelValue(uint32_t level, uint32_t x, uint32_t y) {
    return (float)(1000u * level + 16u * y + x);
}

uint32_t LodLevelSize(uint32_t size, uint32_t level) {
    const uint32_t side = level < 32u ? size >> level : 0u;
    return side > 0u ? side : 1u;
}

int32_t LodTexelIndex(double coord, uint32_t size, int32_t offset) {
    int32_t index = (int32_t)floor(coord * (double)size) + offset;
    if (index < 0) {
        index = 0;
    }
    if (index > (int32_t)size - 1) {
        index = (int32_t)size - 1;
    }
    return index;
}

double LodCoordMargin(double coord, uint32_t size, uint32_t levels) {
    double margin = 1.0;
    for (uint32_t level = 0; level < levels; ++level) {
        const double t = coord * (double)LodLevelSize(size, level);
        const double fraction = t - floor(t);
        const double distance = fraction < 1.0 - fraction ? fraction : 1.0 - fraction;
        if (distance < margin) {
            margin = distance;
        }
    }
    return margin;
}

double LodGradientLambda(double dudx, double dvdx, double dudy, double dvdy, uint32_t width,
                         uint32_t height) {
    const double along_x = hypot(dudx * (double)width, dvdx * (double)height);
    const double along_y = hypot(dudy * (double)width, dvdy * (double)height);
    const double rho = along_x > along_y ? along_x : along_y;
    return log2(rho);
}

uint32_t LodNearestLevel(double lambda, uint32_t levels) {
    if (lambda <= 0.5) {
        return 0;
    }
    const double level = ceil(lambda + 0.5) - 1.0;
    return level >= (double)(levels - 1u) ? levels - 1u : (uint32_t)level;
}

float LodExpected(uint32_t level, double u, double v, int32_t ox, int32_t oy) {
    const int32_t x = LodTexelIndex(u, LodLevelSize(LOD_WIDTH, level), ox);
    const int32_t y = LodTexelIndex(v, LodLevelSize(LOD_HEIGHT, level), oy);
    return LodTexelValue(level, (uint32_t)x, (uint32_t)y);
}

double LodExpectedLinear(double lambda, double u, double v) {
    if (lambda <= 0.0) {
        return LodExpected(0, u, v, 0, 0);
    }
    const uint32_t low = (uint32_t)floor(lambda);
    if (low >= LOD_LEVELS - 1u) {
        return LodExpected(LOD_LEVELS - 1u, u, v, 0, 0);
    }
    const double weight = lambda - floor(lambda);
    return (1.0 - weight) * (double)LodExpected(low, u, v, 0, 0) +
           weight * (double)LodExpected(low + 1u, u, v, 0, 0);
}
