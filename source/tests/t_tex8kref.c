// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../astcimages.h"
#include "../check.h"
#include "../report.h"
#include "../tex8kref.h"
#include "tests.h"

// The tex8k suite's judge, source/tex8kref.c. The BC7 and BC1 blocks are held to bytes and
// texels Pillow's BCn decoder (12.3.0, DDS DX10 format 98 and DXT1) gave for the same blocks; the
// ASTC texels to the picture astcenc's decode pins (astcimages.h), and BC7's bound on them to the
// texel each region's bound was measured at; the regions, levels, samples and raw copies to the
// places they must stay inside.

static const char *const SUITE = "reference";

static const Tex8kFormat RGBA8 = { TEX8K_RGBA8, 1, 1, 4, NULL };
static const Tex8kFormat BC1 = { TEX8K_BC1, 4, 4, 8, NULL };
static const Tex8kFormat BC7 = { TEX8K_BC7, 4, 4, 16, NULL };

static void Hex(char *out, const uint8_t *bytes, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        snprintf(out + 2u * i, 3, "%02x", bytes[i]);
    }
}

static void Bc7(void) {
    static const struct {
        uint32_t bx, by, salt;
        const char *hex;
        uint32_t texel;
        uint8_t rgba[4];
    } VECTORS[] = {
        { 1, 2, 0, "c0272c6fb8a264cd1032547698badcfe", 0, { 159, 243, 175, 101 } },
        { 1, 2, 0, "c0272c6fb8a264cd1032547698badcfe", 5, { 138, 167, 144, 118 } },
        { 1, 2, 0, "c0272c6fb8a264cd1032547698badcfe", 15, { 96, 12, 80, 154 } },
        { 2047, 2047, 16, "c09e505b823df1871032547698badcfe", 0, { 123, 181, 97, 241 } },
        { 2047, 2047, 16, "c09e505b823df1871032547698badcfe", 13, { 131, 89, 149, 46 } },
    };
    uint32_t wrong = 0;
    char detail[160] = "";
    for (size_t i = 0; i < sizeof(VECTORS) / sizeof(VECTORS[0]); ++i) {
        uint8_t block[16];
        char hex[33];
        Tex8kBlock(&BC7, VECTORS[i].bx, VECTORS[i].by, VECTORS[i].salt, block);
        Hex(hex, block, 16);
        uint8_t answers[3][4];
        const uint32_t x = VECTORS[i].bx * 4u + VECTORS[i].texel % 4u;
        const uint32_t y = VECTORS[i].by * 4u + VECTORS[i].texel / 4u;
        Tex8kTexel(&BC7, x, y, VECTORS[i].salt, answers);
        if (strcmp(hex, VECTORS[i].hex) != 0 || memcmp(answers[0], VECTORS[i].rgba, 4) != 0 ||
            !Tex8kMatches(VECTORS[i].rgba, answers) || memcmp(answers[1], answers[0], 4) != 0) {
            if (wrong++ == 0u) {
                snprintf(detail, sizeof(detail), "block %u,%u salt %u is %s, texel %u %u %u %u %u",
                         (unsigned)VECTORS[i].bx, (unsigned)VECTORS[i].by,
                         (unsigned)VECTORS[i].salt, hex, (unsigned)VECTORS[i].texel,
                         answers[0][0], answers[0][1], answers[0][2], answers[0][3]);
            }
        }
    }
    CHECK_TRUE(SUITE, "tex8k's BC7 mode 6 blocks are the bytes and texels Pillow decodes",
               CHECK_EXACT, wrong == 0u, "%u of 5 wrong; %s", (unsigned)wrong, detail);
}

