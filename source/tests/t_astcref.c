// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include "../astcblocks.h"
#include "../astcimages.h"
#include "../astcref.h"
#include "../check.h"
#include "../report.h"
#include "tests.h"

// The astc suite's expected texels come from source/astcref.c. Here it is held, block by block,
// to what ARM's reference decoder (astcenc 9fbec680) made of the same bytes, and its weight
// table to values worked out by hand from the bit replication rule.

static const char *const SUITE = "reference";

void RunReferenceAstc(void) {
    static uint16_t texels[ASTC_MAX_TEXELS][4];
    for (uint32_t i = 0; i < ASTC_TEST_BLOCKS; ++i) {
        const AstcTestBlock *const b = &ASTC_TEST_BLOCK[i];
        AstcDecodeBlock(b->bytes, b->width, b->height, texels);
        uint8_t bytes[ASTC_MAX_TEXELS * 8];
        for (uint32_t t = 0; t < b->width * b->height; ++t) {
            for (uint32_t c = 0; c < 4u; ++c) {
                bytes[t * 8u + c * 2u] = (uint8_t)texels[t][c];
                bytes[t * 8u + c * 2u + 1u] = (uint8_t)(texels[t][c] >> 8);
            }
        }
        const uint32_t hash = AstcFnv1a(bytes, b->width * b->height * 8u);
        char name[160];
        snprintf(name, sizeof(name), "the reference decodes the %s block as ARM's decoder does",
                 b->name);
        CHECK_TRUE(SUITE, name, CHECK_NONE, hash == b->hash, "hash 0x%08x, astcenc's 0x%08x",
                   (unsigned)hash, (unsigned)b->hash);
    }

    // The astcimage suite's picture, every block of each footprint, both ways astcenc decoded it:
    // to FP16 and to eight bits (the same for an sRGB image, so checked once).
    static uint8_t picture[ASTC_IMAGE_SIZE * ASTC_IMAGE_SIZE * 4];
    for (uint32_t i = 0; i < ASTC_IMAGES; ++i) {
        const AstcImage *const image = &ASTC_IMAGE[i];
        for (uint32_t way = 0; way < (image->srgb ? 1u : 2u); ++way) {
            AstcImageDecode(image, way == 1u, picture);
            const uint32_t hash = AstcFnv1a(picture, (uint32_t)sizeof(picture));
            const uint32_t want = way == 1u ? image->hash_unorm8 : image->hash;
            char name[160];
            snprintf(name, sizeof(name),
                     "the reference decodes the picture's %s blocks%s as ARM's decoder does",
                     image->name, image->srgb ? "" : way == 1u ? " to eight bits" : " to FP16");
            CHECK_TRUE(SUITE, name, CHECK_NONE, hash == want, "hash 0x%08x, astcenc's 0x%08x",
                       (unsigned)hash, (unsigned)want);
        }
    }
    // By hand: a 4-bit weight replicated to six bits, then one added above 32 (23.17).
    // 7 = 0111 -> 011101 = 29; 8 = 1000 -> 100010 = 34 -> 35; 13 = 1101 -> 110111 = 55 -> 56.
    CHECK_TRUE(SUITE, "4-bit weights 7, 8 and 13 unquantize to 29, 35 and 56", CHECK_NONE,
               AstcUnquantizeWeight(7, 16) == 29u && AstcUnquantizeWeight(8, 16) == 35u &&
                   AstcUnquantizeWeight(13, 16) == 56u,
               "got %u, %u and %u", (unsigned)AstcUnquantizeWeight(7, 16),
               (unsigned)AstcUnquantizeWeight(8, 16), (unsigned)AstcUnquantizeWeight(13, 16));
    // 3 = 00011 -> 000110 = 6; 21 = 10101 -> 101011 = 43 -> 44.
    CHECK_TRUE(SUITE, "5-bit weights 3 and 21 unquantize to 6 and 44", CHECK_NONE,
               AstcUnquantizeWeight(3, 32) == 6u && AstcUnquantizeWeight(21, 32) == 44u,
               "got %u and %u", (unsigned)AstcUnquantizeWeight(3, 32),
               (unsigned)AstcUnquantizeWeight(21, 32));
    CHECK_TRUE(SUITE, "a 3-level weight is 0, 32 or 64", CHECK_NONE,
               AstcUnquantizeWeight(0, 3) == 0u && AstcUnquantizeWeight(1, 3) == 32u &&
                   AstcUnquantizeWeight(2, 3) == 64u,
               "got %u, %u and %u", (unsigned)AstcUnquantizeWeight(0, 3),
               (unsigned)AstcUnquantizeWeight(1, 3), (unsigned)AstcUnquantizeWeight(2, 3));
    uint8_t rounded = 0;
    uint8_t truncated = 0;
    AstcUnorm8Answers(0x8000u, &rounded, &truncated);
    // 0x8000 / 65535 * 255 = 127.50195, so 128 rounded; the top byte is 0x80.
    CHECK_TRUE(SUITE, "the 16-bit texel 0x8000 is 128 either way to eight bits", CHECK_NONE,
               rounded == 128u && truncated == 128u, "got %u and %u", rounded, truncated);
    AstcUnorm8Answers(0x7F80u, &rounded, &truncated);
    // 0x7F80 = 32640, * 255 / 65535 = 127.0; the top byte is 0x7F.
    CHECK_TRUE(SUITE, "the 16-bit texel 0x7f80 is 127 either way to eight bits", CHECK_NONE,
               rounded == 127u && truncated == 127u, "got %u and %u", rounded, truncated);
}
