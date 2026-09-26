// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bcnref.h"

#include <string.h>

#include "coherref.h"

// deko3d's pickTileSize: the largest tile of 1 to 16 gobs not taller than the rows asked for.
static uint32_t PickTileShift(uint32_t gobs) {
    return gobs >= 16u ? 4u : gobs >= 8u ? 3u : gobs >= 4u ? 2u : gobs >= 2u ? 1u : 0u;
}

// deko3d's adjustTileSize for heights: shrink the tile while half of it still holds the level.
static uint32_t AdjustTileShift(uint32_t shift, uint32_t rows) {
    while (shift != 0u && (8u << (shift - 1u)) >= rows) {
        --shift;
    }
    return shift;
}

static uint32_t Blocks(uint32_t texels, uint32_t level) {
    const uint32_t size = texels >> level;
    return ((size != 0u ? size : 1u) + 3u) / 4u;
}

uint32_t BcnLevelBlocksX(const BcnArray *array, uint32_t level) {
    return Blocks(array->width, level);
}

uint32_t BcnLevelBlocksY(const BcnArray *array, uint32_t level) {
    return Blocks(array->height, level);
}

static uint32_t LevelSize(const BcnArray *array, uint32_t level) {
    const uint32_t shift = AdjustTileShift(array->tile_shift, BcnLevelBlocksY(array, level));
    const uint32_t width_gobs = (BcnLevelBlocksX(array, level) * array->bytes_per_block + 63u) / 64u;
    const uint32_t height_gobs = (BcnLevelBlocksY(array, level) + 7u) / 8u;
    const uint32_t height_tiles = (height_gobs + (1u << shift) - 1u) >> shift;
    return (width_gobs * height_tiles) << (9u + shift);
}

uint32_t BcnLevelOffset(const BcnArray *array, uint32_t level) {
    uint32_t offset = 0;
    for (uint32_t i = 0; i < level; ++i) {
        offset += LevelSize(array, i);
    }
    return offset;
}

void BcnArrayLayout(BcnArray *array, uint32_t width, uint32_t height, uint32_t layers,
                    uint32_t levels, uint32_t bytes_per_block) {
    memset(array, 0, sizeof(*array));
    array->width = width;
    array->height = height;
    array->layers = layers;
    array->levels = levels;
    array->bytes_per_block = bytes_per_block;
    // The block rows times one and a half, in gobs of eight rows.
    const uint32_t rows = (height + 3u) / 4u;
    array->tile_shift = PickTileShift((rows + rows / 2u + 7u) / 8u);
    const uint32_t size = BcnLevelOffset(array, levels);
    // alignLayerSize: the tile of level 0's rows, shrunk the same way.
    const uint32_t gobs_shift = AdjustTileShift(array->tile_shift, rows) + 9u;
    const uint32_t unit = 1u << gobs_shift;
    array->layer_size = (size + unit - 1u) / unit * unit;
}

uint32_t BcnBlockOffset(const BcnArray *array, uint32_t layer, uint32_t level, uint32_t bx,
                        uint32_t by) {
    const uint32_t shift = AdjustTileShift(array->tile_shift, BcnLevelBlocksY(array, level));
    return layer * array->layer_size + BcnLevelOffset(array, level) +
           CohBlockLinearOffset(bx * array->bytes_per_block, by,
                                BcnLevelBlocksX(array, level) * array->bytes_per_block,
                                1u << shift);
}

void BcnSolidBc4(uint8_t block[8], uint8_t value) {
    memset(block, 0, 8);
    block[0] = value;
    block[1] = value;
}