static void Bc1(void) {
    uint8_t block[8];
    char hex[17];
    Tex8kBlock(&BC1, 1, 2, 0, block);
    Hex(hex, block, 8);
    uint8_t answers[3][4];
    Tex8kTexel(&BC1, 6, 11, 0, answers);
    const uint8_t want[4] = { 255, 154, 123, 255 };
    CHECK_TRUE(SUITE, "tex8k's BC1 blocks are one colour, the bytes and texels Pillow decodes",
               CHECK_EXACT,
               strcmp(hex, "cffc300300000000") == 0 && memcmp(answers[0], want, 4) == 0,
               "block 1,2 is %s, texel 6,11 %u %u %u %u", hex, answers[0][0], answers[0][1],
               answers[0][2], answers[0][3]);
    // A colour whose codes are 3, 12 and 24: repeated bits and rounding part ways on each (24 or
    // 25, 48 or 49, 198 or 197), and both are taken, nothing else.
    uint32_t salt = 0;
    uint32_t code = 0;
    for (salt = 0; salt < 1u << 22; ++salt) {
        code = Tex8kMix(0, 0, salt) & 0xFFFFu;
        if (code == (3u << 11 | 12u << 5 | 24u)) {
            break;
        }
    }
    Tex8kTexel(&BC1, 0, 0, salt, answers);
    const uint8_t a[4] = { 24, 48, 198, 255 };
    const uint8_t b[4] = { 25, 49, 197, 255 };
    const uint8_t c[4] = { 26, 48, 198, 255 };
    CHECK_TRUE(SUITE, "tex8k's BC1 takes either way a five or six bit code becomes eight",
               CHECK_EXACT,
               salt < 1u << 22 && Tex8kMatches(a, answers) && Tex8kMatches(b, answers) &&
                   !Tex8kMatches(c, answers),
               "salt %u, code %04x: answers %u/%u %u/%u %u/%u", (unsigned)salt, (unsigned)code,
               answers[0][0], answers[1][0], answers[0][1], answers[1][1], answers[0][2],
               answers[1][2]);
}

static void Rgba8(void) {
    uint8_t texel[4];
    Tex8kBlock(&RGBA8, 8191, 8191, 16, texel);
    // 8191 | 8191 << 13 | 16 << 26
    const uint8_t want[4] = { 0xFF, 0xFF, 0xFF, 0x43 };
    uint8_t filled[3 * 4 * 2];
    Tex8kFillBlocks(&RGBA8, filled, 12, 8189, 8190, 3, 2, 16);
    uint32_t wrong = 0;
    for (uint32_t r = 0; r < 2u; ++r) {
        for (uint32_t i = 0; i < 3u; ++i) {
            uint8_t one[4];
            Tex8kBlock(&RGBA8, 8189u + i, 8190u + r, 16, one);
            wrong += memcmp(one, filled + r * 12u + i * 4u, 4) != 0 ? 1u : 0u;
        }
    }
    // A salt past 63 folds into 1 to 63, never nought: frame 63's salt, 64, reads as 1.
    uint8_t folded[4];
    Tex8kBlock(&RGBA8, 0, 0, TEX8K_FRAMES, folded);
    uint8_t filled64[4];
    Tex8kFillBlocks(&RGBA8, filled64, 4, 0, 0, 1, 1, TEX8K_FRAMES);
    const uint8_t want64[4] = { 0x00, 0x00, 0x00, 0x04 };
    CHECK_TRUE(SUITE, "tex8k's RGBA8 texels are their place and salt, filled a word at a time",
               CHECK_EXACT,
               memcmp(texel, want, 4) == 0 && wrong == 0u && memcmp(folded, want64, 4) == 0 &&
                   memcmp(filled64, want64, 4) == 0 && Tex8kRgba8Salt(63) == 63u &&
                   Tex8kRgba8Salt(0) == 0u,
               "texel 8191,8191 of salt 16 is %02x%02x%02x%02x; %u of 6 filled texels differ; "
               "texel 0,0 of salt %u is %02x%02x%02x%02x, filled %02x%02x%02x%02x",
               texel[0], texel[1], texel[2], texel[3], (unsigned)wrong, (unsigned)TEX8K_FRAMES,
               folded[0], folded[1], folded[2], folded[3], filled64[0], filled64[1], filled64[2],
               filled64[3]);
}

