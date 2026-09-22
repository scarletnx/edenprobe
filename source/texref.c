// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>

#include "texref.h"

/// The remainder that is never negative, which is not what the language's own operator gives.
static int32_t FloorMod(int32_t value, int32_t modulus) {
    const int32_t remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

int32_t TexWrapIndex(TexWrap mode, int32_t index, int32_t size) {
    if (size <= 0) {
        return 0;
    }
    switch (mode) {
    case TEXWRAP_REPEAT:
        return FloorMod(index, size);
    case TEXWRAP_CLAMP_TO_EDGE:
        if (index < 0) {
            return 0;
        }
        return index >= size ? size - 1 : index;
    case TEXWRAP_MIRRORED_REPEAT: {
        // Period twice the texture, with the second half walked backwards. Index minus one
        // therefore lands on texel nought rather than on the last texel, which is the whole
        // difference between this and repeating, and the seam a mirrored tiling does not have.
        const int32_t folded = FloorMod(index, 2 * size);
        return folded < size ? folded : (2 * size - 1 - folded);
    }
    }
    return 0;
}

double TexCoordInTexels(double first, double last, uint32_t span_pixels, uint32_t pixel,
                        uint32_t size) {
    if (span_pixels == 0u) {
        return 0.0;
    }
    const double along = ((double)pixel + 0.5) / (double)span_pixels;
    return (first + (last - first) * along) * (double)size;
}

int32_t TexNearestTexel(TexWrap mode, double coordinate, int32_t size) {
    return TexWrapIndex(mode, (int32_t)floor(coordinate), size);
}

void TexLinearTexels(TexWrap mode, double coordinate, int32_t size, int32_t out_pair[2],
                     double *out_weight) {
    const double shifted = coordinate - 0.5;
    const double base = floor(shifted);
    out_pair[0] = TexWrapIndex(mode, (int32_t)base, size);
    out_pair[1] = TexWrapIndex(mode, (int32_t)base + 1, size);
    if (out_weight != NULL) {
        *out_weight = shifted - base;
    }
}
