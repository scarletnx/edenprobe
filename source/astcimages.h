// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// One 128x128 picture, encoded by ARM's astcenc at the footprints the astcimage suite samples.
///
/// tools/astc-images.py makes the picture (four 64x64 regions: gradients, hard edges where three
/// colours meet in a block, fine noise, and alpha, a ramp above and a cutout below) and has
/// astcenc (commit 9fbec680, -medium) encode it as a game's tools would, the sRGB ones in its sRGB
/// mode. The hashes are FNV-1a over what astcenc decodes the blocks to, eight bits a channel, RGBA
/// row major over the 128x128 texels, the two ways an implementation may decode a linear image:
/// `hash` to FP16, the specification's default, rounded to eight bits as a target would
/// (AstcUnorm8Fp16), and `hash_unorm8` to eight bits (decode_unorm8, C's top byte). An sRGB image
/// is C's top byte, the sRGB code, both ways. The reference suite holds source/astcref.c to both.
typedef struct {
    const char *name;
    uint32_t block_width;
    uint32_t block_height;
    bool srgb;
    /// Row major, a row of blocks after another, as an .astc file holds them.
    const uint8_t *blocks;
    uint32_t block_count;
    uint32_t hash;
    uint32_t hash_unorm8;
} AstcImage;

#define ASTC_IMAGE_SIZE 128u
#define ASTC_IMAGES 6u

/// The regions, each a quarter of the picture: top left, top right, bottom left, bottom right.
#define ASTC_IMAGE_REGIONS 4u
extern const char *const ASTC_IMAGE_REGION[ASTC_IMAGE_REGIONS];

extern const AstcImage ASTC_IMAGE[ASTC_IMAGES];

/// FNV-1a over the picture astcenc was given, so a change to the tool's picture shows here.
extern const uint32_t ASTC_IMAGE_SOURCE_HASH;

/// Which region texel (@p x, @p y) of the picture lies in, an index into ASTC_IMAGE_REGION.
uint32_t AstcImageRegion(uint32_t x, uint32_t y);

/// The block of @p image that holds texel (@p x, @p y).
const uint8_t *AstcImageBlock(const AstcImage *image, uint32_t x, uint32_t y);

/// What source/astcref.c decodes the whole of @p image to, eight bits a channel, RGBA row major:
/// for a linear image the FP16 decode's byte (AstcUnorm8Fp16), or with @p unorm8 C's top byte;
/// for an sRGB one C's top byte (the sRGB code) either way. What `hash` and `hash_unorm8` are
/// taken over.
void AstcImageDecode(const AstcImage *image, bool unorm8,
                     uint8_t out[ASTC_IMAGE_SIZE * ASTC_IMAGE_SIZE * 4]);

#ifdef __cplusplus
}
#endif