static void Astc(void) {
    // Every 37th texel of a 1024x1024 corner, in both footprints and two salts: what tex8k's
    // judge expects is the picture's texel its block came from, as astcenc's decode has it.
    static uint8_t picture[2][ASTC_IMAGE_SIZE * ASTC_IMAGE_SIZE * 4];
    const AstcImage *const images[2] = { &ASTC_IMAGE[0], &ASTC_IMAGE[2] };
    uint32_t wrong = 0;
    uint32_t checked = 0;
    char detail[160] = "";
    for (uint32_t k = 0; k < 2u; ++k) {
        const AstcImage *const image = images[k];
        AstcImageDecode(image, false, picture[k]);
        const Tex8kFormat format = { TEX8K_ASTC, image->block_width, image->block_height, 16,
                                     image };
        for (uint32_t salt = 0; salt < 17u; salt += 16u) {
            for (uint32_t y = 3; y < 1024u; y += 37u) {
                for (uint32_t x = 5; x < 1024u; x += 37u) {
                    uint32_t px = 0;
                    uint32_t py = 0;
                    Tex8kAstcSource(&format, x / image->block_width, y / image->block_height,
                                    salt, &px, &py);
                    const uint32_t sx = px * image->block_width + x % image->block_width;
                    const uint32_t sy = py * image->block_height + y % image->block_height;
                    const uint8_t *const want = picture[k] + (sy * ASTC_IMAGE_SIZE + sx) * 4u;
                    uint8_t answers[3][4];
                    Tex8kTexel(&format, x, y, salt, answers);
                    ++checked;
                    if (memcmp(answers[0], want, 4) != 0 && wrong++ == 0u) {
                        snprintf(detail, sizeof(detail),
                                 "%s texel %u,%u salt %u: %u %u %u %u, the picture's %u,%u %u %u "
                                 "%u %u",
                                 image->name, (unsigned)x, (unsigned)y, (unsigned)salt,
                                 answers[0][0], answers[0][1], answers[0][2], answers[0][3],
                                 (unsigned)sx, (unsigned)sy, want[0], want[1], want[2], want[3]);
                    }
                }
            }
        }
    }
    CHECK_TRUE(SUITE, "tex8k's ASTC texels are the picture's, from the block their tile shifts in",
               CHECK_EXACT, wrong == 0u, "%u of %u wrong; %s", (unsigned)wrong, (unsigned)checked,
               detail);
    // Each region's bound is the distance from the picture to an encoder's texel there, the worst
    // one measured: the texel and what Pillow decoded that encoder's block to.
    static const struct {
        uint32_t image;
        uint32_t x, y;
        uint8_t encoded[4];
    } WORST[ASTC_IMAGE_REGIONS] = {
        { 0, 47, 43, { 165, 169, 129, 255 } },  // 4x4, bc7enc level 0, perceptual
        { 2, 115, 55, { 112, 167, 116, 255 } }, // 8x8, bc7enc level 2, perceptual
        { 0, 44, 99, { 92, 122, 91, 255 } },    // 4x4, bc7enc level 2, perceptual
        { 2, 105, 64, { 131, 134, 161, 88 } },  // 8x8, bc7enc level 0, perceptual
    };
    static uint8_t unorm8[ASTC_IMAGE_SIZE * ASTC_IMAGE_SIZE * 4];
    uint32_t bounds_wrong = 0;
    char bounds_detail[160] = "";
    for (uint32_t r = 0; r < ASTC_IMAGE_REGIONS; ++r) {
        const AstcImage *const image = &ASTC_IMAGE[WORST[r].image];
        const uint32_t k = WORST[r].image == 0u ? 0u : 1u;
        AstcImageDecode(image, true, unorm8);
        const size_t at = ((size_t)WORST[r].y * ASTC_IMAGE_SIZE + WORST[r].x) * 4u;
        uint8_t decodes[3][4];
        memcpy(decodes[0], picture[k] + at, 4);
        memcpy(decodes[1], unorm8 + at, 4);
        memcpy(decodes[2], picture[k] + at, 4);
        const uint32_t distance = Tex8kDistance(WORST[r].encoded, decodes);
        if ((AstcImageRegion(WORST[r].x, WORST[r].y) != r || distance != TEX8K_BC7_BOUND[r]) &&
            bounds_wrong++ == 0u) {
            snprintf(bounds_detail, sizeof(bounds_detail),
                     "%s: texel %u,%u of region %u is %u from the encoder's, the bound %u",
                     ASTC_IMAGE_REGION[r], (unsigned)WORST[r].x, (unsigned)WORST[r].y,
                     (unsigned)AstcImageRegion(WORST[r].x, WORST[r].y), (unsigned)distance,
                     (unsigned)TEX8K_BC7_BOUND[r]);
        }
    }
    // The texel a tex8k texture holds sits in the region its source texel of the picture does.
    const Tex8kFormat astc4 = { TEX8K_ASTC, 4, 4, 16, &ASTC_IMAGE[0] };
    uint32_t px = 0;
    uint32_t py = 0;
    Tex8kAstcSource(&astc4, 1000u / 4u, 3000u / 4u, 5, &px, &py);
    const uint32_t region = AstcImageRegion(px * 4u + 1000u % 4u, py * 4u + 3000u % 4u);
    CHECK_TRUE(SUITE, "tex8k's BC7 bound a region is the worst texel measured there",
               CHECK_EXACT,
               bounds_wrong == 0u && Tex8kAstcRegion(&astc4, 1000, 3000, 5) == region,
               "%u of 4 wrong; %s; texel 1000,3000 of salt 5 in region %u, its source's %u",
               (unsigned)bounds_wrong, bounds_detail,
               (unsigned)Tex8kAstcRegion(&astc4, 1000, 3000, 5), (unsigned)region);
    // Two blocks a tile apart take different blocks of the picture, and so does a frame.
    const Tex8kFormat format = { TEX8K_ASTC, 4, 4, 16, &ASTC_IMAGE[0] };
    uint32_t a[2];
    uint32_t b[2];
    uint32_t c[2];
    Tex8kAstcSource(&format, 7, 9, 0, &a[0], &a[1]);
    Tex8kAstcSource(&format, 7 + 32, 9, 0, &b[0], &b[1]);
    Tex8kAstcSource(&format, 7, 9, 1, &c[0], &c[1]);
    CHECK_TRUE(SUITE, "tex8k's ASTC tiles and frames take other blocks of the picture",
               CHECK_EXACT,
               (a[0] != b[0] || a[1] != b[1]) && (a[0] != c[0] || a[1] != c[1]),
               "block 7,9 takes %u,%u, a tile across %u,%u, in frame 0 %u,%u", (unsigned)a[0],
               (unsigned)a[1], (unsigned)b[0], (unsigned)b[1], (unsigned)c[0], (unsigned)c[1]);
}

