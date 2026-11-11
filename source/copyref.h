// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The copy suite's rules: the pictures it copies, where a flipped or offset copy puts each texel,
/// and the bytes a texel of nought and full channels becomes in each format a blit converts to.
///
/// Touches no graphics library, so the reference suite pins all of it with no device present.

/// The side of every image the suite copies, in texels, and of each layer of its arrays.
#define COPY_SIDE 16u
/// The layers of its arrays.
#define COPY_LAYERS 4u

/// The texel at (@p x, @p y) of layer @p layer of the suite's pictures, as the RGBA8 word whose
/// low byte is red: every texel of every layer a value of its own, so a copy that moved one says
/// which.
uint32_t CopyTexel(uint32_t x, uint32_t y, uint32_t layer);

/// The texel at (@p x, @p y) of the picture the conversion cases blit: each channel nought or
/// full, red by x's bit 0, green by x's bit 1, blue by y's bit 0 and alpha by y's bit 1, so the
/// sixteen texels of any 4 by 4 square hold every combination, and the converted value of each
/// is exact in every format whatever the converter rounds.
uint32_t CopyFullTexel(uint32_t x, uint32_t y);

/// The formats the conversion cases blit into.
typedef enum {
    COPY_RGBA8,
    COPY_BGRA8,
    COPY_RGB565,
    COPY_RGBA16F,
} CopyFormat;

/// The bytes a format holds a pixel in.
uint32_t CopyFormatBytes(CopyFormat format);

/// The bytes the RGBA8 word @p texel, every channel nought or full, becomes in @p format, little
/// endian, into @p out (CopyFormatBytes of them).
void CopyConvert(uint32_t texel, CopyFormat format, uint8_t *out);

/// Where the texel a flipped copy writes at (@p x, @p y) comes from, in an image @p side wide and
/// tall: mirrored across the middle column by @p flip_x and the middle row by @p flip_y.
void CopyFlipped(uint32_t x, uint32_t y, uint32_t side, bool flip_x, bool flip_y, uint32_t *from_x,
                 uint32_t *from_y);

/// The layer a copy of @p depth layers with FlipZ writes into layer @p layer from.
uint32_t CopyFlippedLayer(uint32_t layer, uint32_t depth);

/// The texel a blit of a @p side wide source onto a destination @p scale times as wide, nearest
/// filtered, takes for destination column @p x: the source column under the destination pixel's
/// centre.
uint32_t CopyScaledColumn(uint32_t x, uint32_t scale);

/// The byte offset of texel (@p x, @p y) in a buffer whose rows are @p row_length bytes apart, of
/// @p bytes_per_texel each.
uint32_t CopyBufferOffset(uint32_t x, uint32_t y, uint32_t row_length, uint32_t bytes_per_texel);

#ifdef __cplusplus
}
#endif
