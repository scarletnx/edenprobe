// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../copyref.h"
#include "../gpu.h"
#include "../report.h"
#include "tests.h"

// The copies a game's driver makes between images and buffers, through the two engines deko3d
// sends them to: the copy engine, which moves bytes and can step through memory backwards, and
// the 2D engine, which converts formats, mirrors and scales. What a game's driver asks of them and
// an emulator remakes on its own: a copy to or from a buffer turned upside down (the copy engine
// stepping by a negative pitch), mirrored (the 2D engine stepping its source backwards), with rows
// further apart than they are wide, a rectangle between two images, layers in reverse order, bytes
// copied between two formats of one size, and a blit that converts, mirrors, scales or keeps an
// sRGB image's bytes.
//
// Every image is 16 by 16 and every texel a value of its own, put in and read out by the copy
// engine with no flags, which the first case checks alone; each case then judges every byte it
// reads back, padding included: exact. The conversion cases blit channels of nought and full,
// which every format holds exactly whatever a converter rounds. Which texel lands where is
// copyref.c's, pinned by the reference suite. Three things only a console settles are notes:
// what an sRGB to linear blit and a half of a quarter to eight bits give, and the 2D engine's
// blend.

static const char *const SUITE = "copy";

#define SIDE COPY_SIDE
#define TEXELS (SIDE * SIDE)
/// Bytes a row of four byte texels takes in a buffer when nothing pads it.
#define ROW (SIDE * 4u)
/// A buffer's rows when the case pads them: a third more, a multiple of the 32 the 2D engine
/// wants a pitch to be.
#define PADDED_ROW 96u
#define PAD_BYTE 0xEEu

typedef struct {
    GpuContext gpu;
    /// The pictures, a layer each, as RGBA8 words.
    uint32_t picture[COPY_LAYERS][TEXELS];
    /// What a case expects to read back, byte for byte, and how many of them.
    uint8_t expected[COPY_LAYERS * TEXELS * 8u + PADDED_ROW * SIDE];
    uint32_t expected_size;
} CopyContext;

/// One context at a time, so the pictures are not on the stack.
static CopyContext context;

static void PutWord(uint8_t *out, uint32_t word) {
    out[0] = (uint8_t)word;
    out[1] = (uint8_t)(word >> 8);
    out[2] = (uint8_t)(word >> 16);
    out[3] = (uint8_t)(word >> 24);
}

static bool Fail(CopyContext *x, const char *name, const char *why) {
    ReportCase(SUITE, name, CHECK_NONE, false, x->gpu.failure[0] != '\0' ? x->gpu.failure : why);
    return false;
}

/// A buffer of @p bytes in the processor-visible scratch, every byte @p fill.
static uint8_t *Buffer(CopyContext *x, uint32_t bytes, uint8_t fill, DkGpuAddr *address) {
    uint8_t *const cpu = (uint8_t *)GpuAllocData(&x->gpu, bytes, 256u, address);
    if (cpu != NULL) {
        memset(cpu, fill, bytes);
    }
    return cpu;
}

/// A block linear 16 by 16 image the 2D engine can use, @p scale times as wide and tall.
static bool Image(CopyContext *x, DkImage *out, DkImageFormat format, uint32_t scale) {
    DkImageLayout layout;
    return GpuCreateImage(&x->gpu, out, &layout, format, SIDE * scale, SIDE * scale,
                          DkImageFlags_Usage2DEngine, 0, NULL);
}

/// Stages layer @p layer of the pictures, or @p words when given, in a buffer with rows
/// @p row_length bytes apart, the gaps between rows PAD_BYTE.
static bool Stage(CopyContext *x, const uint32_t *words, uint32_t row_length, DkCopyBuf *out) {
    DkGpuAddr address = 0;
    uint8_t *const cpu = Buffer(x, row_length * SIDE, PAD_BYTE, &address);
    if (cpu == NULL) {
        return false;
    }
    for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t xx = 0; xx < SIDE; ++xx) {
            PutWord(cpu + CopyBufferOffset(xx, y, row_length, 4u), words[y * SIDE + xx]);
        }
    }
    out->addr = address;
    out->rowLength = row_length;
    out->imageHeight = 0;
    return true;
}

