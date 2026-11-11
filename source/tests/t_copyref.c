// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../copyref.h"
#include "../report.h"
#include "tests.h"

// The copy suite's rules, against answers worked out by hand: that every texel of its pictures
// says where it came from, the bytes a texel of nought and full channels becomes in each format,
// and where a flipped, layered, scaled or padded copy puts a texel.

static const char *const SUITE = "reference";

static void Pictures(void) {
    // Every texel of every layer differs from every other: a copy that moved one is told apart.
    uint32_t repeated = 0;
    for (uint32_t a = 0; a < COPY_SIDE * COPY_SIDE * COPY_LAYERS; ++a) {
        const uint32_t ta =
            CopyTexel(a % COPY_SIDE, a / COPY_SIDE % COPY_SIDE, a / (COPY_SIDE * COPY_SIDE));
        for (uint32_t b = a + 1u; b < COPY_SIDE * COPY_SIDE * COPY_LAYERS; ++b) {
            repeated += ta == CopyTexel(b % COPY_SIDE, b / COPY_SIDE % COPY_SIDE,
                                        b / (COPY_SIDE * COPY_SIDE));
        }
    }
    // Every 4 by 4 square of the full texel picture holds all sixteen combinations of nought and
    // full in its four channels.
    uint32_t squares_short = 0;
    for (uint32_t sy = 0; sy < COPY_SIDE; sy += 4u) {
        for (uint32_t sx = 0; sx < COPY_SIDE; sx += 4u) {
            uint32_t seen = 0;
            for (uint32_t i = 0; i < 16u; ++i) {
                const uint32_t t = CopyFullTexel(sx + i % 4u, sy + i / 4u);
                const uint32_t combination =
                    (t & 1u) | (t >> 7 & 2u) | (t >> 14 & 4u) | (t >> 21 & 8u);
                seen |= 1u << combination;
            }
            squares_short += seen != 0xFFFFu;
        }
    }
    const bool hand = CopyTexel(0, 0, 0) == 0xA5400000u && CopyTexel(15, 3, 2) == 0xA542030Fu &&
                      CopyFullTexel(1, 2) == 0xFF0000FFu && CopyFullTexel(2, 1) == 0x00FFFF00u;
    CHECK_TRUE(SUITE,
               "the copy suite's pictures give every texel a value of its own, and the full "
               "texels every combination of nought and full",
               CHECK_NONE, repeated == 0u && squares_short == 0u && hand,
               "%u texels repeat another, %u squares lack a combination, hand-worked texels %d",
               (unsigned)repeated, (unsigned)squares_short, hand);
}

static bool Bytes(uint32_t texel, CopyFormat format, const uint8_t *want) {
    uint8_t got[8];
    CopyConvert(texel, format, got);
    return memcmp(got, want, CopyFormatBytes(format)) == 0;
}

static void Conversions(void) {
    // Red and alpha full: BGRA8 puts red third; RGB565 is 0xf800; RGBA16F one and one.
    static const uint8_t BGRA_RED[4] = { 0x00, 0x00, 0xFF, 0xFF };
    static const uint8_t RGB565_RED[2] = { 0x00, 0xF8 };
    static const uint8_t RGB565_GREEN[2] = { 0xE0, 0x07 };
    static const uint8_t RGB565_BLUE[2] = { 0x1F, 0x00 };
    static const uint8_t HALF_RED[8] = { 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C };
    static const uint8_t RGBA_BLUE[4] = { 0x00, 0x00, 0xFF, 0x00 };
    const bool ok =
        Bytes(0xFF0000FFu, COPY_BGRA8, BGRA_RED) && Bytes(0xFF0000FFu, COPY_RGB565, RGB565_RED) &&
        Bytes(0x0000FF00u, COPY_RGB565, RGB565_GREEN) &&
        Bytes(0x00FF0000u, COPY_RGB565, RGB565_BLUE) &&
        Bytes(0xFF0000FFu, COPY_RGBA16F, HALF_RED) && Bytes(0x00FF0000u, COPY_RGBA8, RGBA_BLUE) &&
        CopyFormatBytes(COPY_RGB565) == 2u && CopyFormatBytes(COPY_RGBA16F) == 8u &&
        CopyFormatBytes(COPY_BGRA8) == 4u;
    CHECK_TRUE(SUITE,
               "the copy suite's channels of nought and full become the bytes each format holds "
               "them in",
               CHECK_NONE, ok, "a conversion disagrees with the bytes worked out by hand");
}

static void Places(void) {
    uint32_t fx, fy, gx, gy, hx, hy;
    CopyFlipped(0, 0, COPY_SIDE, true, true, &fx, &fy);
    CopyFlipped(3, 5, COPY_SIDE, true, false, &gx, &gy);
    CopyFlipped(3, 5, COPY_SIDE, false, true, &hx, &hy);
    const bool flips = fx == 15u && fy == 15u && gx == 12u && gy == 5u && hx == 3u && hy == 10u;
    const bool layers = CopyFlippedLayer(0, COPY_LAYERS) == 3u &&
                        CopyFlippedLayer(3, COPY_LAYERS) == 0u &&
                        CopyFlippedLayer(1, COPY_LAYERS) == 2u;
    const bool scaled = CopyScaledColumn(0, 2) == 0u && CopyScaledColumn(1, 2) == 0u &&
                        CopyScaledColumn(2, 2) == 1u && CopyScaledColumn(31, 2) == 15u;
    const bool padded =
        CopyBufferOffset(1, 2, 96, 4) == 196u && CopyBufferOffset(15, 15, 96, 4) == 1500u;
    CHECK_TRUE(SUITE,
               "a flipped, layered, scaled or padded copy puts each texel where worked out by hand",
               CHECK_NONE, flips && layers && scaled && padded,
               "flips %d, layers %d, scaled %d, padded %d", flips, layers, scaled, padded);
}

void RunReferenceCopy(void) {
    Pictures();
    Conversions();
    Places();
}
