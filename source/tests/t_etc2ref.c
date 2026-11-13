// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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
// The blocks from "columns" on each catch a decoder with one rule changed that the ten before
// them let through.

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

// What each block shows when a host uploads it as it is and samples its bytes as RGBA8, worked
// out apart from etc2ref.c straight from the bytes (an RGB8 format's alpha is 255 by its
// swizzle); an emulator that does that showed exactly these texel 0s.
// clang-format off
static const struct {
    const char *name;
    uint8_t texels[2][4];
} RAW[] = {
    { "individual", { { 0xf2, 0x0d, 0x85, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "differential", { { 0xa4, 0x2b, 0xff, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "T", { { 0x1c, 0x39, 0xf1, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "H-high", { { 0x73, 0x05, 0x1c, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "H-low", { { 0x9c, 0xfa, 0x73, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "planar", { { 0x7e, 0x01, 0x04, 0xff }, { 0xff, 0xfa, 0xb0, 0xff } } },
    { "pta-diff", { { 0x53, 0xa6, 0xf1, 0x94 }, { 0x93, 0x6c, 0x5a, 0x5a } } },
    { "pta-t", { { 0x05, 0xe7, 0x84, 0xb5 }, { 0x93, 0x6c, 0x5a, 0x5a } } },
    { "pta-planar", { { 0x0b, 0x49, 0xfa, 0x50 }, { 0x28, 0x1f, 0xff, 0xd1 } } },
    { "rgba", { { 0x80, 0xf0, 0x10, 0x43 }, { 0x4d, 0x59, 0x67, 0xdf } } },    { "columns", { { 0x4c, 0xb2, 0x79, 0xff }, { 0xff, 0x00, 0xf0, 0xff } } },
    { "H-equal", { { 0x35, 0x05, 0xb5, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "H-green", { { 0x41, 0xeb, 0xc1, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "edge-high", { { 0xe3, 0x1d, 0x0f, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "edge-low", { { 0x1d, 0xe3, 0xf1, 0xff }, { 0x93, 0x6c, 0x5a, 0xff } } },
    { "planar-bits", { { 0x17, 0x1b, 0x15, 0xff }, { 0xc9, 0x8e, 0x80, 0xff } } },
    { "pta-h", { { 0x6a, 0x14, 0x95, 0x71 }, { 0x93, 0x6c, 0x5a, 0x5a } } },
    { "rgba-flat", { { 0x5a, 0x0d, 0x10, 0x43 }, { 0x4d, 0x59, 0x67, 0xdf } } },
};
// clang-format on

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

    // Individual, not flipped, indices 0 to 3 along each row: column 0 is the left subblock's
    // base (44, bb, 77) plus table 2's 9, column 1 plus its 29, and (3, 0) the right subblock's
    // (cc, 22, 99) minus table 5's 80, clamped. Read along the rows the block would hold one
    // index a row.
    Decode(Find("columns"), t);
    CHECK_TRUE(SUITE, "ETC2 pixel indices run down the columns", CHECK_NONE,
               Rgba(t[0]) == 0x4dc480ffu && Rgba(t[12]) == 0x4dc480ffu &&
                   Rgba(t[1]) == 0x61d894ffu && Rgba(t[3]) == 0x7c0049ffu,
               "texels (0, 0), (0, 3), (1, 0) and (3, 0) are %08x %08x %08x %08x",
               (unsigned)Rgba(t[0]), (unsigned)Rgba(t[12]), (unsigned)Rgba(t[1]),
               (unsigned)Rgba(t[3]));
    // H mode with (66, aa, 33) twice: equal counts as not smaller, index (1 << 1) | 1 = 3,
    // distance 16, where index 2 would be 11. And (88, 22, ff) against (88, 33, 00): the same
    // red, less green, so index 3 << 1 = 6, distance 41, where 7 would be 64.
    Decode(Find("H-equal"), t);
    const uint32_t equal = Rgba(t[0]);
    Decode(Find("H-green"), t);
    CHECK_TRUE(SUITE, "an ETC2 H mode base that is equal is not smaller, and red alone does "
                      "not order two", CHECK_NONE,
               equal == 0x76ba43ffu && Rgba(t[0]) == 0xb14bffffu, "got %08x and %08x",
               (unsigned)equal, (unsigned)Rgba(t[0]));
    // Differential with a second base of exactly (31, 0, 0), from (28, 3, 1) + (3, -3, -1): in
    // range, so no T, H or planar mode. (2, 0) is the right subblock at index 2: (ff, 00, 00)
    // minus table 3's 13. And (0, 31, 31) from (3, 28, 30) + (-3, 3, 1), flipped: (2, 2) is the
    // bottom subblock at index 0, (00, ff, ff) plus table 0's 2.
    Decode(Find("edge-high"), t);
    const uint32_t edge_high = Rgba(t[2]);
    Decode(Find("edge-low"), t);
    CHECK_TRUE(SUITE, "an ETC2 second base of 31 or 0 is still inside the range", CHECK_NONE,
               edge_high == 0xf20000ffu && Rgba(t[10]) == 0x02ffffffu, "got %08x and %08x",
               (unsigned)edge_high, (unsigned)Rgba(t[10]));
    // Planar: the origin (11, 77, 50) at 6, 7 and 6 bits is (2c, 9b, cb) at (0, 0); the codes
    // over 63 rounded would be (2d, 9b, ca).
    Decode(Find("planar-bits"), t);
    CHECK_TRUE(SUITE, "an ETC2 planar colour of six bits repeats its top two bits below",
               CHECK_NONE, Rgba(t[0]) == 0x2c9bcbffu, "texel (0, 0) is %08x",
               (unsigned)Rgba(t[0]));
    // A see-through H block: index 2 at (2, 0) transparent, index 3 at (3, 0) the second base
    // (22, aa, ee) minus distance 16.
    Decode(Find("pta-h"), t);
    CHECK_TRUE(SUITE, "a see-through ETC2 punch-through H block is transparent at index 2",
               CHECK_NONE, Rgba(t[2]) == 0u && Rgba(t[3]) == 0x129adeffu,
               "texels (2, 0) and (3, 0) are %08x and %08x", (unsigned)Rgba(t[2]),
               (unsigned)Rgba(t[3]));
    // RGBA alpha with a multiplier of nought: the base, 0x5a, at every index.
    Decode(Find("rgba-flat"), t);
    uint32_t flat = 0;
    for (uint32_t i = 0; i < 16u; ++i) {
        flat += t[i][3] == 0x5Au;
    }
    CHECK_TRUE(SUITE, "ETC2 RGBA alpha with a multiplier of nought is its base", CHECK_NONE,
               flat == 16u, "%u of 16 texels have alpha 0x5a", (unsigned)flat);

    // The etc2 suite names an upload of the blocks as they are when it sees one: every block's
    // own bytes must read as one, and no block's colours may, or the note would land on a pass.
    uint32_t missed = 0, false_alarms = 0;
    const char *first_missed = "none";
    const char *first_alarm = "none";
    for (uint32_t i = 0; i < ETC2_TEST_BLOCKS; ++i) {
        const Etc2TestBlock *const b = &ETC2_TEST_BLOCK[i];
        uint8_t raw[16][4];
        memset(raw, 0, sizeof(raw));
        for (size_t r = 0; r < sizeof(RAW) / sizeof(RAW[0]); ++r) {
            if (strcmp(RAW[r].name, b->name) == 0) {
                memcpy(raw[0], RAW[r].texels[0], 4);
                memcpy(raw[1], RAW[r].texels[1], 4);
            }
        }
        if (!Etc2LooksRaw(b->bytes, raw) && missed++ == 0u) {
            first_missed = b->name;
        }
        Decode(b, t);
        if (Etc2LooksRaw(b->bytes, t) && false_alarms++ == 0u) {
            first_alarm = b->name;
        }
    }
    CHECK_TRUE(SUITE, "an ETC2 block's own bytes sampled as RGBA8 read as an upload of the block",
               CHECK_NONE, missed == 0u && sizeof(RAW) / sizeof(RAW[0]) == ETC2_TEST_BLOCKS,
               "%u of %u blocks not seen as raw, the first %s", (unsigned)missed,
               (unsigned)ETC2_TEST_BLOCKS, first_missed);
    CHECK_TRUE(SUITE, "no ETC2 block's decoded colours read as an upload of its bytes",
               CHECK_NONE, false_alarms == 0u, "%u of %u blocks would be, the first %s",
               (unsigned)false_alarms, (unsigned)ETC2_TEST_BLOCKS, first_alarm);
}
