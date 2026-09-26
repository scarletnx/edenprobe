// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../astcblocks.h"
#include "../check.h"
#include "../etc2blocks.h"
#include "../etc2ref.h"
#include "../report.h"
#include "tests.h"

// The etc2 suite's expected texels come from source/etc2ref.c. Here the builders are held to
// the bytes the suite samples, etc2ref.c to what Mesa's decoder (26.1.6) makes of each block,
// and a few texels to values worked out by hand. etc2ref.c, Mesa and Ryubing's decoder agreed
// on 306 million blocks when it was written (notas, tasks/ryubing-gpu/artifacts/part2/etc2).

static const char *const SUITE = "reference";

static void Decode(const Etc2TestBlock *b, uint8_t texels[16][4]) {
    if (b->kind == 2u) {
        Etc2DecodeRgba(b->bytes, texels);
    } else {
        Etc2DecodeRgb(b->bytes, b->kind == 1u, texels);
    }
}

static const Etc2TestBlock *Find(const char *name) {
    for (uint32_t i = 0; i < ETC2_TEST_BLOCKS; ++i) {
        if (strcmp(ETC2_TEST_BLOCK[i].name, name) == 0) {
            return &ETC2_TEST_BLOCK[i];
        }
    }
    return &ETC2_TEST_BLOCK[0];
}

static uint32_t Rgba(const uint8_t texel[4]) {
    return ((uint32_t)texel[0] << 24) | ((uint32_t)texel[1] << 16) | ((uint32_t)texel[2] << 8) |
           texel[3];
}

void RunReferenceEtc2(void) {
    uint32_t built_wrong = 0;
    uint32_t first_built = 0;
    for (uint32_t i = 0; i < ETC2_TEST_BLOCKS; ++i) {
        uint8_t bytes[16];
        Etc2BuildTestBlock(i, bytes);
        if (memcmp(bytes, ETC2_TEST_BLOCK[i].bytes, 16) != 0 && built_wrong++ == 0) {
            first_built = i;
        }
    }
    CHECK_TRUE(SUITE, "the ETC2 builders make the blocks the etc2 suite samples", CHECK_NONE,
               built_wrong == 0, "%u of %u blocks differ, the first is %s",
               (unsigned)built_wrong, (unsigned)ETC2_TEST_BLOCKS,
               ETC2_TEST_BLOCK[first_built].name);

    for (uint32_t i = 0; i < ETC2_TEST_BLOCKS; ++i) {
        const Etc2TestBlock *const b = &ETC2_TEST_BLOCK[i];
        uint8_t texels[16][4];
        Decode(b, texels);
        const uint32_t hash = AstcFnv1a(texels, sizeof(texels));
        char name[160];
        snprintf(name, sizeof(name), "the reference decodes the ETC2 %s block as Mesa does",
                 b->name);
        CHECK_TRUE(SUITE, name, CHECK_NONE, hash == b->hash, "hash 0x%08x, Mesa's 0x%08x",
                   (unsigned)hash, (unsigned)b->hash);
    }

    uint8_t t[16][4];
    // T mode, base 2 = (f, 1, 0) extended to (ff, 11, 00), distance index 7 = 64: paint colour
    // 1 is base 2 + 64, clamped, at texel (1, 0), which has pixel index 1.
    Decode(Find("T"), t);
    CHECK_TRUE(SUITE, "an ETC2 T mode paint colour is base 2 plus the distance, clamped",
               CHECK_NONE, Rgba(t[1]) == 0xff5140ffu, "texel (1, 0) is %08x",
               (unsigned)Rgba(t[1]));
    // H mode: (ee, 66, 22) >= (33, 99, cc), so index (2 << 1) | 1 = 5, distance 32; texel
    // (0, 0) has index 0, base 1 + 32 = (10e -> ff, 86, 42).
    Decode(Find("H-high"), t);
    const uint32_t high = Rgba(t[0]);
    Decode(Find("H-low"), t);
    // Swapped bases: index 4, distance 23; (33, 99, cc) + 23 = (4a, b0, e3).
    const uint32_t low = Rgba(t[0]);
    CHECK_TRUE(SUITE, "an ETC2 H mode distance index takes its last bit from the base order",
               CHECK_NONE, high == 0xff8642ffu && low == 0x4ab0e3ffu, "got %08x and %08x",
               (unsigned)high, (unsigned)low);
    // Planar: the origin (63, 0, 32) at 6, 7 and 6 bits extends to (ff, 00, 82) at (0, 0).
    Decode(Find("planar"), t);
    CHECK_TRUE(SUITE, "an ETC2 planar block starts from its origin colour", CHECK_NONE,
               Rgba(t[0]) == 0xff0082ffu, "texel (0, 0) is %08x", (unsigned)Rgba(t[0]));
    // Punch-through, not opaque: index 2 at (2, 0) is transparent black, index 0 at (0, 0) is
    // the base (10, 20, 30) at 5 bits, (52, a5, f7), with no modifier.
    Decode(Find("pta-diff"), t);
    CHECK_TRUE(SUITE, "a see-through ETC2 punch-through block has no modifier at index 0",
               CHECK_NONE, Rgba(t[2]) == 0u && Rgba(t[0]) == 0x52a5f7ffu,
               "texels (2, 0) and (0, 0) are %08x and %08x", (unsigned)Rgba(t[2]),
               (unsigned)Rgba(t[0]));
    // RGBA alpha: base 128, multiplier 15, table 0; index 3 at (3, 0) gives 128 - 225, clamped
    // to 0, and index 7 at (3, 1) gives 128 + 210, clamped to 255.
    Decode(Find("rgba"), t);
    CHECK_TRUE(SUITE, "ETC2 RGBA alpha is base plus modifier times multiplier, clamped",
               CHECK_NONE, t[3][3] == 0u && t[7][3] == 255u && t[0][3] == 83u,
               "alpha at (3, 0), (3, 1) and (0, 0) is %u, %u and %u", t[3][3], t[7][3], t[0][3]);
}
