// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The unsigned 11-bit EAC block (R11_EAC, and each half of RG11_EAC), from the Khronos Data
/// Format Specification 1.3's ETC2 chapter (21.5, "Format R11 EAC"): a 64-bit big-endian word of
/// an 8-bit base codeword, a 4-bit multiplier, a 4-bit modifier table index and sixteen 3-bit
/// indices, the texels in column order (x = k / 4, y = k % 4).
///
/// Writes the sixteen 11-bit values, row major. A multiplier of nought counts as one eighth.
void EacDecodeUnsigned11(const uint8_t block[8], uint16_t values[16]);

/// The two eight-bit bytes an implementation may hand back for the 11-bit value @p v: v / 2047
/// rounded, and the value extended to 16 bits by bit replication, / 65535, rounded.
void EacUnorm8Answers(uint16_t v, uint8_t *direct, uint8_t *extended);

/// Builds a block from its fields, @p indices in row major order.
void EacBuildBlock(uint8_t block[8], uint32_t base, uint32_t multiplier, uint32_t table,
                   const uint8_t indices[16]);

#ifdef __cplusplus
}
#endif
