// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The ASTC blocks the astc suite samples, one per feature of the format, and what ARM's
/// reference decoder makes of each.
///
/// Four are built by hand (tools/astc-blocks.py prints them with their fields): a void extent, a
/// block whose sixteen 4-bit weights run 0 to 15 between endpoints 0 and 255, and two whose 5-bit
/// weights cover all 32 values. The rest are the first block of a seeded random set, per
/// footprint, that has the feature, chosen by source/astcref.c's reading of their fields.
/// `hash` is FNV-1a over astcenc's 16-bit texels (commit 9fbec680, the LDR profile), row major,
/// RGBA little endian: the reference suite holds astcref.c to it block by block.
typedef struct {
    const char *name;
    const char *sentence;
    uint32_t width;
    uint32_t height;
    uint8_t bytes[16];
    uint32_t hash;
} AstcTestBlock;

#define ASTC_TEST_BLOCKS 16u

extern const AstcTestBlock ASTC_TEST_BLOCK[ASTC_TEST_BLOCKS];

/// FNV-1a over @p size bytes, the hash astcenc's answers are pinned with.
uint32_t AstcFnv1a(const void *data, uint32_t size);

#ifdef __cplusplus
}
#endif