/// Records a plain upload of @p words, packed rows, into the whole of @p image.
static bool Upload(CopyContext *x, const DkImage *image, const uint32_t *words) {
    DkCopyBuf source;
    if (!Stage(x, words, ROW, &source)) {
        return false;
    }
    DkImageView view;
    dkImageViewDefaults(&view, image);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    dkCmdBufCopyBufferToImage(x->gpu.cmdbuf, &source, &view, &rect, 0);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    return true;
}

/// Records a plain copy of @p layers layers of @p image, @p side texels square of @p bytes each,
/// into a packed buffer, and hands back where the processor reads it once the list has run.
static uint8_t *Download(CopyContext *x, const DkImage *image, uint32_t side, uint32_t bytes,
                         uint32_t layers) {
    DkGpuAddr address = 0;
    uint8_t *const cpu = Buffer(x, side * side * bytes * layers, 0x00u, &address);
    if (cpu == NULL) {
        return NULL;
    }
    DkImageView view;
    dkImageViewDefaults(&view, image);
    const DkImageRect rect = { 0, 0, 0, side, side, layers };
    const DkCopyBuf destination = { address, side * bytes, side * side * bytes };
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    dkCmdBufCopyImageToBuffer(x->gpu.cmdbuf, &view, &rect, &destination, 0);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    return cpu;
}

/// Runs the list and holds the @p size bytes at @p got to the expected ones, unit by unit of
/// @p unit bytes (a texel, or a byte where padding is judged); the line names the first wrong
/// one, at (x, y) of its layer for a texel of a 16 wide picture.
static void Judge(CopyContext *x, const char *name, const uint8_t *got, uint32_t unit,
                  uint32_t side, const char *what) {
    GpuSubmit(&x->gpu);
    if (!x->gpu.ready || got == NULL) {
        Fail(x, name, "the list was not recorded");
        return;
    }
    uint32_t wrong = 0, first = 0;
    for (uint32_t i = 0; i < x->expected_size; i += unit) {
        if (memcmp(got + i, x->expected + i, unit) != 0) {
            first = wrong == 0 ? i : first;
            ++wrong;
        }
    }
    char where[64];
    const uint32_t index = first / unit;
    snprintf(where, sizeof(where), "(%u, %u) of layer %u", (unsigned)(index % side),
             (unsigned)(index / side % side), (unsigned)(index / (side * side)));
    char got_text[24] = "", want_text[24] = "";
    for (uint32_t b = 0; b < unit && b < 8u; ++b) {
        snprintf(got_text + b * 2u, 3, "%02x", got[first + unit - 1u - b]);
        snprintf(want_text + b * 2u, 3, "%02x", x->expected[first + unit - 1u - b]);
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%s: %u of %u wrong; the first, %s, is 0x%s, expected 0x%s", what, (unsigned)wrong,
               (unsigned)(x->expected_size / unit), unit == 1u ? "a byte" : where, got_text,
               want_text);
}

static bool Begin(CopyContext *x, const char *name) {
    if (!ReportStart(SUITE, name)) {
        return false;
    }
    GpuBeginList(&x->gpu);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    return true;
}

/// Expects layer @p layer of the pictures at every texel, each taken from where @p flip_x and
/// @p flip_y move it.
static void ExpectFlipped(CopyContext *x, uint32_t layer, bool flip_x, bool flip_y) {
    for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t xx = 0; xx < SIDE; ++xx) {
            uint32_t fx, fy;
            CopyFlipped(xx, y, SIDE, flip_x, flip_y, &fx, &fy);
            PutWord(x->expected + (y * SIDE + xx) * 4u, x->picture[layer][fy * SIDE + fx]);
        }
    }
    x->expected_size = TEXELS * 4u;
}

