// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "copyref.h"

uint32_t CopyTexel(uint32_t x, uint32_t y, uint32_t layer) {
    // Red is x, green y, blue the layer with a high bit set, so no texel is nought in any
    // channel but red and green at the origin, and alpha a fixed value that no channel takes.
    return (x & 0xFFu) | (y & 0xFFu) << 8 | ((0x40u | layer) & 0xFFu) << 16 | 0xA5u << 24;
}

uint32_t CopyFullTexel(uint32_t x, uint32_t y) {
    const uint32_t r = (x & 1u) != 0u ? 0xFFu : 0u;
    const uint32_t g = (x & 2u) != 0u ? 0xFFu : 0u;
    const uint32_t b = (y & 1u) != 0u ? 0xFFu : 0u;
    const uint32_t a = (y & 2u) != 0u ? 0xFFu : 0u;
    return r | g << 8 | b << 16 | a << 24;
}

uint32_t CopyFormatBytes(CopyFormat format) {
    switch (format) {
    case COPY_RGB565:
        return 2u;
    case COPY_RGBA16F:
        return 8u;
    default:
        return 4u;
    }
}

static bool Full(uint32_t texel, uint32_t shift) {
    return ((texel >> shift) & 0xFFu) != 0u;
}

void CopyConvert(uint32_t texel, CopyFormat format, uint8_t *out) {
    const bool r = Full(texel, 0), g = Full(texel, 8), b = Full(texel, 16), a = Full(texel, 24);
    switch (format) {
    case COPY_RGBA8:
        out[0] = r ? 0xFFu : 0u, out[1] = g ? 0xFFu : 0u, out[2] = b ? 0xFFu : 0u;
        out[3] = a ? 0xFFu : 0u;
        break;
    case COPY_BGRA8:
        // Blue in the first byte, red in the third: the same channels in another order.
        out[0] = b ? 0xFFu : 0u, out[1] = g ? 0xFFu : 0u, out[2] = r ? 0xFFu : 0u;
        out[3] = a ? 0xFFu : 0u;
        break;
    case COPY_RGB565: {
        // Red in the top five bits of a 16-bit word, green the six below, blue the low five; no
        // alpha. Full is every bit of the field, whatever a converter rounds to.
        const uint32_t word = (r ? 0x1Fu << 11 : 0u) | (g ? 0x3Fu << 5 : 0u) | (b ? 0x1Fu : 0u);
        out[0] = (uint8_t)word;
        out[1] = (uint8_t)(word >> 8);
        break;
    }
    case COPY_RGBA16F: {
        // One is 0x3c00 as a half, nought is nought.
        const bool channels[4] = { r, g, b, a };
        for (uint32_t i = 0; i < 4u; ++i) {
            out[i * 2u] = 0u;
            out[i * 2u + 1u] = channels[i] ? 0x3Cu : 0u;
        }
        break;
    }
    }
}

void CopyFlipped(uint32_t x, uint32_t y, uint32_t side, bool flip_x, bool flip_y, uint32_t *from_x,
                 uint32_t *from_y) {
    *from_x = flip_x ? side - 1u - x : x;
    *from_y = flip_y ? side - 1u - y : y;
}

uint32_t CopyFlippedLayer(uint32_t layer, uint32_t depth) {
    return depth - 1u - layer;
}

uint32_t CopyScaledColumn(uint32_t x, uint32_t scale) {
    // The centre of destination pixel x is (x + 0.5) / scale in the source, whose texel is the
    // whole part: x / scale, since the half never carries a whole scale over.
    return x / scale;
}

uint32_t CopyBufferOffset(uint32_t x, uint32_t y, uint32_t row_length, uint32_t bytes_per_texel) {
    return y * row_length + x * bytes_per_texel;
}
