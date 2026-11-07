// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "coverageref.h"

#include <math.h>

// deko3d source/maxwell/gpu_3d_ms.cpp, locationsMS1, MS2, MS4 and MS8.
const uint32_t COVERAGE_STANDARD_2X[4] = { 0x44CC44CCu, 0x44CC44CCu, 0x44CC44CCu, 0x44CC44CCu };
const uint32_t COVERAGE_STANDARD_4X[4] = { 0xEAA26E26u, 0xEAA26E26u, 0xEAA26E26u, 0xEAA26E26u };
const uint32_t COVERAGE_STANDARD_8X[4] = { 0x359DB759u, 0x1FFB71D3u, 0x359DB759u, 0x1FFB71D3u };
const uint32_t COVERAGE_CENTRE[4] = { 0x88888888u, 0x88888888u, 0x88888888u, 0x88888888u };

const uint8_t COVERAGE_COLOUR_A[4] = { 0xA0u, 0x60u, 0x20u, 0x80u };
const uint8_t COVERAGE_COLOUR_B[4] = { 0x40u, 0x90u, 0x18u, 0xA4u };

void CoverageSuitePatterns(uint32_t pairs[4], uint32_t grid[4]) {
    // Samples 0 and 1 at x 2, 2 and 3 at x 14, two heights apart so no two samples coincide.
    static const uint8_t pair_x[4] = { 2, 2, 14, 14 };
    static const uint8_t pair_y[4] = { 4, 12, 4, 12 };
    uint8_t xs[16];
    uint8_t ys[16];
    for (uint32_t i = 0; i < 16u; ++i) {
        xs[i] = pair_x[i % 4u];
        ys[i] = pair_y[i % 4u];
    }
    CoverageEncode(xs, ys, pairs);
    // Entry (py * 2 + px) * 4 + sample: px odd mirrors x, in both rows.
    for (uint32_t i = 0; i < 16u; ++i) {
        const uint32_t px = (i / 4u) % 2u;
        xs[i] = px != 0u ? (uint8_t)(16u - pair_x[i % 4u]) : pair_x[i % 4u];
        ys[i] = pair_y[i % 4u];
    }
    CoverageEncode(xs, ys, grid);
}

void CoverageEncode(const uint8_t xs[16], const uint8_t ys[16], uint32_t words[4]) {
    for (uint32_t w = 0; w < 4u; ++w) {
        words[w] = 0;
        for (uint32_t b = 0; b < 4u; ++b) {
            const uint32_t i = w * 4u + b;
            const uint32_t byte = (uint32_t)(xs[i] & 15u) | (uint32_t)(ys[i] & 15u) << 4;
            words[w] |= byte << (8u * b);
        }
    }
}

uint8_t CoverageLocation(const uint32_t words[4], uint32_t samples, uint32_t px, uint32_t py,
                         uint32_t sample) {
    // Only four samples have a grid this file claims; any other count reads the first pixel's
    // entries, which is every pixel's when the pattern repeats as the cases program it.
    uint32_t slot = 0;
    if (samples == 4u) {
        slot = (py % 2u) * 2u + (px % 2u);
    }
    const uint32_t index = (slot * samples + sample) % 16u;
    return (uint8_t)(words[index / 4u] >> (8u * (index % 4u)));
}

uint32_t CoverageLeftOf(const uint32_t words[4], uint32_t samples, uint32_t px, uint32_t py,
                        uint32_t edge) {
    uint32_t mask = 0;
    for (uint32_t s = 0; s < samples; ++s) {
        const uint32_t x = px * 16u + (CoverageLocation(words, samples, px, py, s) & 15u);
        if (x < edge) {
            mask |= 1u << s;
        }
    }
    return mask;
}

uint32_t CoverageEdgeMargin(const uint32_t words[4], uint32_t samples, uint32_t px, uint32_t py,
                            uint32_t edge) {
    uint32_t margin = UINT32_MAX;
    for (uint32_t s = 0; s < samples; ++s) {
        const uint32_t x = px * 16u + (CoverageLocation(words, samples, px, py, s) & 15u);
        const uint32_t distance = x > edge ? x - edge : edge - x;
        if (distance < margin) {
            margin = distance;
        }
    }
    return margin;
}

uint32_t CoverageCount(uint32_t mask) {
    uint32_t count = 0;
    for (; mask != 0u; mask &= mask - 1u) {
        ++count;
    }
    return count;
}

bool CoverageScale(uint8_t code, uint32_t numerator, uint32_t denominator, uint8_t *out) {
    if (denominator == 0u || numerator > denominator) {
        return false;
    }
    const uint32_t product = (uint32_t)code * numerator;
    if (product % denominator != 0u) {
        return false;
    }
    *out = (uint8_t)(product / denominator);
    return true;
}

bool CoverageScaleSafe(uint8_t code, uint32_t numerator, uint32_t denominator) {
    uint8_t exact = 0;
    if (!CoverageScale(code, numerator, denominator, &exact)) {
        return false;
    }
    // The factor as the nearest 255th, both ways a halfway factor could have been rounded, and
    // the product with the channel in codes. Each must round to the exact byte with room.
    const double factor = (double)numerator / (double)denominator;
    const double low = floor(factor * 255.0) / 255.0;
    const double high = ceil(factor * 255.0) / 255.0;
    const double candidates[2] = { (double)code * low, (double)code * high };
    for (int i = 0; i < 2; ++i) {
        const double distance = fabs(candidates[i] - (double)exact);
        if (distance > 0.49) {
            return false;
        }
    }
    return true;
}

uint32_t CoverageTableEntry(uint32_t covered, uint32_t samples) {
    if (samples == 0u) {
        return 0;
    }
    uint32_t index = covered * 16u / samples;
    if (index < 1u) {
        index = 1u;
    }
    return index - 1u;
}