// ---- the copy engine, and the 2D engine for FlipX

static void RoundTripCase(CopyContext *x) {
    static const char *const NAME = "a picture copied to an image and back keeps every texel";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage image;
    if (!Image(x, &image, DkImageFormat_RGBA8_Unorm, 1) || !Upload(x, &image, x->picture[0])) {
        Fail(x, NAME, "no room for the image");
        return;
    }
    const uint8_t *const got = Download(x, &image, SIDE, 4u, 1);
    ExpectFlipped(x, 0, false, false);
    Judge(x, NAME, got, 4u, SIDE, "uploaded and read back with no flags");
}

/// A copy from a buffer into an image with @p flags, read back plainly.
static void FromBufferCase(CopyContext *x, const char *name, uint32_t flags, const char *what) {
    if (!Begin(x, name)) {
        return;
    }
    DkImage image;
    DkCopyBuf source;
    if (!Image(x, &image, DkImageFormat_RGBA8_Unorm, 1) || !Stage(x, x->picture[1], ROW, &source)) {
        Fail(x, name, "no room for the image");
        return;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    dkCmdBufCopyBufferToImage(x->gpu.cmdbuf, &source, &view, &rect, flags);
    const uint8_t *const got = Download(x, &image, SIDE, 4u, 1);
    ExpectFlipped(x, 1, (flags & DkBlitFlag_FlipX) != 0u, (flags & DkBlitFlag_FlipY) != 0u);
    Judge(x, name, got, 4u, SIDE, what);
}

/// An image uploaded plainly, copied to a buffer with @p flags.
static void ToBufferCase(CopyContext *x, const char *name, uint32_t flags, const char *what) {
    if (!Begin(x, name)) {
        return;
    }
    DkImage image;
    DkGpuAddr address = 0;
    if (!Image(x, &image, DkImageFormat_RGBA8_Unorm, 1) || !Upload(x, &image, x->picture[2])) {
        Fail(x, name, "no room for the image");
        return;
    }
    uint8_t *const got = Buffer(x, TEXELS * 4u, 0x00u, &address);
    if (got == NULL) {
        Fail(x, name, "no room for the buffer");
        return;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    const DkCopyBuf destination = { address, ROW, 0 };
    dkCmdBufCopyImageToBuffer(x->gpu.cmdbuf, &view, &rect, &destination, flags);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    ExpectFlipped(x, 2, (flags & DkBlitFlag_FlipX) != 0u, (flags & DkBlitFlag_FlipY) != 0u);
    Judge(x, name, got, 4u, SIDE, what);
}

/// An image copied to a buffer whose rows are PADDED_ROW bytes apart: every byte of the buffer is
/// judged, the texels where they go and the gaps as they were.
static void PaddedToBufferCase(CopyContext *x) {
    static const char *const NAME =
        "a copy to a buffer with rows 96 bytes apart leaves the 32 bytes between them as they were";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage image;
    DkGpuAddr address = 0;
    if (!Image(x, &image, DkImageFormat_RGBA8_Unorm, 1) || !Upload(x, &image, x->picture[3])) {
        Fail(x, NAME, "no room for the image");
        return;
    }
    uint8_t *const got = Buffer(x, PADDED_ROW * SIDE, PAD_BYTE, &address);
    if (got == NULL) {
        Fail(x, NAME, "no room for the buffer");
        return;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    const DkCopyBuf destination = { address, PADDED_ROW, 0 };
    dkCmdBufCopyImageToBuffer(x->gpu.cmdbuf, &view, &rect, &destination, 0);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    memset(x->expected, PAD_BYTE, PADDED_ROW * SIDE);
    for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t xx = 0; xx < SIDE; ++xx) {
            PutWord(x->expected + CopyBufferOffset(xx, y, PADDED_ROW, 4u),
                    x->picture[3][y * SIDE + xx]);
        }
    }
    x->expected_size = PADDED_ROW * SIDE;
    Judge(x, NAME, got, 1u, SIDE, "16 rows of 64 bytes copied 96 bytes apart over 0xee");
}

static void PaddedFromBufferCase(CopyContext *x) {
    static const char *const NAME =
        "a copy from a buffer with rows 96 bytes apart reads each row from its own place";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage image;
    DkCopyBuf source;
    if (!Image(x, &image, DkImageFormat_RGBA8_Unorm, 1) ||
        !Stage(x, x->picture[0], PADDED_ROW, &source)) {
        Fail(x, NAME, "no room for the image");
        return;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    dkCmdBufCopyBufferToImage(x->gpu.cmdbuf, &source, &view, &rect, 0);
    const uint8_t *const got = Download(x, &image, SIDE, 4u, 1);
    ExpectFlipped(x, 0, false, false);
    Judge(x, NAME, got, 4u, SIDE, "16 rows 96 bytes apart, 0xee between them, copied in");
}

static void RectangleCase(CopyContext *x) {
    static const char *const NAME =
        "an 8 by 8 copy from (3, 5) to (6, 2) between two images writes that rectangle alone";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage from, to;
    if (!Image(x, &from, DkImageFormat_RGBA8_Unorm, 1) ||
        !Image(x, &to, DkImageFormat_RGBA8_Unorm, 1) || !Upload(x, &from, x->picture[0]) ||
        !Upload(x, &to, x->picture[1])) {
        Fail(x, NAME, "no room for the images");
        return;
    }
    DkImageView source, destination;
    dkImageViewDefaults(&source, &from);
    dkImageViewDefaults(&destination, &to);
    const DkImageRect src = { 3, 5, 0, 8, 8, 1 };
    const DkImageRect dst = { 6, 2, 0, 8, 8, 1 };
    dkCmdBufCopyImage(x->gpu.cmdbuf, &source, &src, &destination, &dst, 0);
    const uint8_t *const got = Download(x, &to, SIDE, 4u, 1);
    ExpectFlipped(x, 1, false, false);
    for (uint32_t y = 0; y < 8u; ++y) {
        for (uint32_t xx = 0; xx < 8u; ++xx) {
            PutWord(x->expected + ((2u + y) * SIDE + 6u + xx) * 4u,
                    x->picture[0][(5u + y) * SIDE + 3u + xx]);
        }
    }
    Judge(x, NAME, got, 4u, SIDE, "an 8 by 8 window of one picture copied into another");
}

static void FormatBytesCase(CopyContext *x) {
    static const char *const NAME =
        "a copy from an R32 float image to an RGBA8 one keeps every byte, NaNs and denormals too";
    if (!Begin(x, NAME)) {
        return;
    }
    // Words a copy through a float would change: a signalling NaN with a payload, a negative
    // quiet NaN of all ones, the smallest denormal, and negative nought.
    uint32_t words[TEXELS];
    memcpy(words, x->picture[2], sizeof(words));
    words[0] = 0x7F800001u;
    words[1] = 0xFFFFFFFFu;
    words[2] = 0x00000001u;
    words[3] = 0x80000000u;
    DkImage from, to;
    if (!Image(x, &from, DkImageFormat_R32_Float, 1) ||
        !Image(x, &to, DkImageFormat_RGBA8_Unorm, 1) || !Upload(x, &from, words)) {
        Fail(x, NAME, "no room for the images");
        return;
    }
    DkImageView source, destination;
    dkImageViewDefaults(&source, &from);
    dkImageViewDefaults(&destination, &to);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    dkCmdBufCopyImage(x->gpu.cmdbuf, &source, &rect, &destination, &rect, 0);
    const uint8_t *const got = Download(x, &to, SIDE, 4u, 1);
    for (uint32_t i = 0; i < TEXELS; ++i) {
        PutWord(x->expected + i * 4u, words[i]);
    }
    x->expected_size = TEXELS * 4u;
    Judge(x, NAME, got, 4u, SIDE, "an R32 float image copied whole into an RGBA8 one");
}

static void LayersCase(CopyContext *x) {
    static const char *const NAME = "a copy of four layers with FlipZ puts the last layer first";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage from, to;
    DkImageLayout layout;
    uint8_t *from_cpu = NULL, *to_cpu = NULL;
    DkCopyBuf staged;
    DkGpuAddr address = 0;
    uint8_t *const cpu = Buffer(x, TEXELS * 4u * COPY_LAYERS, 0x00u, &address);
    if (cpu == NULL ||
        !GpuCreateArrayImage(&x->gpu, &from, &layout, DkImageFormat_RGBA8_Unorm, SIDE, SIDE,
                             COPY_LAYERS, 1, &from_cpu) ||
        !GpuCreateArrayImage(&x->gpu, &to, &layout, DkImageFormat_RGBA8_Unorm, SIDE, SIDE,
                             COPY_LAYERS, 1, &to_cpu)) {
        Fail(x, NAME, "no room for the arrays");
        return;
    }
    for (uint32_t l = 0; l < COPY_LAYERS; ++l) {
        for (uint32_t i = 0; i < TEXELS; ++i) {
            PutWord(cpu + (l * TEXELS + i) * 4u, x->picture[l][i]);
        }
    }
    staged.addr = address;
    staged.rowLength = ROW;
    staged.imageHeight = ROW * SIDE;
    DkImageView source, destination;
    dkImageViewDefaults(&source, &from);
    dkImageViewDefaults(&destination, &to);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, COPY_LAYERS };
    dkCmdBufCopyBufferToImage(x->gpu.cmdbuf, &staged, &source, &rect, 0);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    dkCmdBufCopyImage(x->gpu.cmdbuf, &source, &rect, &destination, &rect, DkBlitFlag_FlipZ);
    const uint8_t *const got = Download(x, &to, SIDE, 4u, COPY_LAYERS);
    for (uint32_t l = 0; l < COPY_LAYERS; ++l) {
        for (uint32_t i = 0; i < TEXELS; ++i) {
            PutWord(x->expected + (l * TEXELS + i) * 4u,
                    x->picture[CopyFlippedLayer(l, COPY_LAYERS)][i]);
        }
    }
    x->expected_size = TEXELS * 4u * COPY_LAYERS;
    Judge(x, NAME, got, 4u, SIDE, "a 16 by 16 array of four layers copied with FlipZ");
}

// ---- the 2D engine

static DkImageFormat DkFormat(CopyFormat format) {
    switch (format) {
    case COPY_BGRA8:
        return DkImageFormat_BGRA8_Unorm;
    case COPY_RGB565:
        return DkImageFormat_RGB565_Unorm;
    case COPY_RGBA16F:
        return DkImageFormat_RGBA16_Float;
    default:
        return DkImageFormat_RGBA8_Unorm;
    }
}

/// Records a 1:1 blit, nearest, of the whole of @p from onto @p to with @p flags.
static void Blit(CopyContext *x, const DkImage *from, const DkImage *to, uint32_t scale,
                 uint32_t flags) {
    DkImageView source, destination;
    dkImageViewDefaults(&source, from);
    dkImageViewDefaults(&destination, to);
    const DkImageRect src = { 0, 0, 0, SIDE, SIDE, 1 };
    const DkImageRect dst = { 0, 0, 0, SIDE * scale, SIDE * scale, 1 };
    dkCmdBufBlitImage(x->gpu.cmdbuf, &source, &src, &destination, &dst,
                      DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit | flags, 0);
}

/// The full texel picture blitted from RGBA8 into @p format, or from @p format into RGBA8 when
/// @p into_rgba8.
static void ConvertCase(CopyContext *x, const char *name, CopyFormat format, bool into_rgba8) {
    if (!Begin(x, name)) {
        return;
    }
    const CopyFormat from_format = into_rgba8 ? format : COPY_RGBA8;
    const CopyFormat to_format = into_rgba8 ? COPY_RGBA8 : format;
    const uint32_t from_bytes = CopyFormatBytes(from_format);
    const uint32_t to_bytes = CopyFormatBytes(to_format);
    DkImage from, to;
    DkGpuAddr address = 0;
    uint8_t *const staged = Buffer(x, TEXELS * from_bytes, 0x00u, &address);
    if (staged == NULL || !Image(x, &from, DkFormat(from_format), 1) ||
        !Image(x, &to, DkFormat(to_format), 1)) {
        Fail(x, name, "no room for the images");
        return;
    }
    for (uint32_t i = 0; i < TEXELS; ++i) {
        CopyConvert(CopyFullTexel(i % SIDE, i / SIDE), from_format, staged + i * from_bytes);
        CopyConvert(CopyFullTexel(i % SIDE, i / SIDE), to_format, x->expected + i * to_bytes);
    }
    x->expected_size = TEXELS * to_bytes;
    DkImageView view;
    dkImageViewDefaults(&view, &from);
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    const DkCopyBuf source = { address, SIDE * from_bytes, 0 };
    dkCmdBufCopyBufferToImage(x->gpu.cmdbuf, &source, &view, &rect, 0);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    Blit(x, &from, &to, 1, 0);
    const uint8_t *const got = Download(x, &to, SIDE, to_bytes, 1);
    char what[96];
    snprintf(what, sizeof(what), "channels of nought and full blitted 1:1 into a %u byte format",
             (unsigned)to_bytes);
    Judge(x, name, got, to_bytes, SIDE, what);
}

static void FlipBlitCase(CopyContext *x) {
    static const char *const NAME = "a blit with FlipX and FlipY turns the picture around";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage from, to;
    if (!Image(x, &from, DkImageFormat_RGBA8_Unorm, 1) ||
        !Image(x, &to, DkImageFormat_RGBA8_Unorm, 1) || !Upload(x, &from, x->picture[3])) {
        Fail(x, NAME, "no room for the images");
        return;
    }
    Blit(x, &from, &to, 1, DkBlitFlag_FlipX | DkBlitFlag_FlipY);
    const uint8_t *const got = Download(x, &to, SIDE, 4u, 1);
    ExpectFlipped(x, 3, true, true);
    Judge(x, NAME, got, 4u, SIDE, "a 1:1 blit, nearest, mirrored both ways");
}

static void ScaleCase(CopyContext *x) {
    static const char *const NAME =
        "a blit to twice the size, nearest, repeats each texel over two by two pixels";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage from, to;
    if (!Image(x, &from, DkImageFormat_RGBA8_Unorm, 1) ||
        !Image(x, &to, DkImageFormat_RGBA8_Unorm, 2) || !Upload(x, &from, x->picture[1])) {
        Fail(x, NAME, "no room for the images");
        return;
    }
    Blit(x, &from, &to, 2, 0);
    const uint8_t *const got = Download(x, &to, SIDE * 2u, 4u, 1);
    for (uint32_t y = 0; y < SIDE * 2u; ++y) {
        for (uint32_t xx = 0; xx < SIDE * 2u; ++xx) {
            PutWord(x->expected + (y * SIDE * 2u + xx) * 4u,
                    x->picture[1][CopyScaledColumn(y, 2) * SIDE + CopyScaledColumn(xx, 2)]);
        }
    }
    x->expected_size = TEXELS * 16u;
    Judge(x, NAME, got, 4u, SIDE * 2u, "16 by 16 blitted onto 32 by 32, nearest");
}

static void SrgbCase(CopyContext *x) {
    static const char *const NAME = "a blit between two sRGB images keeps the bytes";
    if (!Begin(x, NAME)) {
        return;
    }
    DkImage from, to;
    if (!Image(x, &from, DkImageFormat_RGBA8_Unorm_sRGB, 1) ||
        !Image(x, &to, DkImageFormat_RGBA8_Unorm_sRGB, 1) || !Upload(x, &from, x->picture[2])) {
        Fail(x, NAME, "no room for the images");
        return;
    }
    Blit(x, &from, &to, 1, 0);
    const uint8_t *const got = Download(x, &to, SIDE, 4u, 1);
    ExpectFlipped(x, 2, false, false);
    Judge(x, NAME, got, 4u, SIDE, "a 1:1 blit, nearest, sRGB to sRGB");
}

// ---- notes

/// A blit of one texel's worth, @p word in @p from_format, onto @p to_word in @p to_format with
/// @p flags; the note gives the destination's first texel.
static void BlitNote(CopyContext *x, const char *what, DkImageFormat from_format,
                     uint32_t from_bytes, const uint8_t *word, DkImageFormat to_format,
                     uint32_t to_bytes, uint32_t to_word, uint32_t flags) {
    GpuBeginList(&x->gpu);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    DkImage from, to;
    DkGpuAddr from_address = 0, to_address = 0;
    uint8_t *const from_cpu = Buffer(x, TEXELS * from_bytes, 0x00u, &from_address);
    uint8_t *const to_cpu = Buffer(x, TEXELS * to_bytes, 0x00u, &to_address);
    if (from_cpu == NULL || to_cpu == NULL || !Image(x, &from, from_format, 1) ||
        !Image(x, &to, to_format, 1)) {
        ReportNote("copy %s: no room (%s)", what, x->gpu.failure);
        return;
    }
    for (uint32_t i = 0; i < TEXELS; ++i) {
        memcpy(from_cpu + i * from_bytes, word, from_bytes);
        PutWord(to_cpu + i * to_bytes, to_word);
    }
    DkImageView view;
    const DkImageRect rect = { 0, 0, 0, SIDE, SIDE, 1 };
    const DkCopyBuf from_staged = { from_address, SIDE * from_bytes, 0 };
    const DkCopyBuf to_staged = { to_address, SIDE * to_bytes, 0 };
    dkImageViewDefaults(&view, &from);
    dkCmdBufCopyBufferToImage(x->gpu.cmdbuf, &from_staged, &view, &rect, 0);
    dkImageViewDefaults(&view, &to);
    dkCmdBufCopyBufferToImage(x->gpu.cmdbuf, &to_staged, &view, &rect, 0);
    dkCmdBufBarrier(x->gpu.cmdbuf, DkBarrier_Full, 0);
    DkImageView source, destination;
    dkImageViewDefaults(&source, &from);
    dkImageViewDefaults(&destination, &to);
    dkCmdBufBlitImage(x->gpu.cmdbuf, &source, &rect, &destination, &rect,
                      DkBlitFlag_FilterNearest | flags, 0);
    const uint8_t *const got = Download(x, &to, SIDE, to_bytes, 1);
    GpuSubmit(&x->gpu);
    if (!x->gpu.ready || got == NULL) {
        ReportNote("copy %s: no readback (%s)", what, x->gpu.failure);
        return;
    }
    ReportNote("copy %s: the first texel reads %02x %02x %02x %02x", what, got[0], got[1], got[2],
               got[3]);
}

static void Notes(CopyContext *x) {
    // 0x80 in each colour channel is 0.2158 linear: 0x37 when the 2D engine decodes the source,
    // 0x80 when it moves the bytes.
    static const uint8_t SRGB[4] = { 0x80, 0x80, 0x80, 0xFF };
    BlitNote(x, "sRGB 80 80 80 ff blitted into RGBA8 (37 37 37 ff decoded, 80 80 80 ff kept)",
             DkImageFormat_RGBA8_Unorm_sRGB, 4u, SRGB, DkImageFormat_RGBA8_Unorm, 4u, 0u,
             DkBlitFlag_ModeBlit);
    // A quarter, 0x3400 as a half, is 63.75 in eight bits: 3f truncated, 40 rounded.
    static const uint8_t QUARTER[8] = { 0x00, 0x34, 0x00, 0x34, 0x00, 0x34, 0x00, 0x3C };
    BlitNote(x, "RGBA16F of a quarter blitted into RGBA8 (3f truncated, 40 rounded)",
             DkImageFormat_RGBA16_Float, 8u, QUARTER, DkImageFormat_RGBA8_Unorm, 4u, 0u,
             DkBlitFlag_ModeBlit);
    // The blend operation over a destination of 00 00 ff ff, with a source of ff 00 00 at alpha
    // 0x80: what it mixes, or whether it copies, is what the note says.
    static const uint8_t HALF_RED[4] = { 0xFF, 0x00, 0x00, 0x80 };
    BlitNote(x, "the 2D engine's blend of ff 00 00 80 over 00 00 ff ff", DkImageFormat_RGBA8_Unorm,
             4u, HALF_RED, DkImageFormat_RGBA8_Unorm, 4u, 0xFFFF0000u, DkBlitFlag_ModeAlphaBlend);
}

void RunCopy(void) {
    CopyContext *const x = &context;
    static const char *const OPEN = "graphics device opens with room for the copies";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    GpuSpec spec;
    GpuSpecDefaults(&spec, SIDE, SIDE);
    // Each case takes its images and buffers anew and nothing is given back: a few KiB each.
    spec.data_size = 512u * 1024u;
    spec.image_size = 2u * 1024u * 1024u;
    spec.cpu_image_size = 512u * 1024u;
    const bool opened = GpuOpenSpec(&x->gpu, &spec);
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, opened, "%s", x->gpu.failure);
    if (!opened) {
        return;
    }
    for (uint32_t l = 0; l < COPY_LAYERS; ++l) {
        for (uint32_t i = 0; i < TEXELS; ++i) {
            x->picture[l][i] = CopyTexel(i % SIDE, i / SIDE, l);
        }
    }
    RoundTripCase(x);
    FromBufferCase(x, "a copy from a buffer with FlipY writes the rows bottom up", DkBlitFlag_FlipY,
                   "uploaded with FlipY, the copy engine stepping the buffer backwards");
    ToBufferCase(x, "a copy to a buffer with FlipY writes the rows bottom up", DkBlitFlag_FlipY,
                 "read out with FlipY, the copy engine stepping the buffer backwards");
    FromBufferCase(x, "a copy from a buffer with FlipX writes each row right to left",
                   DkBlitFlag_FlipX, "uploaded with FlipX, through the 2D engine");
    ToBufferCase(x, "a copy to a buffer with FlipX writes each row right to left", DkBlitFlag_FlipX,
                 "read out with FlipX, through the 2D engine");
    FromBufferCase(x, "a copy from a buffer with FlipX and FlipY turns the picture around",
                   DkBlitFlag_FlipX | DkBlitFlag_FlipY,
                   "uploaded with FlipX and FlipY, through the 2D engine");
    PaddedToBufferCase(x);
    PaddedFromBufferCase(x);
    RectangleCase(x);
    FormatBytesCase(x);
    LayersCase(x);
    ConvertCase(x, "a blit from RGBA8 to BGRA8 swaps red and blue", COPY_BGRA8, false);
    ConvertCase(x, "a blit from RGBA8 to RGB565 keeps nought and full in every channel",
                COPY_RGB565, false);
    ConvertCase(x, "a blit from RGBA8 to RGBA16F makes full channels one", COPY_RGBA16F, false);
    ConvertCase(x, "a blit from RGBA16F to RGBA8 makes channels of one full", COPY_RGBA16F, true);
    FlipBlitCase(x);
    ScaleCase(x);
    SrgbCase(x);
    Notes(x);
    GpuClose(&x->gpu);
}
