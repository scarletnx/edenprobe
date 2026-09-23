// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Which texel a sampled coordinate lands on, worked out on the processor.
///
/// Two things live here, and they are the two the texture suite has to be right about before it
/// can judge anything. Where a pixel's texture coordinate is, which is a property of the quad
/// being drawn and not of the sampler; and which texel that coordinate names once the wrap mode
/// has had its say.

typedef enum {
    /// The texture tiles. Texel minus one is the last texel.
    TEXWRAP_REPEAT,
    /// Everything outside is the nearest edge texel.
    TEXWRAP_CLAMP_TO_EDGE,
    /// The texture tiles, reflected every other time, so the tiling has no seam.
    TEXWRAP_MIRRORED_REPEAT,
} TexWrap;

/// The texel a whole numbered index names once @p mode has folded it into nought to @p size - 1.
int32_t TexWrapIndex(TexWrap mode, int32_t index, int32_t size);

/// Where the pixel at @p pixel lands in texel units.
///
/// A quad maps the screen span from nought to @p span_pixels onto the texture coordinate span
/// from @p first to @p last, and the coordinate is interpolated linearly across it. A pixel is
/// sampled at its centre, half a pixel in. Multiplying by the texture's @p size turns a
/// normalised coordinate into texel units, which is the space both filters work in.
double TexCoordInTexels(double first, double last, uint32_t span_pixels, uint32_t pixel,
                        uint32_t size);

/// The texel nearest filtering picks for a coordinate already in texel units.
int32_t TexNearestTexel(TexWrap mode, double coordinate, int32_t size);

/// The two texels linear filtering blends, and how much of the second one it takes.
///
/// Linear filtering measures from texel centres, which sit half a texel in, so the pair is
/// found by stepping back half a texel and taking the whole part and the fraction. A weight of
/// nought is all of the first texel and a weight of one is all of the second.
void TexLinearTexels(TexWrap mode, double coordinate, int32_t size, int32_t out_pair[2],
                     double *out_weight);

#ifdef __cplusplus
}
#endif
