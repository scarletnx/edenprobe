// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

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

/// The seeds of every pattern the suite draws, uploads or writes, one per step so no two steps
/// can be mistaken for each other. Every one below 256, which the pattern shader relies on.
typedef enum {
    COH_SEED_RENDERED_START = 1,
    COH_SEED_RENDER_WHOLE,
    COH_SEED_RENDER_PART,
    COH_SEED_PITCH_START,
    COH_SEED_PITCH_REWRITE,
    COH_SEED_BLOCK_START,
    COH_SEED_BLOCK_REWRITE,
    COH_SEED_ALIAS_WHOLE,
    COH_SEED_ALIAS_PART,
    COH_SEED_COPIED_START,
    COH_SEED_COPY_STAGED,
    COH_SEED_COPY_SOURCE,
    COH_SEED_HELD,
    COH_SEED_READBACK_WHOLE,
    COH_SEED_READBACK_PART,
    COH_SEED_CACHED_START,
    COH_SEED_CACHED_ROW,
    COH_SEED_RENDERED_BIG_START,
    COH_SEED_END,
} CohSeed;

/// The texel pattern @p seed holds at @p x, @p y: the same four sums pattern.frag.glsl works
/// out from the fragment's coordinate, so a drawn pattern and an uploaded one are one rule.
///
///   red   = 4x + seed          green = 4y + 3 seed
///   blue  = 4 (x xor y) + 5 seed   alpha = x + y + 7 seed      each modulo 256
///
/// Red and green together name the texel inside a 64 by 64 image, red alone tells two seeds
/// apart at the same texel, and alpha varies so the fourth channel is checked like the others.
void CohPattern(uint32_t seed, uint32_t x, uint32_t y, uint8_t out[4]);

/// The four bytes of a normalised RGBA8 texel read as one R32_Uint word: the first byte in
/// memory is the least significant, r | g << 8 | b << 16 | a << 24.
uint32_t CohAliasWord(const uint8_t rgba[4]);

/// The inverse: the word's bytes, least significant first.
void CohAliasBytes(uint32_t word, uint8_t out[4]);

/// What the alias shader writes for a texel whose four bytes are @p rgba: the little endian
/// word rotated right by four bits, and its bytes, least significant first. The rotation moves
/// every nibble across a byte boundary, so each output byte mixes two input bytes and the
/// answer depends on all four arriving in the right order, not merely on each being present.
/// Also in alias.frag.glsl.
void CohAliasedTexel(const uint8_t rgba[4], uint8_t out[4]);

/// The largest image the model holds, the size of the target every case reads back.
#define COH_MAX_SIZE 64u

/// An image as the model holds it: rows of four byte texels, row major, no padding.
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t texels[COH_MAX_SIZE * COH_MAX_SIZE][4];
} CohImage;

/// What every target is cleared to before a case draws: magenta with no alpha. The reference
/// suite checks that no pattern texel equals it, so a pixel nothing reached cannot pass.
extern const uint8_t COH_SENTINEL[4];

/// How a rectangle's texels land somewhere else.
typedef enum {
    /// The bytes as they are: a copy, or a nearest sample at a texel centre.
    COH_MOVE_BYTES,
    /// The bytes as the alias shader writes them after reading them as one R32_Uint word.
    COH_MOVE_ALIASED,
} CohMove;

/// Sets the size and clears the texels. False when it does not fit the model.
bool CohImageInit(CohImage *image, uint32_t width, uint32_t height);

/// The texel at @p x, @p y, which must be inside.
static inline uint8_t *CohTexelAt(CohImage *image, uint32_t x, uint32_t y) {
    return image->texels[y * image->width + x];
}

void CohFill(CohImage *image, const uint8_t colour[4]);

/// Every texel from CohPattern with @p seed.
void CohFillPattern(CohImage *image, uint32_t seed);

/// One colour over a rectangle. False, and nothing changed, unless it is wholly inside.
bool CohFillRect(CohImage *image, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                 const uint8_t colour[4]);

/// What the suite's cell draw leaves: the image cut into a 4 by 4 grid of equal cells, and the
/// cells whose bit is set in @p mask (bit 4 row + column) painted with pattern @p seed at the
/// image's own coordinates, as the pattern shader does. The mask only chooses what is redrawn.
/// False, and nothing changed, when the image does not divide into the grid.
bool CohPaintCells(CohImage *image, uint32_t seed, uint32_t mask);

/// The @p width by @p height rectangle of @p source at @p source_x, @p source_y lands in
/// @p destination at @p x, @p y, moved as @p how says. The two may be one image and overlap in
/// any direction; every texel lands as it was before the move. False, and nothing changed,
/// unless both rectangles are wholly inside their images.
bool CohMoveRect(CohImage *destination, uint32_t x, uint32_t y, const CohImage *source,
                 uint32_t source_x, uint32_t source_y, uint32_t width, uint32_t height,
                 CohMove how);

/// What a quad sampling all of @p texture over @p region of @p target leaves there, texel by
/// CohTexelUnder, moved as @p how says. False, and nothing changed, unless the region is wholly
/// inside the target.
bool CohSampleRegion(CohImage *target, const CohRect *region, const CohImage *texture,
                     CohMove how);

/// What four draws leave on a 64 by 64 image, one per 32 by 32 quadrant in reading order, each
/// in @p base with channel @p channel set to @p values of that quadrant. @p lag names the value
/// @p lag draws earlier instead, the first one where there is none, which is the image a cache
/// that many rewrites behind would leave. False, and nothing changed, unless the image is 64 by
/// 64 and the channel is one of the four.
bool CohQuadrantColours(CohImage *image, const uint8_t base[4], uint32_t channel,
                        const uint8_t values[4], uint32_t lag);

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
