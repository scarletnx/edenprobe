// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>

#include "vertexref.h"

#define ONE 0x3f800000u

uint32_t VertexLayoutBytes(VertexLayout layout) {
    switch (layout) {
    case VERTEX_RGBA32F:
        return 16u;
    case VERTEX_RGB32F:
        return 12u;
    case VERTEX_RG32F:
    case VERTEX_RGBA16_SSCALED:
        return 8u;
    default:
        return 4u;
    }
}

static uint32_t Word(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

/// A whole number as the bits of the float that holds it exactly: |@p v| under 2^24.
static uint32_t WholeFloat(int32_t v) {
    if (v == 0) {
        return 0u;
    }
    const uint32_t sign = v < 0 ? 0x80000000u : 0u;
    uint32_t m = (uint32_t)(v < 0 ? -v : v);
    int32_t exponent = 0;
    while ((m >> exponent) > 1u) {
        ++exponent;
    }
    return sign | (uint32_t)(127 + exponent) << 23 | ((m << (23 - exponent)) & 0x7fffffu);
}

/// Nought or one from an unsigned normalised field of @p full; false for anything between.
static bool Normalised(uint32_t field, uint32_t full, uint32_t *out) {
    if (field != 0u && field != full) {
        return false;
    }
    *out = field == 0u ? 0u : ONE;
    return true;
}

bool VertexExpected(VertexLayout layout, bool bgra, const uint8_t *bytes, uint32_t out[4]) {
    out[0] = 0u, out[1] = 0u, out[2] = 0u, out[3] = ONE;
    bool exact = true;
    switch (layout) {
    case VERTEX_RGBA32F:
        out[3] = Word(bytes + 12);
        // fall through
    case VERTEX_RGB32F:
        out[2] = Word(bytes + 8);
        // fall through
    case VERTEX_RG32F:
        out[1] = Word(bytes + 4);
        // fall through
    case VERTEX_R32F:
        out[0] = Word(bytes);
        break;
    case VERTEX_RGBA8_UNORM:
        for (uint32_t i = 0; i < 4u; ++i) {
            exact = Normalised(bytes[i], 0xffu, &out[i]) && exact;
        }
        break;
    case VERTEX_RGBA16_SSCALED:
        for (uint32_t i = 0; i < 4u; ++i) {
            out[i] = WholeFloat((int16_t)(bytes[i * 2u] | bytes[i * 2u + 1u] << 8));
        }
        break;
    case VERTEX_RGBA8_USCALED:
        for (uint32_t i = 0; i < 4u; ++i) {
            out[i] = WholeFloat(bytes[i]);
        }
        break;
    case VERTEX_RGB10A2_UNORM: {
        const uint32_t w = Word(bytes);
        exact = Normalised(w & 0x3ffu, 0x3ffu, &out[0]) &&
                Normalised(w >> 10 & 0x3ffu, 0x3ffu, &out[1]) &&
                Normalised(w >> 20 & 0x3ffu, 0x3ffu, &out[2]) && Normalised(w >> 30, 3u, &out[3]);
        break;
    }
    }
    if (bgra) {
        const uint32_t red = out[0];
        out[0] = out[2];
        out[2] = red;
    }
    return exact;
}

uint32_t VertexAddress(uint32_t vertex, uint32_t stride, uint32_t offset, uint32_t divisor) {
    const uint32_t element = divisor != 0u ? vertex / divisor : vertex;
    return element * stride + offset;
}

uint32_t VertexPixel(uint32_t vertex, uint32_t instance) {
    return vertex + instance * 8u;
}
