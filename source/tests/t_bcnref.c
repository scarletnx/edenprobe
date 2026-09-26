// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include "../bcnref.h"
#include "../check.h"
#include "../report.h"
#include "tests.h"

// The bcn suite writes its blocks where source/bcnref.c says deko3d laid the array out. Here
// that layout is held to offsets worked out by hand from deko3d 0.5.0's rules
// (source/dk_image.cpp: pickTileSize, adjustTileSize, calcLevelOffset, alignLayerSize), and the
// solid BC4 block to its bytes.

static const char *const SUITE = "reference";

void RunReferenceBcn(void) {
    BcnArray a;
    // 4x4 BC4, three levels, six layers: one block per level, rows 1 * 1.5 -> one gob, so every
    // level is one 512-byte gob and a layer is three of them.
    BcnArrayLayout(&a, 4, 4, 6, 3, 8);
    CHECK_TRUE(SUITE, "a 4x4 BC4 array's levels are one gob each and a layer is three",
               CHECK_NONE,
               BcnLevelOffset(&a, 1) == 512u && BcnLevelOffset(&a, 2) == 1024u &&
                   a.layer_size == 1536u && BcnBlockOffset(&a, 5, 2, 0, 0) == 5u * 1536u + 1024u,
               "levels at %u and %u, layer size %u, layer 5 level 2 at %u",
               (unsigned)BcnLevelOffset(&a, 1), (unsigned)BcnLevelOffset(&a, 2),
               (unsigned)a.layer_size, (unsigned)BcnBlockOffset(&a, 5, 2, 0, 0));
    // 8x6: 2x2 blocks in one gob; block (1, 1) is 8 bytes across and one row down, which the
    // gob puts at 16 + 8.
    BcnArrayLayout(&a, 8, 6, 2, 1, 8);
    CHECK_TRUE(SUITE, "a BC4 block one down and one across sits 24 bytes into its gob",
               CHECK_NONE, BcnBlockOffset(&a, 0, 0, 1, 1) == 24u && a.layer_size == 512u,
               "block (1, 1) at %u, layer size %u", (unsigned)BcnBlockOffset(&a, 0, 0, 1, 1),
               (unsigned)a.layer_size);
    // 64x64: 16 block rows * 1.5 = 24 rows, three gobs, so a tile of two gobs. Level 0 is two
    // gobs wide (128 bytes) and one tile tall: 2048. Levels 1 and 2 (8 and 4 block rows) shrink
    // to one-gob tiles: 512 each. 3072 is already a multiple of the 1024-byte tile.
    BcnArrayLayout(&a, 64, 64, 2, 3, 8);
    CHECK_TRUE(SUITE, "a 64x64 BC4 array uses two-gob tiles on level 0 and one-gob tiles below",
               CHECK_NONE,
               a.tile_shift == 1u && BcnLevelOffset(&a, 1) == 2048u &&
                   BcnLevelOffset(&a, 2) == 2560u && a.layer_size == 3072u,
               "tile shift %u, levels at %u and %u, layer size %u", (unsigned)a.tile_shift,
               (unsigned)BcnLevelOffset(&a, 1), (unsigned)BcnLevelOffset(&a, 2),
               (unsigned)a.layer_size);
    uint8_t block[8];
    BcnSolidBc4(block, 0x5A);
    CHECK_TRUE(SUITE, "a solid BC4 block is its value twice and sixteen nought indices",
               CHECK_NONE,
               block[0] == 0x5Au && block[1] == 0x5Au && block[2] == 0u && block[7] == 0u,
               "bytes %02x %02x %02x .. %02x", block[0], block[1], block[2], block[7]);
}
