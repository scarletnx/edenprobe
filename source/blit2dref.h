// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The 2D engine's scaled copy, block linear to block linear, and what each case expects.
///
/// A 128 by 32 source whose every texel names itself, 0x10000000 | y << 12 | x, copied into a
/// 21 by 9 window at (5, 3) of a 64 by 16 destination filled with 0xEE, both A8B8G8R8 in blocks
/// two gobs tall. The window's rows start and end inside 16 byte runs of a gob and its rows
/// cross a gob border, so a copy that writes whole runs or whole gobs shows outside it.
/// tools/blit2d-expect.py works out the same images apart from this file.

#define BLIT2D_SRC_WIDTH 128u
#define BLIT2D_SRC_HEIGHT 32u
#define BLIT2D_SRC_BYTES (BLIT2D_SRC_WIDTH * BLIT2D_SRC_HEIGHT * 4u)
#define BLIT2D_DST_WIDTH 64u
#define BLIT2D_DST_HEIGHT 16u
#define BLIT2D_DST_BYTES (BLIT2D_DST_WIDTH * BLIT2D_DST_HEIGHT * 4u)
#define BLIT2D_BLOCK_GOBS 2u
#define BLIT2D_WIN_X 5u
#define BLIT2D_WIN_Y 3u
#define BLIT2D_WIN_WIDTH 21u
#define BLIT2D_WIN_HEIGHT 9u
#define BLIT2D_WIN_PIXELS (BLIT2D_WIN_WIDTH * BLIT2D_WIN_HEIGHT)
#define BLIT2D_OUTSIDE_PIXELS (BLIT2D_DST_WIDTH * BLIT2D_DST_HEIGHT - BLIT2D_WIN_PIXELS)
#define BLIT2D_FILL 0xEEu
#define BLIT2D_FILL_WORD 0xEEEEEEEEu

/// The 2D class's methods a case writes, in words (deko3d source/maxwell/engine_2d.def, deko3d
/// 350f2b0; NVIDIA's cl902d.h has them at four times these, in bytes). The library binds the
/// class to subchannel 3 when it makes a queue (source/maxwell/helpers.h, gpu_base.cpp).
#define BLIT2D_SUBCHANNEL 3u
#define BLIT2D_SET_DST_FORMAT 0x080u ///< then the layout, block size, depth and layer
#define BLIT2D_SET_DST_WIDTH 0x086u  ///< then the height and the address, high word first
#define BLIT2D_SET_SRC_FORMAT 0x08Cu ///< then the layout, block size and depth
#define BLIT2D_SET_SRC_WIDTH 0x092u  ///< then the height and the address, high word first
#define BLIT2D_SET_CLIP_ENABLE 0x0A4u
#define BLIT2D_SET_OPERATION 0x0ABu
#define BLIT2D_SET_COMPRESSION 0x0B5u
#define BLIT2D_SET_SAMPLE_MODE 0x223u
/// The first of twelve: destination origin and size, du/dx and dv/dy, and the source origin,
/// each 32.32 pair low word first. The twelfth, the source origin's integer y, launches.
#define BLIT2D_SET_DST_X0 0x22Cu
#define BLIT2D_FORMAT_A8B8G8R8 0xD5u
#define BLIT2D_OPERATION_SRCCOPY 3u
#define BLIT2D_BLOCK_SIZE_TWO_GOBS (1u << 4)
/// Bit 0 of the sample mode: 0 the centre origin, 1 the corner. Bit 4, the filter, stays point.
#define BLIT2D_ORIGIN_CORNER 1u
/// The words Blit2dRawWords writes.
#define BLIT2D_WORDS 38u

typedef enum {
    BLIT2D_CORNER_HALF,
    BLIT2D_CENTER_HALF,
    BLIT2D_CORNER_2_3,
    BLIT2D_CENTER_2_3,
    BLIT2D_CASES,
} Blit2dCaseId;

typedef struct {
    const char *name; ///< as the cases were asked, "blit2d_corner_half"
    const char *title;
    uint32_t origin;  ///< BLIT2D_ORIGIN_CORNER or 0
    uint64_t du;      ///< texels per pixel on both axes, 32.32
} Blit2dCase;

/// The open questions as a bit set of answers, the bit set for B.
///
/// Q1: A the corner origin samples the window's pixel n at n * du and the centre origin at
/// (n + 1/2) * du; B the other way round. Q2: A du is stepped exactly, in 32.32, pixel by pixel;
/// B what Eden does (fermi_2d.cpp:79-96): the corner start moved back by the integer half of
/// du, both ends of the source span cut to whole texels, the span sampled at pixel centres.
#define BLIT2D_Q1_B 1u
#define BLIT2D_Q2_B 2u
#define BLIT2D_COMBINATIONS 4u

const Blit2dCase *Blit2dCaseAt(Blit2dCaseId id);

/// The source texel at @p x, @p y: its own coordinates, so any pixel says which texel it took.
uint32_t Blit2dSourceTexel(uint32_t x, uint32_t y);

/// Where the four bytes of pixel @p x, @p y sit in the destination, block linear.
uint32_t Blit2dDstOffset(uint32_t x, uint32_t y);

/// Writes the whole source, BLIT2D_SRC_BYTES, block linear, as the processor writes it.
void Blit2dFillSource(uint8_t *bytes);

/// The source texel the window's pixel @p n takes on an axis @p extent pixels long, under the
/// answers @p answers. The same rule on both axes, since du/dx and dv/dy are the same.
int32_t Blit2dTexel(const Blit2dCase *c, uint32_t answers, uint32_t n, uint32_t extent);

/// The destination's BLIT2D_DST_BYTES after the copy under @p answers: the fill outside the
/// window, the texel Blit2dTexel names inside.
void Blit2dExpected(const Blit2dCase *c, uint32_t answers, uint8_t *out);

/// The MD5 of @p size bytes in lower case hex, 32 digits and a nul.
void Blit2dMd5Hex(const void *bytes, size_t size, char out[33]);

/// The case's whole setup and launch as raw words for subchannel 3, BLIT2D_WORDS of them, in the
/// order the library writes its own blits (source/maxwell/gpu_transfer.cpp, Blit2DEngine): the
/// operation and no clip, the source, the destination, compression on, the sample mode, then the
/// twelve words that launch. Returns BLIT2D_WORDS.
size_t Blit2dRawWords(const Blit2dCase *c, uint64_t src_iova, uint64_t dst_iova, uint32_t *out);

#ifdef __cplusplus
}
#endif
