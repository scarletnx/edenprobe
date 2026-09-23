// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>

#include "coherref.h"
#include "texref.h"

#define GOB_WIDTH_BYTES 64u
#define GOB_HEIGHT_ROWS 8u
#define GOB_BYTES (GOB_WIDTH_BYTES * GOB_HEIGHT_ROWS)

bool CohTexelUnder(const CohRect *region, uint32_t texture_width, uint32_t texture_height,
                   uint32_t pixel_x, uint32_t pixel_y, uint32_t *texel_x, uint32_t *texel_y) {
    if (pixel_x < region->x || pixel_y < region->y || pixel_x - region->x >= region->width ||
        pixel_y - region->y >= region->height) {
        return false;
    }
    const double u =
        TexCoordInTexels(0.0, 1.0, region->width, pixel_x - region->x, texture_width);
    const double v =
        TexCoordInTexels(0.0, 1.0, region->height, pixel_y - region->y, texture_height);
    *texel_x = (uint32_t)TexNearestTexel(TEXWRAP_CLAMP_TO_EDGE, u, (int32_t)texture_width);
    *texel_y = (uint32_t)TexNearestTexel(TEXWRAP_CLAMP_TO_EDGE, v, (int32_t)texture_height);
    return true;
}

double CohClosestTexelEdge(const CohRect *region, uint32_t texture_width,
                           uint32_t texture_height) {
    double closest = 1.0;
    for (uint32_t i = 0; i < region->width || i < region->height; ++i) {
        const double along[2] = {
            i < region->width ? TexCoordInTexels(0.0, 1.0, region->width, i, texture_width)
                              : 0.5,
            i < region->height ? TexCoordInTexels(0.0, 1.0, region->height, i, texture_height)
                               : 0.5,
        };
        for (int axis = 0; axis < 2; ++axis) {
            const double distance = fabs(along[axis] - floor(along[axis] + 0.5));
            if (distance < closest) {
                closest = distance;
            }
        }
    }
    return closest;
}

void CohPatternTexel(uint32_t seed, uint32_t x, uint32_t y, uint8_t out[4]) {
    // x and y in the five high bits of red and green, the seed in blue's high nibble, and alpha
    // mixing all three so it is never a constant a wrong channel order could land on by luck.
    out[0] = (uint8_t)((x & 31u) << 3 | 0x03u);
    out[1] = (uint8_t)((y & 31u) << 3 | 0x05u);
    out[2] = (uint8_t)((seed & 15u) << 4 | ((x + y) & 15u));
    out[3] = (uint8_t)(0x40u + ((x * 3u + y * 5u + seed * 7u) & 0x7Fu));
}

void CohCellColour(uint32_t seed, uint32_t cell, uint8_t out[4]) {
    out[0] = (uint8_t)(0x11u * (cell & 15u));
    out[1] = (uint8_t)(0xF0u - 0x0Fu * (cell & 15u));
    out[2] = (uint8_t)((seed & 15u) << 4 | 0x08u);
    out[3] = (uint8_t)(0x80u + ((cell * 5u + seed * 3u) & 0x3Fu));
}

uint32_t CohAliasWord(const uint8_t rgba[4]) {
    return (uint32_t)rgba[0] | (uint32_t)rgba[1] << 8 | (uint32_t)rgba[2] << 16 |
           (uint32_t)rgba[3] << 24;
}

void CohAliasBytes(uint32_t word, uint8_t out[4]) {
    out[0] = (uint8_t)(word & 0xFFu);
    out[1] = (uint8_t)(word >> 8 & 0xFFu);
    out[2] = (uint8_t)(word >> 16 & 0xFFu);
    out[3] = (uint8_t)(word >> 24 & 0xFFu);
}

void CohAliasExpected(const uint8_t rgba[4], uint8_t out[4]) {
    CohAliasBytes(CohAliasWord(rgba) + COH_ALIAS_ADDEND, out);
}

uint32_t CohBlockLinearOffset(uint32_t x_bytes, uint32_t y, uint32_t width_bytes,
                              uint32_t block_height_gobs) {
    const uint32_t block_rows = GOB_HEIGHT_ROWS * block_height_gobs;
    const uint32_t gobs_across = (width_bytes + GOB_WIDTH_BYTES - 1u) / GOB_WIDTH_BYTES;
    const uint32_t block = (y / block_rows) * gobs_across + x_bytes / GOB_WIDTH_BYTES;
    const uint32_t gob_in_block = (y % block_rows) / GOB_HEIGHT_ROWS;
    const uint32_t gx = x_bytes % GOB_WIDTH_BYTES;
    const uint32_t gy = y % GOB_HEIGHT_ROWS;
    const uint32_t inside = (gx / 32u) * 256u + (gy / 2u) * 64u + ((gx % 32u) / 16u) * 32u +
                            (gy % 2u) * 16u + gx % 16u;
    return block * GOB_BYTES * block_height_gobs + gob_in_block * GOB_BYTES + inside;
}