static void Places(void) {
    uint32_t problems = 0;
    char detail[160] = "";
    // The regions inside the texture, apart, and each the salt of its own frame.
    for (uint32_t f = 0; f < TEX8K_FRAMES; ++f) {
        uint32_t x = 0;
        uint32_t y = 0;
        Tex8kRegion(f, &x, &y);
        for (uint32_t g = 0; g < f; ++g) {
            uint32_t gx = 0;
            uint32_t gy = 0;
            Tex8kRegion(g, &gx, &gy);
            if (gx == x && gy == y) {
                ++problems;
                snprintf(detail, sizeof(detail), "regions %u and %u share a tile", (unsigned)g,
                         (unsigned)f);
            }
        }
        if (x + TEX8K_REGION > 8192u || y + TEX8K_REGION > 8192u ||
            Tex8kSaltAt(x, y, TEX8K_FRAMES) != f + 1u ||
            Tex8kSaltAt(x + TEX8K_REGION - 1u, y + TEX8K_REGION - 1u, TEX8K_FRAMES) != f + 1u ||
            Tex8kSaltAt(x, y, f) != 0u) {
            ++problems;
            snprintf(detail, sizeof(detail), "region %u at %u,%u", (unsigned)f, (unsigned)x,
                     (unsigned)y);
        }
    }
    // The levels' squares inside level 0, whole 8x8 blocks, the BC7 texture's whole chain too.
    for (uint32_t level = 1; level < TEX8K_BC7_LEVELS; ++level) {
        uint32_t x = 0;
        uint32_t y = 0;
        Tex8kLevelOrigin(8192u, level, &x, &y);
        uint32_t below = 0;
        uint32_t ignored = 0;
        Tex8kLevelOrigin(8192u, level - 1u, &below, &ignored);
        if (x % 8u != 0u || x + (8192u >> level) > 8192u || x != y ||
            (level > 1u && x <= below)) {
            ++problems;
            snprintf(detail, sizeof(detail), "level %u from %u,%u", (unsigned)level, (unsigned)x,
                     (unsigned)y);
        }
    }
    // The samples inside the texture, at both sizes, and a cell's inside its region.
    uint32_t x = 0;
    uint32_t y = 0;
    Tex8kSpreadTexel(8192u, TEX8K_TARGET - 1u, TEX8K_TARGET - 1u, &x, &y);
    uint32_t x16 = 0;
    uint32_t y16 = 0;
    Tex8kSpreadTexel(16384u, TEX8K_TARGET - 1u, TEX8K_TARGET - 1u, &x16, &y16);
    if (x >= 8192u || y >= 8192u || x16 >= 16384u || y16 >= 16384u) {
        ++problems;
        snprintf(detail, sizeof(detail), "the last spread texel %u,%u and %u,%u", (unsigned)x,
                 (unsigned)y, (unsigned)x16, (unsigned)y16);
    }
    for (uint32_t p = 0; p < TEX8K_TARGET * TEX8K_TARGET; ++p) {
        const uint32_t f = Tex8kCellTexel(p % TEX8K_TARGET, p / TEX8K_TARGET, &x, &y);
        if (f >= TEX8K_FRAMES || Tex8kSaltAt(x, y, TEX8K_FRAMES) != f + 1u) {
            ++problems;
            snprintf(detail, sizeof(detail), "pixel %u fetches %u,%u for frame %u", (unsigned)p,
                     (unsigned)x, (unsigned)y, (unsigned)f);
        }
    }
    // The copy-out half in frame 3's region: of its 256x256 texels a quarter, 128x128.
    Tex8kCopyOutOrigin(&x, &y);
    uint32_t in_frame = 0;
    for (uint32_t ty = y; ty < y + TEX8K_REGION; ++ty) {
        for (uint32_t tx = x; tx < x + TEX8K_REGION; ++tx) {
            in_frame += Tex8kSaltAt(tx, ty, TEX8K_FRAMES) == 4u ? 1u : 0u;
        }
    }
    if (in_frame != 128u * 128u) {
        ++problems;
        snprintf(detail, sizeof(detail), "the copy-out at %u,%u holds %u of frame 3's texels",
                 (unsigned)x, (unsigned)y, (unsigned)in_frame);
    }
    // The processor's square in the pitch linear texture: clear of every frame, its rows across
    // a page edge, and inside the draw that judges it with texels on every side.
    const uint32_t from_x = TEX8K_EDGE_FROM_X;
    const uint32_t to_x = from_x + (TEX8K_TARGET - 1u) * TEX8K_EDGE_STEP;
    const uint32_t from_y = TEX8K_EDGE_FROM_Y;
    const uint32_t to_y = from_y + (TEX8K_TARGET - 1u) * TEX8K_EDGE_STEP;
    uint32_t in_frames = 0;
    for (uint32_t ty = from_y; ty <= to_y; ++ty) {
        for (uint32_t tx = from_x; tx <= to_x; ++tx) {
            in_frames += Tex8kSaltAt(tx, ty, TEX8K_FRAMES) != 0u ? 1u : 0u;
        }
    }
    const uint32_t first_page = TEX8K_EDGE_X * 4u / 4096u;
    const uint32_t last_page = (TEX8K_EDGE_X + TEX8K_EDGE_SIDE - 1u) * 4u / 4096u;
    if (in_frames != 0u || first_page == last_page || from_x >= TEX8K_EDGE_X ||
        to_x < TEX8K_EDGE_X + TEX8K_EDGE_SIDE || from_y >= TEX8K_EDGE_Y ||
        to_y < TEX8K_EDGE_Y + TEX8K_EDGE_SIDE) {
        ++problems;
        snprintf(detail, sizeof(detail),
                 "the edge square at %u,%u: %u texels of its draw in a frame's region",
                 (unsigned)TEX8K_EDGE_X, (unsigned)TEX8K_EDGE_Y, (unsigned)in_frames);
    }
    CHECK_TRUE(SUITE, "tex8k's regions, levels, samples and copy-out sit where its judge says",
               CHECK_NONE, problems == 0u, "%u problems; the last: %s", (unsigned)problems,
               detail);
}

