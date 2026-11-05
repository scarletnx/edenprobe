// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>

#include "astcref.h"
#include "tex8kref.h"

uint32_t Tex8kMix(uint32_t x, uint32_t y, uint32_t salt) {
    uint32_t h = x * 0x9E3779B1u ^ y * 0x85EBCA77u ^ salt * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

/// Puts the low @p count bits (at most 32) of @p value at bit @p *at of the 128-bit block
/// @p bits, low word first, and moves @p *at past them. A word at a time: the suite encodes four
/// million blocks a texture.
static void PutBits(uint64_t bits[2], uint32_t *at, uint64_t value, uint32_t count) {
    value &= (1ull << count) - 1u;
    const uint32_t word = *at / 64u;
    const uint32_t shift = *at % 64u;
    bits[word] |= value << shift;
    if (shift != 0u && shift + count > 64u) {
        bits[word + 1u] |= value >> (64u - shift);
    }
    *at += count;
}

// BC7's weights for a four bit index (the BC7 format's table, which mode 6 uses).
static const uint8_t BC7_WEIGHTS4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

/// Mode 6's two endpoints and p-bits for block (@p bx, @p by) and @p salt: endpoint 0 is seven
/// bits a channel of the mix, endpoint 1 its complement, so the block runs between two colours
/// that differ in every channel.
static void Bc7Endpoints(uint32_t bx, uint32_t by, uint32_t salt, uint8_t e0[4], uint8_t e1[4],
                         uint32_t *p0, uint32_t *p1) {
    const uint32_t h = Tex8kMix(bx, by, salt);
    for (uint32_t c = 0; c < 4u; ++c) {
        e0[c] = (uint8_t)((h >> (7u * c)) & 0x7Fu);
        e1[c] = (uint8_t)(0x7Fu - e0[c]);
    }
    *p0 = (h >> 28) & 1u;
    *p1 = (h >> 29) & 1u;
}

void Tex8kAstcSource(const Tex8kFormat *format, uint32_t bx, uint32_t by, uint32_t salt,
                     uint32_t *px, uint32_t *py) {
    const uint32_t across = ASTC_IMAGE_SIZE / format->block_width;
    const uint32_t down = ASTC_IMAGE_SIZE / format->block_height;
    const uint32_t tx = bx / across;
    const uint32_t ty = by / down;
    const uint32_t sx = (tx * 7u + ty * 3u + salt * 5u + 1u) % across;
    const uint32_t sy = (tx * 5u + ty * 11u + salt * 13u + 2u) % down;
    *px = (bx % across + sx) % across;
    *py = (by % down + sy) % down;
}

void Tex8kBlock(const Tex8kFormat *format, uint32_t bx, uint32_t by, uint32_t salt,
                uint8_t *out) {
    switch (format->kind) {
    case TEX8K_RGBA8: {
        const uint32_t word = bx | by << 13 | salt << 26;
        out[0] = (uint8_t)word;
        out[1] = (uint8_t)(word >> 8);
        out[2] = (uint8_t)(word >> 16);
        out[3] = (uint8_t)(word >> 24);
        return;
    }
    case TEX8K_BC1: {
        // Colour 0 from the mix and every index nought, so every texel is colour 0 whichever
        // way round the two colours are; colour 1 is its complement.
        const uint32_t c0 = Tex8kMix(bx, by, salt) & 0xFFFFu;
        const uint32_t c1 = c0 ^ 0xFFFFu;
        memset(out, 0, 8);
        out[0] = (uint8_t)c0;
        out[1] = (uint8_t)(c0 >> 8);
        out[2] = (uint8_t)c1;
        out[3] = (uint8_t)(c1 >> 8);
        return;
    }
    case TEX8K_BC7: {
        uint8_t e0[4];
        uint8_t e1[4];
        uint32_t p0 = 0;
        uint32_t p1 = 0;
        Bc7Endpoints(bx, by, salt, e0, e1, &p0, &p1);
        uint64_t bits[2] = { 0, 0 };
        uint32_t at = 0;
        // Mode 6 is six noughts and a one.
        PutBits(bits, &at, 1u << 6, 7);
        for (uint32_t c = 0; c < 4u; ++c) {
            PutBits(bits, &at, e0[c], 7);
            PutBits(bits, &at, e1[c], 7);
        }
        PutBits(bits, &at, p0, 1);
        PutBits(bits, &at, p1, 1);
        // Texel t takes index t; the first, the anchor, has one bit fewer, and nought fits it.
        for (uint32_t t = 0; t < 16u; ++t) {
            PutBits(bits, &at, t, t == 0u ? 3u : 4u);
        }
        for (uint32_t i = 0; i < 16u; ++i) {
            out[i] = (uint8_t)(bits[i / 8u] >> (8u * (i % 8u)));
        }
        return;
    }
    case TEX8K_ASTC: {
        uint32_t px = 0;
        uint32_t py = 0;
        Tex8kAstcSource(format, bx, by, salt, &px, &py);
        const uint32_t across = ASTC_IMAGE_SIZE / format->block_width;
        memcpy(out, format->astc->blocks + 16u * (py * across + px), 16);
        return;
    }
    }
}

void Tex8kFillBlocks(const Tex8kFormat *format, uint8_t *out, size_t pitch, uint32_t bx0,
                     uint32_t by0, uint32_t across, uint32_t rows, uint32_t salt) {
    for (uint32_t r = 0; r < rows; ++r) {
        uint8_t *const row = out + (size_t)r * pitch;
        const uint32_t by = by0 + r;
        if (format->kind == TEX8K_RGBA8) {
            // The 256 MiB upload's row, a word at a time: what Tex8kBlock gives, written fast.
            const uint32_t base = by << 13 | salt << 26;
            uint32_t *const words = (uint32_t *)(void *)row;
            for (uint32_t i = 0; i < across; ++i) {
                words[i] = base | (bx0 + i);
            }
            continue;
        }
        for (uint32_t i = 0; i < across; ++i) {
            Tex8kBlock(format, bx0 + i, by, salt, row + (size_t)i * format->block_bytes);
        }
    }
}

/// A five or six bit code made eight bits both ways a decoder may: its bits repeated, and the
/// code over its largest, rounded.
static void Expand(uint32_t code, uint32_t bits, uint8_t *repeated, uint8_t *rounded) {
    const uint32_t largest = (1u << bits) - 1u;
    *repeated = (uint8_t)(code << (8u - bits) | code >> (2u * bits - 8u));
    *rounded = (uint8_t)((code * 255u * 2u + largest) / (2u * largest));
}

void Tex8kTexel(const Tex8kFormat *format, uint32_t x, uint32_t y, uint32_t salt,
                uint8_t answers[3][4]) {
    const uint32_t bx = x / format->block_width;
    const uint32_t by = y / format->block_height;
    switch (format->kind) {
    case TEX8K_RGBA8:
        Tex8kBlock(format, x, y, salt, answers[0]);
        break;
    case TEX8K_BC1: {
        const uint32_t c0 = Tex8kMix(bx, by, salt) & 0xFFFFu;
        Expand(c0 >> 11, 5, &answers[0][0], &answers[1][0]);
        Expand((c0 >> 5) & 0x3Fu, 6, &answers[0][1], &answers[1][1]);
        Expand(c0 & 0x1Fu, 5, &answers[0][2], &answers[1][2]);
        answers[0][3] = answers[1][3] = 255u;
        memcpy(answers[2], answers[0], 4);
        return;
    }
    case TEX8K_BC7: {
        uint8_t e0[4];
        uint8_t e1[4];
        uint32_t p0 = 0;
        uint32_t p1 = 0;
        Bc7Endpoints(bx, by, salt, e0, e1, &p0, &p1);
        const uint32_t w = BC7_WEIGHTS4[(y % 4u) * 4u + x % 4u];
        for (uint32_t c = 0; c < 4u; ++c) {
            const uint32_t a = (uint32_t)e0[c] << 1 | p0;
            const uint32_t b = (uint32_t)e1[c] << 1 | p1;
            answers[0][c] = (uint8_t)(((64u - w) * a + w * b + 32u) >> 6);
        }
        break;
    }
    case TEX8K_ASTC: {
        uint8_t block[16];
        Tex8kBlock(format, bx, by, salt, block);
        uint16_t wide[ASTC_MAX_TEXELS][4];
        AstcDecodeBlock(block, format->block_width, format->block_height, wide);
        const uint32_t t = (y % format->block_height) * format->block_width + x % format->block_width;
        for (uint32_t c = 0; c < 4u; ++c) {
            answers[0][c] = AstcUnorm8Fp16(wide[t][c]);
            AstcUnorm8Answers(wide[t][c], &answers[1][c], &answers[2][c]);
        }
        return;
    }
    }
    memcpy(answers[1], answers[0], 4);
    memcpy(answers[2], answers[0], 4);
}

bool Tex8kMatches(const uint8_t rgba[4], const uint8_t answers[3][4]) {
    for (uint32_t c = 0; c < 4u; ++c) {
        if (rgba[c] != answers[0][c] && rgba[c] != answers[1][c] && rgba[c] != answers[2][c]) {
            return false;
        }
    }
    return true;
}

void Tex8kLevelOrigin(uint32_t size, uint32_t level, uint32_t *x, uint32_t *y) {
    if (level == 0u) {
        *x = *y = 0u;
        return;
    }
    // The bottom right corner of level 0, eight texels in per level so no two levels start
    // together.
    *x = *y = size - (size >> level) - 8u * level;
}

void Tex8kRegion(uint32_t frame, uint32_t *x, uint32_t *y) {
    *x = TEX8K_REGION * ((5u * frame + 2u) % 32u);
    *y = TEX8K_REGION * ((11u * frame + 7u) % 32u);
}

uint32_t Tex8kSaltAt(uint32_t x, uint32_t y, uint32_t frames) {
    for (uint32_t f = 0; f < frames; ++f) {
        uint32_t rx = 0;
        uint32_t ry = 0;
        Tex8kRegion(f, &rx, &ry);
        if (x >= rx && x < rx + TEX8K_REGION && y >= ry && y < ry + TEX8K_REGION) {
            return f + 1u;
        }
    }
    return 0u;
}

void Tex8kSpreadTexel(uint32_t size, uint32_t x, uint32_t y, uint32_t *tx, uint32_t *ty) {
    const uint32_t step = size / TEX8K_TARGET;
    *tx = x * step + TEX8K_SPREAD_X;
    *ty = y * step + TEX8K_SPREAD_Y;
}

uint32_t Tex8kCellTexel(uint32_t x, uint32_t y, uint32_t *tx, uint32_t *ty) {
    const uint32_t frame = (y / TEX8K_CELL) * (TEX8K_TARGET / TEX8K_CELL) + x / TEX8K_CELL;
    uint32_t rx = 0;
    uint32_t ry = 0;
    Tex8kRegion(frame, &rx, &ry);
    const uint32_t step = TEX8K_REGION / TEX8K_CELL;
    *tx = rx + (x % TEX8K_CELL) * step + TEX8K_CELL_X;
    *ty = ry + (y % TEX8K_CELL) * step + TEX8K_CELL_Y;
    return frame;
}

void Tex8kCopyOutOrigin(uint32_t *x, uint32_t *y) {
    Tex8kRegion(3u, x, y);
    *x -= TEX8K_REGION / 2u;
    *y -= TEX8K_REGION / 2u;
}
