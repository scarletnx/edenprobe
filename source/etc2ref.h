// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// ETC2 colour blocks, from the Khronos Data Format Specification 1.3's ETC2 chapter: RGB8
/// (individual, differential, T, H and planar modes), RGB8 with punch-through alpha, and RGBA8,
/// whose first eight bytes are an 8-bit EAC alpha block. A block is a 64-bit big-endian word;
/// the pixel indices run down each column (texel k is x = k / 4, y = k % 4).

/// Decodes an RGB8 block, or with @p punchthrough an RGB8A1 block, into sixteen RGBA texels in
/// row major order. RGB8 texels are opaque.
void Etc2DecodeRgb(const uint8_t block[8], bool punchthrough, uint8_t texels[16][4]);

/// Decodes an RGBA8 block: the alpha block in bytes 0 to 7, the RGB8 block in bytes 8 to 15.
void Etc2DecodeRgba(const uint8_t block[16], uint8_t texels[16][4]);

/// Whether @p texels are @p block's own bytes rather than its colours: texels 0 and 1 hold bytes
/// 0 to 7, each texel's alpha either its byte or 255, which an RGB format's swizzle can put
/// there. What a host shows when the blocks were uploaded as they are and never decoded.
bool Etc2LooksRaw(const uint8_t block[16], const uint8_t texels[16][4]);

/// The builders take the fields of one mode and @p indices in row major order (two bits a texel
/// for colour, three for alpha). @p bit33 is the differential bit of RGB8 or the opaque bit of
/// RGB8A1. Bases are four bits a channel in individual, T and H mode and five in differential
/// mode; planar takes the origin, horizontal and vertical colours at 6, 7 and 6 bits. The bits a
/// mode leaves free are set so the mode is the one the decoder picks.
void Etc2BuildIndividual(uint8_t block[8], const uint8_t base1[3], const uint8_t base2[3],
                         uint32_t table1, uint32_t table2, bool flip, const uint8_t indices[16]);
void Etc2BuildDifferential(uint8_t block[8], const uint8_t base[3], const int8_t delta[3],
                           uint32_t table1, uint32_t table2, bool flip, bool bit33,
                           const uint8_t indices[16]);
void Etc2BuildT(uint8_t block[8], const uint8_t base1[3], const uint8_t base2[3],
                uint32_t distance, bool bit33, const uint8_t indices[16]);
/// H mode: @p distance_high holds the two stored bits of the distance index; its lowest bit is
/// whether base1 >= base2, so the order of the bases chooses it.
void Etc2BuildH(uint8_t block[8], const uint8_t base1[3], const uint8_t base2[3],
                uint32_t distance_high, bool bit33, const uint8_t indices[16]);
void Etc2BuildPlanar(uint8_t block[8], const uint8_t origin[3], const uint8_t horizontal[3],
                     const uint8_t vertical[3], bool bit33);
/// The 8-bit EAC alpha block of RGBA8 (the same fields as eacref.h's EacBuildBlock).
void Etc2BuildAlpha(uint8_t block[8], uint32_t base, uint32_t multiplier, uint32_t table,
                    const uint8_t indices[16]);

#ifdef __cplusplus
}
#endif
