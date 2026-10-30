// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "astcimages.h"

#include "astcref.h"

const char *const ASTC_IMAGE_REGION[ASTC_IMAGE_REGIONS] = { "gradients", "edges", "noise",
                                                            "alpha" };

uint32_t AstcImageRegion(uint32_t x, uint32_t y) {
    const uint32_t half = ASTC_IMAGE_SIZE / 2u;
    return (y >= half ? 2u : 0u) + (x >= half ? 1u : 0u);
}

const uint8_t *AstcImageBlock(const AstcImage *image, uint32_t x, uint32_t y) {
    const uint32_t across = (ASTC_IMAGE_SIZE + image->block_width - 1u) / image->block_width;
    return image->blocks + 16u * ((y / image->block_height) * across + x / image->block_width);
}

void AstcImageDecode(const AstcImage *image, bool unorm8,
                     uint8_t out[ASTC_IMAGE_SIZE * ASTC_IMAGE_SIZE * 4]) {
    static uint16_t wide[ASTC_MAX_TEXELS][4];
    static uint8_t codes[ASTC_MAX_TEXELS][4];
    const uint32_t bw = image->block_width;
    const uint32_t bh = image->block_height;
    for (uint32_t by = 0; by < ASTC_IMAGE_SIZE; by += bh) {
        for (uint32_t bx = 0; bx < ASTC_IMAGE_SIZE; bx += bw) {
            const uint8_t *const block = AstcImageBlock(image, bx, by);
            if (image->srgb) {
                AstcDecodeBlockSrgb(block, bw, bh, codes);
            } else {
                AstcDecodeBlock(block, bw, bh, wide);
            }
            // A block past the picture's edge keeps the texels inside it.
            for (uint32_t t = 0; t < bh && by + t < ASTC_IMAGE_SIZE; ++t) {
                for (uint32_t s = 0; s < bw && bx + s < ASTC_IMAGE_SIZE; ++s) {
                    uint8_t *const texel = out + 4u * ((by + t) * ASTC_IMAGE_SIZE + bx + s);
                    for (uint32_t c = 0; c < 4u; ++c) {
                        const uint16_t value = wide[t * bw + s][c];
                        texel[c] = image->srgb ? codes[t * bw + s][c]
                                   : unorm8    ? (uint8_t)(value >> 8)
                                               : AstcUnorm8Fp16(value);
                    }
                }
            }
        }
    }
}
