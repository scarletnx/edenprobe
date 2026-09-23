// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the coherence suite expects, worked out on the processor.
///
/// Four rules, each more than a line, so each lives here and the reference suite pins it with no
/// device: which texel a pixel of a region samples, the texels the suite's patterns hold, the
/// byte order two formats sharing one memory agree on, and where the block linear layout puts a
/// texel for a processor writing it by hand.

/// A rectangle of pixels, top left corner first.
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} CohRect;

/// The texel a pixel samples when a quad stretches a whole @p texture_width by @p texture_height
/// texture over @p region with nearest filtering. False for a pixel outside the region.
///
/// Built on the texture suite's own coordinate rule, so this adds only the region's offset.
bool CohTexelUnder(const CohRect *region, uint32_t texture_width, uint32_t texture_height,
                   uint32_t pixel_x, uint32_t pixel_y, uint32_t *texel_x, uint32_t *texel_y);

/// How close any pixel centre of @p region comes to a texel edge, in texels. The suite maps one
/// texel to one pixel, so every centre is a texel centre and this is a half.
double CohClosestTexelEdge(const CohRect *region, uint32_t texture_width,
                           uint32_t texture_height);

/// One texel of pattern @p seed, every texel of a 32 by 32 pattern different from the others
/// and from every texel of another seed below 16. Alpha is never 0x00 or 0xFF, so a channel
/// order mistake cannot hide behind an opaque alpha.
void CohPatternTexel(uint32_t seed, uint32_t x, uint32_t y, uint8_t out[4]);

/// The flat colour of cell @p cell (0 to 15) of a painted pattern, the colour a draw can put
/// on a render target exactly. Every cell and every seed below 16 different.
void CohCellColour(uint32_t seed, uint32_t cell, uint8_t out[4]);

/// The four bytes of a normalised RGBA8 texel read as one R32_Uint word: the first byte in
/// memory is the least significant, r | g << 8 | b << 16 | a << 24.
uint32_t CohAliasWord(const uint8_t rgba[4]);

/// The inverse: the word's bytes, least significant first.
void CohAliasBytes(uint32_t word, uint8_t out[4]);

/// What the alias shader adds before writing the word's bytes back out. Carries run across
/// bytes, so a word assembled in the wrong order cannot come out right. Also in alias.frag.glsl.
#define COH_ALIAS_ADDEND 0x01010101u

/// The bytes the alias case expects for a texel holding @p rgba.
void CohAliasExpected(const uint8_t rgba[4], uint8_t out[4]);

/// Where byte @p x_bytes of row @p y sits in a block linear image whose rows are
/// @p width_bytes wide and whose blocks are @p block_height_gobs gobs tall.
///
/// A gob is 64 bytes by 8 rows, 512 bytes, stored as two 32 byte halves of four row pairs of
/// 16 byte runs. Blocks run left to right, then down; gobs inside a block run down.
uint32_t CohBlockLinearOffset(uint32_t x_bytes, uint32_t y, uint32_t width_bytes,
                              uint32_t block_height_gobs);

#ifdef __cplusplus
}
#endif
