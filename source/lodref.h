// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The level of detail a lookup picks and the texel it then reads, worked out on the processor.
///
/// The lod suite's texture is R32_Float, 16 by 8 with its five levels, and texel (x, y) of level
/// l holds 1000 l + 16 y + x, a whole number a float holds exactly, so the value a lookup
/// returns names the level and the texel it came from. Wider than tall, so a gradient along y
/// scaled by the width instead of the height lands a level away. The rules are the
/// specification's (OpenGL 4.6, 8.14): rho the larger of the two derivatives' lengths in texels,
/// lambda its base two logarithm plus the sampler's bias, clamped to the sampler's minimum and
/// maximum, and nearest mip filtering taking level nought up to lambda one half and
/// ceil(lambda + 1/2) - 1 after it.

#define LOD_WIDTH 16u
#define LOD_HEIGHT 8u
#define LOD_LEVELS 5u

/// The value texel (@p x, @p y) of level @p level holds.
float LodTexelValue(uint32_t level, uint32_t x, uint32_t y);

/// One side of level @p level of an image whose level nought is @p size on that side.
uint32_t LodLevelSize(uint32_t size, uint32_t level);

/// The texel nearest filtering reads along one axis: the coordinate times the level's size,
/// rounded down, plus the lookup's @p offset in that level's texels, clamped to the edge.
int32_t LodTexelIndex(double coord, uint32_t size, int32_t offset);

/// How far, in texels, @p coord lands from the nearest texel boundary on any level of an axis
/// @p size wide at level nought. A case only uses coordinates where this is far from nought, so
/// no rounding in the sampler's arithmetic can move it a texel.
double LodCoordMargin(double coord, uint32_t size, uint32_t levels);

/// Lambda for explicit gradients, the derivatives of the normalised coordinate along x
/// (@p dudx, @p dvdx) and along y (@p dudy, @p dvdy), on a level nought of @p width by @p height.
double LodGradientLambda(double dudx, double dvdx, double dudy, double dvdy, uint32_t width,
                         uint32_t height);

/// The level nearest mip filtering takes at @p lambda, out of @p levels.
uint32_t LodNearestLevel(double lambda, uint32_t levels);

/// What a nearest lookup at (@p u, @p v) with the offset (@p ox, @p oy) returns from level
/// @p level of the lod suite's texture.
float LodExpected(uint32_t level, double u, double v, int32_t ox, int32_t oy);

/// What a lookup with linear mip filtering at lambda @p lambda returns: the two levels around it,
/// each read nearest, weighted by lambda's fraction. A double, so the reference suite can see
/// whether a float holds it exactly.
double LodExpectedLinear(double lambda, double u, double v);

#ifdef __cplusplus
}
#endif
