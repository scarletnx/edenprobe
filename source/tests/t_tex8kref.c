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
// ASTC texels to the picture astcenc's decode pins (astcimages.h); the regions, levels and
// samples to the sizes they must stay inside.

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
    CHECK_TRUE(SUITE, "tex8k's RGBA8 texels are their place and salt, filled a word at a time",
               CHECK_EXACT, memcmp(texel, want, 4) == 0 && wrong == 0u,
               "texel 8191,8191 of salt 16 is %02x%02x%02x%02x; %u of 6 filled texels differ",
               texel[0], texel[1], texel[2], texel[3], (unsigned)wrong);
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
            if (gx == x) {
                ++problems;
                snprintf(detail, sizeof(detail), "regions %u and %u share a column", (unsigned)g,
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
    // The levels' squares inside level 0, whole 8x8 blocks.
    for (uint32_t level = 1; level < TEX8K_LEVELS; ++level) {
        uint32_t x = 0;
        uint32_t y = 0;
        Tex8kLevelOrigin(8192u, level, &x, &y);
        if (x % 8u != 0u || x + (8192u >> level) > 8192u || x != y) {
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
    CHECK_TRUE(SUITE, "tex8k's regions, levels, samples and copy-out sit where its judge says",
               CHECK_NONE, problems == 0u, "%u problems; the last: %s", (unsigned)problems,
               detail);
}

void RunReferenceTex8k(void) {
    Bc7();
    Bc1();
    Rgba8();
    Astc();
    Places();
}
