// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <math.h>
#include <string.h>

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

void CohPattern(uint32_t seed, uint32_t x, uint32_t y, uint8_t out[4]) {
    out[0] = (uint8_t)((4u * x + seed) & 0xFFu);
    out[1] = (uint8_t)((4u * y + 3u * seed) & 0xFFu);
    out[2] = (uint8_t)((4u * (x ^ y) + 5u * seed) & 0xFFu);
    out[3] = (uint8_t)((x + y + 7u * seed) & 0xFFu);
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

void CohAliasedTexel(const uint8_t rgba[4], uint8_t out[4]) {
    const uint32_t word = CohAliasWord(rgba);
    CohAliasBytes(word >> 4 | word << 28, out);
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

const uint8_t COH_SENTINEL[4] = { 0xFF, 0x00, 0xFF, 0x00 };

bool CohImageInit(CohImage *image, uint32_t width, uint32_t height) {
    if (image == NULL || width == 0u || height == 0u || width > COH_MAX_SIZE ||
        height > COH_MAX_SIZE) {
        return false;
    }
    memset(image, 0, sizeof(*image));
    image->width = width;
    image->height = height;
    return true;
}

void CohFill(CohImage *image, const uint8_t colour[4]) {
    for (uint32_t i = 0; i < image->width * image->height; ++i) {
        memcpy(image->texels[i], colour, 4);
    }
}

void CohFillPattern(CohImage *image, uint32_t seed) {
    for (uint32_t y = 0; y < image->height; ++y) {
        for (uint32_t x = 0; x < image->width; ++x) {
            CohPattern(seed, x, y, CohTexelAt(image, x, y));
        }
    }
}

/// Whether a rectangle is wholly inside, by subtraction so a huge size cannot wrap past it.
static bool Inside(const CohImage *image, uint32_t x, uint32_t y, uint32_t width,
                   uint32_t height) {
    return width != 0u && height != 0u && width <= image->width && height <= image->height &&
           x <= image->width - width && y <= image->height - height;
}

bool CohFillRect(CohImage *image, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                 const uint8_t colour[4]) {
    if (!Inside(image, x, y, width, height)) {
        return false;
    }
    for (uint32_t row = y; row < y + height; ++row) {
        for (uint32_t column = x; column < x + width; ++column) {
            memcpy(CohTexelAt(image, column, row), colour, 4);
        }
    }
    return true;
}

#define CELLS_ACROSS 4u

bool CohPaintCells(CohImage *image, uint32_t seed, uint32_t mask) {
    if (image->width % CELLS_ACROSS != 0u || image->height % CELLS_ACROSS != 0u) {
        return false;
    }
    const uint32_t cell_width = image->width / CELLS_ACROSS;
    const uint32_t cell_height = image->height / CELLS_ACROSS;
    for (uint32_t y = 0; y < image->height; ++y) {
        for (uint32_t x = 0; x < image->width; ++x) {
            const uint32_t cell = (y / cell_height) * CELLS_ACROSS + x / cell_width;
            if ((mask >> cell & 1u) != 0u) {
                CohPattern(seed, x, y, CohTexelAt(image, x, y));
            }
        }
    }
    return true;
}

static void Move(const uint8_t in[4], CohMove how, uint8_t out[4]) {
    if (how == COH_MOVE_ALIASED) {
        CohAliasedTexel(in, out);
    } else {
        memcpy(out, in, 4);
    }
}

bool CohMoveRect(CohImage *destination, uint32_t x, uint32_t y, const CohImage *source,
                 uint32_t source_x, uint32_t source_y, uint32_t width, uint32_t height,
                 CohMove how) {
    if (!Inside(destination, x, y, width, height) ||
        !Inside(source, source_x, source_y, width, height)) {
        return false;
    }
    // Through a copy of each source row, which covers an overlap along the row, and bottom up
    // when the rows land below where they came from in the same image, which covers one down
    // the columns: either way no texel is read after it has been overwritten.
    const bool bottom_up = destination == source && y > source_y;
    for (uint32_t step = 0; step < height; ++step) {
        const uint32_t row = bottom_up ? height - 1u - step : step;
        uint8_t line[COH_MAX_SIZE][4];
        memcpy(line, source->texels[(source_y + row) * source->width + source_x], width * 4u);
        for (uint32_t column = 0; column < width; ++column) {
            Move(line[column], how, CohTexelAt(destination, x + column, y + row));
        }
    }
    return true;
}

bool CohSampleRegion(CohImage *target, const CohRect *region, const CohImage *texture,
                     CohMove how) {
    if (!Inside(target, region->x, region->y, region->width, region->height)) {
        return false;
    }
    for (uint32_t y = region->y; y < region->y + region->height; ++y) {
        for (uint32_t x = region->x; x < region->x + region->width; ++x) {
            uint32_t tx = 0;
            uint32_t ty = 0;
            CohTexelUnder(region, texture->width, texture->height, x, y, &tx, &ty);
            Move(texture->texels[ty * texture->width + tx], how, CohTexelAt(target, x, y));
        }
    }
    return true;
}

bool CohQuadrantColours(CohImage *image, const uint8_t base[4], uint32_t channel,
                        const uint8_t values[4], uint32_t lag) {
    if (image->width != 64u || image->height != 64u || channel > 3u) {
        return false;
    }
    for (uint32_t q = 0; q < 4u; ++q) {
        uint8_t colour[4];
        memcpy(colour, base, 4);
        colour[channel] = values[q >= lag ? q - lag : 0u];
        CohFillRect(image, (q % 2u) * 32u, (q / 2u) * 32u, 32u, 32u, colour);
    }
    return true;
}
