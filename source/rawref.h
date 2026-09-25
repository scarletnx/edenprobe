// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Raw 3D engine methods, for the registers the graphics library never writes and the methods it
/// never sends, and the answers the cases built on them expect.
///
/// The header layout is deko3d's (source/maxwell/command.h, MakeCmdHeader, deko3d 350f2b0): the
/// method in bits 0-12, the subchannel in 13-15, the word count in 16-28 and the submission mode
/// in 29-31. The method numbers are the 3D class's register indices, in words (deko3d
/// source/maxwell/engine_3d.def). The library binds the 3D engine to subchannel 0.

#define RAW_SUBCHANNEL_3D 0u

#define RAW_3D_WAIT_FOR_IDLE 0x044u
#define RAW_3D_VERTEX_ARRAY_INSTANCE_FIRST 0x485u
#define RAW_3D_VERTEX_ARRAY_INSTANCE_SUBSEQUENT 0x486u
#define RAW_3D_INDEX_ARRAY_START 0x5F2u
#define RAW_3D_INDEX_ARRAY_LIMIT 0x5F4u

/// The limit the library programs once at start-up and never again: the last byte of a 40-bit
/// address space, so a draw is never cut short by it (deko3d source/maxwell/gpu_3d_base.cpp).
#define RAW_INDEX_LIMIT_UNBOUNDED 0xFFFFFFFFFFull

/// The header of an increasing write of @p count words starting at @p method.
uint32_t RawIncreasingHeader(uint32_t subchannel, uint32_t method, uint32_t count);

/// The single word of an inline write of @p value (at most 13 bits) to @p method.
uint32_t RawInlineHeader(uint32_t subchannel, uint32_t method, uint32_t value);

/// Writes the three words that set the index buffer's limit register to @p limit, high word
/// first as every address pair in the 3D class is. Returns 3.
size_t RawIndexLimit(uint32_t out[3], uint64_t limit);

/// The argument of VERTEX_ARRAY_INSTANCE_FIRST and _SUBSEQUENT: the first vertex in bits 0-15,
/// the vertex count in bits 16-27 and the topology in bits 28-31, the layout of the 3D class's
/// DRAW_VERTEX_ARRAY_BEGIN_END_INSTANCE methods.
uint32_t RawInstanceArrayWord(uint32_t topology, uint32_t first, uint32_t count);

/// The last byte of an index array of @p count indices of @p bytes each, starting @p first
/// indices into a buffer at @p start: the limit a driver that bounds its draws exactly would
/// program, since the register holds the last valid byte, not one past it.
uint64_t RawTightIndexLimit(uint64_t start, uint32_t first, uint32_t count, uint32_t bytes);

#ifdef __cplusplus
}
#endif
