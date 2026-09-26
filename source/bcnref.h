// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Where the processor writes the blocks of a block linear 2D array of a 4x4 block format, as
/// deko3d lays it out (source/dk_image.cpp, 0.5.0): each level's tile height shrinks to what
/// its height needs, the levels follow each other inside a layer, and a layer is aligned to
/// the tile height of level 0's size.
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t levels;
    uint32_t bytes_per_block;
    uint32_t tile_shift;
    uint32_t layer_size;
} BcnArray;

/// Lays out a @p width by @p height array of @p layers layers and @p levels levels.
void BcnArrayLayout(BcnArray *array, uint32_t width, uint32_t height, uint32_t layers,
                    uint32_t levels, uint32_t bytes_per_block);

/// The byte offset of level @p level inside a layer.
uint32_t BcnLevelOffset(const BcnArray *array, uint32_t level);

/// The byte offset of block (@p bx, @p by) of @p level in @p layer.
uint32_t BcnBlockOffset(const BcnArray *array, uint32_t layer, uint32_t level, uint32_t bx,
                        uint32_t by);

/// The blocks of @p level in each direction.
uint32_t BcnLevelBlocksX(const BcnArray *array, uint32_t level);
uint32_t BcnLevelBlocksY(const BcnArray *array, uint32_t level);

/// A BC4 block whose sixteen texels are all @p value: both endpoints @p value, every index 0.
void BcnSolidBc4(uint8_t block[8], uint8_t value);

#ifdef __cplusplus
}
#endif
