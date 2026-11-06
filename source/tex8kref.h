// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "astcimages.h"
#include "bcnref.h"

#ifdef __cplusplus
extern "C" {
#endif

/// What the tex8k suite's textures hold, and what each texel of them must read back as.
///
/// Every texel's value follows from where it is, which upload or frame wrote it last, and the
/// format, so a draw that fetches a few thousand texels of an 8192x8192 texture is judged without
/// the texture ever being read back. No libnx here: the host build runs it in the reference
/// suite.

/// The draws' target, 64x64, which is also level 7 of an 8192x8192 texture.
#define TEX8K_TARGET 64u
/// The levels a mipmapped texture carries: 8192 down to 64.
#define TEX8K_LEVELS 8u
/// Where the texels a spread draw fetches start: pixel (x, y) reads texel
/// (x * size / 64 + TEX8K_SPREAD_X, y * size / 64 + TEX8K_SPREAD_Y) of level 0.
#define TEX8K_SPREAD_X 37u
#define TEX8K_SPREAD_Y 91u
/// The frames, the region each one writes, and its cell on the target: frame f's draw covers
/// the 8x8 pixels of cell (f % 8, f / 8), each pixel reading every 32nd texel of the region
/// from (TEX8K_CELL_X, TEX8K_CELL_Y) on.
#define TEX8K_FRAMES 64u
#define TEX8K_REGION 256u
#define TEX8K_CELL 8u
#define TEX8K_CELL_X 5u
#define TEX8K_CELL_Y 9u

/// The BC7 texture's levels, its whole chain from 8192 down to 1, so its smallest levels share
/// one 4 KiB page (the mip tail).
#define TEX8K_BC7_LEVELS 14u

/// The salts the writes after the frames take: the processor's rectangle in the pitch linear
/// texture, and the copy engine's two raw copies into the BC7 texture.
#define TEX8K_EDGE_SALT (TEX8K_FRAMES + 1u)
#define TEX8K_CROSS_SALT (TEX8K_FRAMES + 2u)
#define TEX8K_TAIL_SALT (TEX8K_FRAMES + 3u)

/// The processor's write into the pitch linear texture: a square whose rows cross a 4 KiB page
/// edge (texel 1024 starts a page, a row being 32 KiB), clear of every frame's region; and the
/// draw that judges it, a texel every second one from (TEX8K_EDGE_FROM_X, TEX8K_EDGE_FROM_Y), so
/// the target holds the square, the page edge inside it and texels on all four sides of it.
#define TEX8K_EDGE_X 1000u
#define TEX8K_EDGE_Y 4100u
#define TEX8K_EDGE_SIDE 48u
#define TEX8K_EDGE_FROM_X 960u
#define TEX8K_EDGE_FROM_Y 4076u
#define TEX8K_EDGE_STEP 2u

/// The copy engine's raw copy across two levels of the BC7 texture: TEX8K_CROSS_BYTES before
/// level TEX8K_CROSS_LEVEL starts and as many after.
#define TEX8K_CROSS_LEVEL 4u
#define TEX8K_CROSS_BYTES 4096u

/// The raw copy into the mip tail: bytes TEX8K_TAIL_FROM to TEX8K_TAIL_TO after level
/// TEX8K_TAIL_LEVEL starts, which hold half of level 9's blocks and all of level 10's.
#define TEX8K_TAIL_LEVEL 8u
#define TEX8K_TAIL_FROM 1280u
#define TEX8K_TAIL_TO 1792u

typedef enum {
    TEX8K_RGBA8,
    TEX8K_BC1,
    TEX8K_BC7,
    TEX8K_ASTC,
} Tex8kKind;

/// One format the suite uploads: a block is a texel for RGBA8.
typedef struct {
    Tex8kKind kind;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t block_bytes;
    /// The picture the ASTC blocks come from (ASTC_IMAGE), NULL for the other formats.
    const AstcImage *astc;
} Tex8kFormat;

/// The 32-bit mix every pattern is drawn from: block (or texel) @p x, @p y, written with @p salt
/// (0 for the upload, 1 + f for frame f).
uint32_t Tex8kMix(uint32_t x, uint32_t y, uint32_t salt);

/// The six bits an RGBA8 texel keeps of @p salt: nought for nought, any other salt folded into
/// 1 to 63, so a frame's region never reads as the upload's.
uint32_t Tex8kRgba8Salt(uint32_t salt);

/// The bytes of block (@p bx, @p by) of level 0 as @p salt writes it, block_bytes of them: an
/// RGBA8 texel is x | y << 13 | Tex8kRgba8Salt(salt) << 26, little endian, so every texel of an
/// 8192x8192 texture differs; a BC1 block is one colour (indices nought) from the mix; a BC7
/// block is mode 6, both endpoints and the p-bits from the mix, texel t taking index t, so every
/// texel of a block differs; an ASTC block is the picture's, from a tile of the picture shifted
/// by its position and the salt (Tex8kAstcSource).
void Tex8kBlock(const Tex8kFormat *format, uint32_t bx, uint32_t by, uint32_t salt,
                uint8_t *out);

/// Which of the picture's blocks block (@p bx, @p by) of an ASTC texture takes for @p salt: the
/// texture is the picture tiled, each tile turned by a shift of whole blocks that its position
/// and the salt choose, so a block moved to another tile shows.
void Tex8kAstcSource(const Tex8kFormat *format, uint32_t bx, uint32_t by, uint32_t salt,
                     uint32_t *px, uint32_t *py);

/// Writes the blocks of rows @p by0 to @p by0 + @p rows and columns @p bx0 to @p bx0 + @p across
/// of level 0, as @p salt writes them, to @p out, @p pitch bytes a row of blocks.
void Tex8kFillBlocks(const Tex8kFormat *format, uint8_t *out, size_t pitch, uint32_t bx0,
                     uint32_t by0, uint32_t across, uint32_t rows, uint32_t salt);

/// The bytes texel (@p x, @p y) of level 0 may read back as, written with @p salt: up to three
/// RGBA8 answers, the first the one a wrong texel is shown against. One for RGBA8 and BC7, whose
/// decode is exact; a BC1 channel may be its code with its bits repeated or the code over its
/// largest rounded, which differ for some codes (3 is 24 and 25 in five bits); ASTC the three
/// AstcUnorm8Fp16 and AstcUnorm8Answers give.
void Tex8kTexel(const Tex8kFormat *format, uint32_t x, uint32_t y, uint32_t salt,
                uint8_t answers[3][4]);

/// Whether @p rgba is one of the @p answers.
bool Tex8kMatches(const uint8_t rgba[4], const uint8_t answers[3][4]);

/// How far @p rgba is from the @p answers: its worst channel's distance to the nearest answer
/// for that channel. Nought when it matches.
uint32_t Tex8kDistance(const uint8_t rgba[4], const uint8_t answers[3][4]);

/// BC7's loss on the picture the ASTC textures are made of, a bound a channel per region of it
/// (ASTC_IMAGE_REGION's order: gradients, edges, noise, alpha). Each is the worst channel of
/// the BC7 encodes of the picture's 4x4 and 8x8 decodes by ScarletNX's bc7_encode.glsl and by
/// bc7enc (etcpak 0.9.15, levels 0, 2 and 4, linear and perceptual weights), measured against
/// the nearer of the two decodes astcref.c gives: the texture holds no 4x4 block the picture
/// does not, so that is the worst those encoders do anywhere in it.
extern const uint8_t TEX8K_BC7_BOUND[ASTC_IMAGE_REGIONS];

/// The region of the picture texel (@p x, @p y) of level 0 of an ASTC texture comes from.
uint32_t Tex8kAstcRegion(const Tex8kFormat *format, uint32_t x, uint32_t y, uint32_t salt);

/// The @p percent percentile of @p count values sorted from the smallest, numpy's default: the
/// value at (count - 1) * percent / 100, the two either side of it weighed by where it falls.
uint64_t Tex8kPercentile(const uint64_t *sorted, uint32_t count, uint32_t percent);

/// Where level @p level's texel (0, 0) comes from in level 0 of a @p size texture: each level is
/// the copy engine's copy of a square of level 0's staging, from here on, so a fetch of the
/// wrong level reads other texels. Nought for level 0; a multiple of 8 otherwise, whole blocks,
/// each level's further in than the one before.
void Tex8kLevelOrigin(uint32_t size, uint32_t level, uint32_t *x, uint32_t *y);

/// Whether the raw copy of bytes [@p from, @p to) of a BC texture laid out as @p array covers
/// block (@p bx, @p by) of @p level.
bool Tex8kRawCovers(const BcnArray *array, uint32_t level, uint32_t bx, uint32_t by,
                    uint32_t from, uint32_t to);

/// The bytes [@p from, @p to) of a BC texture laid out as @p array, @p format's blocks, would hold
/// had every block they cover been written with @p salt, a level's blocks taken from level 0's
/// at Tex8kLevelOrigin; nought where no block lies (a gob's padding). Into @p out, to - from
/// bytes. How many blocks it wrote.
uint32_t Tex8kRawBytes(const Tex8kFormat *format, const BcnArray *array, uint32_t from,
                       uint32_t to, uint32_t salt, uint8_t *out);

/// Where frame @p frame's region starts in level 0 of an 8192x8192 texture: one of its 32x32
/// tiles of 256 texels, no two frames' the same. The first 32 take a column each; the next 32
/// the same columns, half the texture further down.
void Tex8kRegion(uint32_t frame, uint32_t *x, uint32_t *y);

/// The salt texel (@p x, @p y) of level 0 holds after @p frames frames: 1 + f for the frame
/// whose region holds it, nought for one no frame wrote.
uint32_t Tex8kSaltAt(uint32_t x, uint32_t y, uint32_t frames);

/// The texel of level 0 that pixel (@p x, @p y) of a spread draw over a @p size texture fetches.
void Tex8kSpreadTexel(uint32_t size, uint32_t x, uint32_t y, uint32_t *tx, uint32_t *ty);

/// The frame whose cell holds pixel (@p x, @p y) of the target, and the texel of level 0 it
/// fetches.
uint32_t Tex8kCellTexel(uint32_t x, uint32_t y, uint32_t *tx, uint32_t *ty);

/// Where the copy-out region starts: half inside frame 3's region and half outside, so the
/// bytes it reads are the frame's on one side and the upload's on the other.
void Tex8kCopyOutOrigin(uint32_t *x, uint32_t *y);

#ifdef __cplusplus
}
#endif
