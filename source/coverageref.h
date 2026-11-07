// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The multisample answers the coverage suite expects, worked out on the processor.
///
/// The 3D class holds sixteen sample locations in four registers, a byte each: x in the low
/// nibble and y in the high, in sixteenths of a pixel, sample 0 in the low byte of the first
/// register (deko3d source/maxwell/gpu_3d_ms.cpp, encodeSampleLocation). With four samples the
/// sixteen cover a 2 by 2 grid of pixels, entry (py * 2 + px) * 4 + sample, which is the order
/// NV_sample_locations gives its table; with any other count every case here programs the same
/// pattern for each pixel, so how that count's grid is laid out decides nothing.

/// The words the library programs for two, four and eight samples (deko3d's locationsMS2, MS4
/// and MS8), and for every sample at the centre (dkMultisampleStateDefaults).
extern const uint32_t COVERAGE_STANDARD_2X[4];
extern const uint32_t COVERAGE_STANDARD_4X[4];
extern const uint32_t COVERAGE_STANDARD_8X[4];
extern const uint32_t COVERAGE_CENTRE[4];

/// The coverage suite's own patterns for four samples. PAIRS puts samples 0 and 1 two sixteenths
/// in from the left and 2 and 3 two in from the right, which splits a pixel at its centre as 0x3
/// where the standard pattern gives 0x5. GRID uses PAIRS in the even columns of the 2 by 2 grid
/// and its mirror in the odd ones (0xc), the same in both rows, so which row the grid starts on
/// decides nothing. Filled by CoverageSuitePatterns.
void CoverageSuitePatterns(uint32_t pairs[4], uint32_t grid[4]);

/// The edges the coverage suite draws to, in sixteenths of a pixel from the left: the centre of
/// pixel 8 and of pixel 9, and a quarter into pixel 8.
#define COVERAGE_EDGE_CENTRE_8 (8u * 16u + 8u)
#define COVERAGE_EDGE_CENTRE_9 (9u * 16u + 8u)
#define COVERAGE_EDGE_QUARTER_8 (8u * 16u + 4u)

/// The colours the modulation cases draw, two targets' worth, chosen so a quarter, a half and
/// three quarters of every channel is a whole byte that CoverageScaleSafe accepts.
extern const uint8_t COVERAGE_COLOUR_A[4];
extern const uint8_t COVERAGE_COLOUR_B[4];

/// Packs sixteen locations, @p xs and @p ys in sixteenths of a pixel (0 to 15), into the four
/// register words, as the library packs them.
void CoverageEncode(const uint8_t xs[16], const uint8_t ys[16], uint32_t words[4]);

/// The location byte of sample @p sample of the pixel at (@p px, @p py) for @p samples samples.
uint8_t CoverageLocation(const uint32_t words[4], uint32_t samples, uint32_t px, uint32_t py,
                         uint32_t sample);

/// The samples of the pixel at (@p px, @p py) a rectangle from the target's left edge to
/// @p edge covers, @p edge counted in sixteenths of a pixel from that left edge: bit i for
/// sample i. A sample exactly on the edge counts as outside, but the cases never put one there
/// (CoverageEdgeMargin).
uint32_t CoverageLeftOf(const uint32_t words[4], uint32_t samples, uint32_t px, uint32_t py,
                        uint32_t edge);

/// The least distance, in sixteenths of a pixel, between @p edge and any sample of the pixel
/// at (@p px, @p py). A case whose margin is nought would be decided by the fill rule rather
/// than by where the samples are.
uint32_t CoverageEdgeMargin(const uint32_t words[4], uint32_t samples, uint32_t px, uint32_t py,
                            uint32_t edge);

/// How many bits of @p mask are set.
uint32_t CoverageCount(uint32_t mask);

/// A normalised channel of byte @p code multiplied by @p numerator / @p denominator, as coverage
/// modulation multiplies it: false when the product is not a whole byte, which a case may not
/// use, since rounding would then decide it.
bool CoverageScale(uint8_t code, uint32_t numerator, uint32_t denominator, uint8_t *out);

/// Whether the byte CoverageScale gives is also the byte a machine gets that holds the factor
/// as an eight bit fraction (the factor rounded to the nearest 255th) and rounds the product to
/// the nearest code: the product then stays at least 0.01 of a code from a halfway point. The
/// coverage suite only uses channels and factors for which this holds, so its exact tier does
/// not rest on the precision the hardware keeps the factor in.
bool CoverageScaleSafe(uint8_t code, uint32_t numerator, uint32_t denominator);

/// The entry of a sixteen entry modulation table the specification reads when @p covered of
/// @p samples raster samples are covered: R = covered / samples, I = max(1, int(R * 16)), entry
/// I - 1 (NV_framebuffer_mixed_samples, "Coverage Reduction").
uint32_t CoverageTableEntry(uint32_t covered, uint32_t samples);

#ifdef __cplusplus
}
#endif