/// Counts the blocks of @p level the raw copy [@p from, @p to) covers, and the box they span.
static uint32_t Covered(const BcnArray *a, uint32_t level, uint32_t from, uint32_t to,
                        uint32_t box[4]) {
    uint32_t count = 0;
    box[0] = box[1] = UINT32_MAX;
    box[2] = box[3] = 0;
    for (uint32_t by = 0; by < BcnLevelBlocksY(a, level); ++by) {
        for (uint32_t bx = 0; bx < BcnLevelBlocksX(a, level); ++bx) {
            if (!Tex8kRawCovers(a, level, bx, by, from, to)) {
                continue;
            }
            ++count;
            box[0] = bx < box[0] ? bx : box[0];
            box[1] = by < box[1] ? by : box[1];
            box[2] = bx > box[2] ? bx : box[2];
            box[3] = by > box[3] ? by : box[3];
        }
    }
    return count;
}

static void Raw(void) {
    // The BC7 texture as deko3d lays out its fourteen levels (dk_image.cpp calcLevelOffset).
    BcnArray a;
    BcnArrayLayout(&a, 8192, 8192, 1, TEX8K_BC7_LEVELS, 16);
    const uint32_t cross = BcnLevelOffset(&a, TEX8K_CROSS_LEVEL);
    const uint32_t tail = BcnLevelOffset(&a, TEX8K_TAIL_LEVEL);
    const uint32_t end = BcnLevelOffset(&a, TEX8K_BC7_LEVELS);
    uint32_t problems = 0;
    char detail[192] = "";
    if (cross != 89128960u || tail != 89477120u || end != 89480704u || tail % 4096u != 0u ||
        end - tail > 4096u) {
        ++problems;
        snprintf(detail, sizeof(detail), "levels 4, 8 and the end at %u, %u, %u", (unsigned)cross,
                 (unsigned)tail, (unsigned)end);
    }
    // Across levels 3 and 4: the last 4 KiB of level 3 are the bottom half of its last tile, the
    // first 4 KiB of level 4 the top half of its first.
    uint32_t box[4];
    const uint32_t from = cross - TEX8K_CROSS_BYTES;
    const uint32_t to = cross + TEX8K_CROSS_BYTES;
    const uint32_t three = Covered(&a, 3, from, to, box);
    if (three != 256u || box[0] != 252u || box[1] != 192u || box[2] != 255u || box[3] != 255u) {
        ++problems;
        snprintf(detail, sizeof(detail),
                 "the copy across holds %u of level 3's blocks, %u,%u to %u,%u", (unsigned)three,
                 (unsigned)box[0], (unsigned)box[1], (unsigned)box[2], (unsigned)box[3]);
    }
    const uint32_t four = Covered(&a, 4, from, to, box);
    if (four != 256u || box[0] != 0u || box[1] != 0u || box[2] != 3u || box[3] != 63u) {
        ++problems;
        snprintf(detail, sizeof(detail),
                 "the copy across holds %u of level 4's blocks, %u,%u to %u,%u", (unsigned)four,
                 (unsigned)box[0], (unsigned)box[1], (unsigned)box[2], (unsigned)box[3]);
    }
    // Into the tail: the right half of level 9 and all of level 10, nothing of the others.
    static const uint32_t TAIL_BLOCKS[TEX8K_BC7_LEVELS - TEX8K_TAIL_LEVEL] = { 0, 8, 4, 0, 0, 0 };
    for (uint32_t level = TEX8K_TAIL_LEVEL; level < TEX8K_BC7_LEVELS; ++level) {
        const uint32_t n = Covered(&a, level, tail + TEX8K_TAIL_FROM, tail + TEX8K_TAIL_TO, box);
        if (n != TAIL_BLOCKS[level - TEX8K_TAIL_LEVEL] || (level == 9u && box[0] != 2u)) {
            ++problems;
            snprintf(detail, sizeof(detail), "the copy into the tail holds %u of level %u's blocks",
                     (unsigned)n, (unsigned)level);
        }
    }
    // The bytes: each covered block where the layout puts it, as Tex8kBlock writes it from the
    // level's origin.
    static uint8_t bytes[2u * TEX8K_CROSS_BYTES];
    const uint32_t made = Tex8kRawBytes(&BC7, &a, from, to, TEX8K_CROSS_SALT, bytes);
    uint32_t ox = 0;
    uint32_t oy = 0;
    Tex8kLevelOrigin(8192u, 4u, &ox, &oy);
    uint8_t want[16];
    Tex8kBlock(&BC7, ox / 4u + 3u, oy / 4u + 63u, TEX8K_CROSS_SALT, want);
    const uint32_t at = BcnBlockOffset(&a, 0, 4, 3, 63) - from;
    static uint8_t tail_bytes[TEX8K_TAIL_TO - TEX8K_TAIL_FROM];
    const uint32_t tail_made = Tex8kRawBytes(&BC7, &a, tail + TEX8K_TAIL_FROM, tail + TEX8K_TAIL_TO,
                                             TEX8K_TAIL_SALT, tail_bytes);
    if (made != 512u || memcmp(bytes + at, want, 16) != 0 || tail_made != 12u) {
        ++problems;
        snprintf(detail, sizeof(detail),
                 "%u blocks across, %u into the tail; level 4's block 3,63 %s", (unsigned)made,
                 (unsigned)tail_made, memcmp(bytes + at, want, 16) == 0 ? "right" : "wrong");
    }
    CHECK_TRUE(SUITE, "tex8k's raw copies into the BC7 texture cover the blocks its judge expects",
               CHECK_NONE, problems == 0u, "%u problems; the last: %s", (unsigned)problems,
               detail);
}

