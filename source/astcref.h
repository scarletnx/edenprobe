// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// An ASTC decoder for the LDR profile, written from the Khronos Data Format Specification's
/// ASTC chapter (1.3, section 23): void extent blocks, one to four partitions, every LDR
/// endpoint mode, trit and quint integer sequences, weight infill and dual planes. It exists to
/// say what a sampled ASTC texel must be, and is checked against ARM's reference decoder
/// (astcenc) by the reference suite and by tools/astc-check.
///
/// The answer is the specification's 16-bit value C per channel, before any conversion: what an
/// UNORM ASTC format hands a shader is C / 65535, or C's top eight bits when the implementation
/// decodes to eight bits (VK_EXT_astc_decode_mode's decode_unorm8). An HDR void extent, and any
/// block the specification calls illegal, is the error colour in this profile; a partition whose
/// endpoint mode is an HDR one gets the error colour on its own texels.

#define ASTC_MAX_TEXELS 144u

typedef enum {
    ASTC_BLOCK_NORMAL,
    /// An LDR void extent: every texel the block's constant colour.
    ASTC_BLOCK_CONSTANT,
    /// Illegal in the LDR profile: every texel the error colour, opaque magenta.
    ASTC_BLOCK_ERROR,
} AstcBlockKind;

/// Decodes the 16 byte @p block of footprint @p width by @p height (4 to 12 each) into
/// @p texels, row major, four 16-bit channels each in RGBA order.
AstcBlockKind AstcDecodeBlock(const uint8_t block[16], uint32_t width, uint32_t height,
                              uint16_t texels[][4]);

/// The two eight bit values an implementation may hand back for the 16-bit value @p c of an
/// UNORM ASTC texel: C / 65535 rounded to eight bits, and C's top eight bits.
void AstcUnorm8Answers(uint16_t c, uint8_t *rounded, uint8_t *truncated);

/// What the fields of a block say, for the reference suite and for a case's detail line.
typedef struct {
    AstcBlockKind kind;
    uint32_t grid_width;
    uint32_t grid_height;
    bool dual_plane;
    uint32_t partitions;
    /// The weight range: 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24 or 32 values.
    uint32_t weight_levels;
    /// The endpoint range, 6 to 256 values.
    uint32_t endpoint_levels;
    uint32_t endpoint_modes[4];
} AstcBlockInfo;

void AstcDescribeBlock(const uint8_t block[16], uint32_t width, uint32_t height,
                       AstcBlockInfo *info);

/// The weight a single weight value @p value of range @p levels unquantizes to, 0 to 64: the
/// specification's table, not a formula over the range.
uint32_t AstcUnquantizeWeight(uint32_t value, uint32_t levels);

#ifdef __cplusplus
}
#endif