static void Numbers(void) {
    // numpy.percentile of 100, 200, ... 6400: the 99th at 62.37 is 6337, the 50th 3250.
    uint64_t values[64];
    for (uint32_t i = 0; i < 64u; ++i) {
        values[i] = 100u * (i + 1u);
    }
    const uint64_t p99 = Tex8kPercentile(values, 64, 99);
    const uint64_t p50 = Tex8kPercentile(values, 64, 50);
    const uint64_t top = Tex8kPercentile(values, 64, 100);
    // A texel 7 from the nearest answer in its worst channel, nought when it is one.
    const uint8_t answers[3][4] = { { 10, 20, 30, 40 }, { 11, 20, 30, 40 }, { 9, 20, 30, 40 } };
    const uint8_t off[4] = { 13, 20, 37, 40 };
    const uint8_t on[4] = { 11, 20, 30, 40 };
    CHECK_TRUE(SUITE, "tex8k's percentile is numpy's and its distance the worst channel's",
               CHECK_EXACT,
               p99 == 6337u && p50 == 3250u && top == 6400u && Tex8kDistance(off, answers) == 7u &&
                   Tex8kDistance(on, answers) == 0u,
               "p99 %llu, p50 %llu, p100 %llu; distances %u and %u", (unsigned long long)p99,
               (unsigned long long)p50, (unsigned long long)top,
               (unsigned)Tex8kDistance(off, answers), (unsigned)Tex8kDistance(on, answers));
}

void RunReferenceTex8k(void) {
    Bc7();
    Bc1();
    Rgba8();
    Astc();
    Places();
    Raw();
    Numbers();
}
