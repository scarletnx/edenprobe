// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coherref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Depth textures the processor writes and a draw samples, texel by texel: the depth an emulator
// uploads from guest memory rather than renders. Every texel of a 16x4 image holds one word;
// the library's formats read the depth from the component their swizzle names (deko3d's format
// table: G of Z16 and Z24S8, R of ZF32), and Z24S8 keeps its 8-bit stencil in the low byte and
// its 24-bit depth above it. Each depth is chosen so the byte the RGBA8 target stores is a
// whole number of 255ths or at least a quarter away from a half, so the tier is exact. Z16 and
// ZF32 are the controls: every host has a depth format of their size. A host without
// D24_UNORM_S8_UINT has to convert the 24-bit words; one that copies them as they are samples
// the words as floats.
//
// Then the other way round: depth targets the graphics unit clears and the processor reads, the
// bytes a game finds in memory when it copies a depth buffer through it. Each format is cleared
// to depth 0.75 and stencil 0x5a; 0.75 is 0xbfffff in 24 bits and 0xbfff in 16 whether the
// conversion rounds or truncates, so the words are exact. The library sends Z24S8 and Z24X8 to the
// surface format with the stencil in the low byte (deko3d's S8Z24Unorm, 0x14) and ZF32_X24S8 to
// the float with the stencil in the low byte of the next word. What the low byte of Z24X8 and the
// 24 bits above ZF32_X24S8's stencil hold is no format's to say, so a note gives it and the case
// judges the rest. The stencil of the packed formats is also sampled, through a view of it, and a
// three layer target cleared a layer at a time keeps each layer's clear, read back and sampled.

static const char *const SUITE = "zeta";

#define TARGET_WIDTH 16u
#define TARGET_HEIGHT 16u
#define TEX_WIDTH 16u
#define TEX_HEIGHT 4u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

/// Samples a 16x4 @p format texture whose every texel the processor wrote as @p word (and
/// @p high above it, for an eight byte format) through a view of its @p source, and expects
/// @p want in every pixel. A depth source takes the depth fetching shader, a stencil one the
/// unsigned one, which folds the four components into one word (swizzle.frag.glsl): the view
/// puts the stencil in all four, so every byte of the pixel is the stencil.
static void ZetaSampleCase(GpuContext *gpu, const DkShader *vertex_shader,
                           const DkShader *fragment_shader, const char *name, DkImageFormat format,
                           uint32_t bytes_per_texel, uint32_t word, uint32_t high,
                           DkDsSource source, const uint8_t want[4]) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    DkImage image;
    DkImageLayout layout;
    uint8_t *memory = NULL;
    DkGpuAddr image_set = 0;
    DkGpuAddr sampler_set = 0;
    DkGpuAddr size_address = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    uint32_t *const size_words =
        (uint32_t *)GpuAllocData(gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &size_address);
    if (images == NULL || samplers == NULL || size_words == NULL ||
        !GpuCreateImage(gpu, &image, &layout, format, TEX_WIDTH, TEX_HEIGHT, 0, 0, &memory)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the texture");
        return;
    }
    // Four rows tall is one gob tall, so the library's tile is one gob.
    for (uint32_t y = 0; y < TEX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TEX_WIDTH; ++x) {
            const uint32_t offset = CohBlockLinearOffset(x * bytes_per_texel, y,
                                                         TEX_WIDTH * bytes_per_texel, 1);
            memcpy(memory + offset, &word, bytes_per_texel < 4u ? bytes_per_texel : 4u);
            if (bytes_per_texel == 8u) {
                memcpy(memory + offset + 4u, &high, 4u);
            }
        }
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    view.dsSource = source;
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_None;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    memset(size_words, 0, UNIFORM_SIZE);
    size_words[0] = TEX_WIDTH;
    size_words[1] = TEX_HEIGHT;

    static const GpuVertex quad[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(gpu);
    dkCmdBufBindImageDescriptorSet(gpu->cmdbuf, image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(gpu->cmdbuf, sampler_set, 1);
    dkCmdBufBindTexture(gpu->cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, size_address, UNIFORM_SIZE);
    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, quad, 6);
    GpuSubmit(gpu);
    const uint8_t *const pixels = drawn && gpu->ready ? GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }
    const uint8_t expect[4] = { want[0], want[1], want[2], want[3] };
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (y >= TEX_HEIGHT) {
                PictureSet(picture, x, y, pixel, PICTURE_UNJUDGED);
                continue;
            }
            const bool right = memcmp(pixel, expect, 4) == 0;
            PictureSet(picture, x, y, expect, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                if (wrong == 0) {
                    memcpy(seen, pixel, 4);
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u texels wrong; the first is %02x%02x%02x%02x, expected %02x%02x%02x%02x "
               "(word 0x%08x)",
               (unsigned)wrong, (unsigned)(TEX_WIDTH * TEX_HEIGHT), seen[0], seen[1], seen[2],
               seen[3], expect[0], expect[1], expect[2], expect[3], (unsigned)word);
}

/// The depth of a texture sampled into every channel but alpha, as the library's depth view gives
/// it (deko3d's format table: the depth in x, y and z, one in w).
static void ZetaCase(GpuContext *gpu, const DkShader *vertex_shader,
                     const DkShader *fragment_shader, const char *name, DkImageFormat format,
                     uint32_t bytes_per_texel, uint32_t word, uint8_t want) {
    const uint8_t expect[4] = { want, want, want, 255 };
    ZetaSampleCase(gpu, vertex_shader, fragment_shader, name, format, bytes_per_texel, word, 0,
                   DkDsSource_Depth, expect);
}

/// The stencil of a texture through a view of it, the same byte in all four channels.
static void StencilCase(GpuContext *gpu, const DkShader *vertex_shader,
                        const DkShader *unsigned_shader, const char *name, DkImageFormat format,
                        uint32_t bytes_per_texel, uint32_t word, uint32_t high, uint8_t want) {
    const uint8_t expect[4] = { want, want, want, want };
    ZetaSampleCase(gpu, vertex_shader, unsigned_shader, name, format, bytes_per_texel, word, high,
                   DkDsSource_Stencil, expect);
}

/// The depth and stencil every written back case clears to.
#define CLEAR_DEPTH 0.75f
#define CLEAR_STENCIL 0x5Au

/// Binds @p view as the only target, depth and stencil writes open, the viewport and scissor
/// over a TEX_WIDTH by TEX_HEIGHT image, and clears it to the depth and stencil above.
static void ClearDepthTarget(GpuContext *gpu, const DkImageView *view, bool stencil) {
    dkCmdBufBindRenderTargets(gpu->cmdbuf, NULL, 0, view);
    const DkViewport viewport = { 0.0f, 0.0f, (float)TEX_WIDTH, (float)TEX_HEIGHT, 0.0f, 1.0f };
    dkCmdBufSetViewports(gpu->cmdbuf, 0, &viewport, 1);
    const DkScissor scissor = { 0, 0, TEX_WIDTH, TEX_HEIGHT };
    dkCmdBufSetScissors(gpu->cmdbuf, 0, &scissor, 1);
    DkDepthStencilState writing;
    dkDepthStencilStateDefaults(&writing);
    writing.depthTestEnable = 0;
    writing.depthWriteEnable = 1;
    writing.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(gpu->cmdbuf, &writing);
    dkCmdBufClearDepthStencil(gpu->cmdbuf, true, CLEAR_DEPTH, stencil ? 0xFFu : 0u,
                              stencil ? CLEAR_STENCIL : 0u);
}

/// A 16x4 @p format target the processor can read, cleared by the graphics unit, its level two
/// cache written back, and every texel's first word held to @p want under @p mask (and, for an
/// eight byte format, its second word to @p want_high under @p mask_high). The bytes the masks
/// leave out go into a note when they are not all one value: no format says what they hold.
static void WrittenBackCase(GpuContext *gpu, const char *name, DkImageFormat format,
                            uint32_t bytes_per_texel, bool stencil, uint32_t want, uint32_t mask,
                            uint32_t want_high, uint32_t mask_high) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, gpu->device);
    maker.flags = DkImageFlags_UsageRender;
    maker.format = format;
    maker.dimensions[0] = TEX_WIDTH;
    maker.dimensions[1] = TEX_HEIGHT;
    DkImage image;
    DkImageLayout layout;
    uint8_t *memory = NULL;
    if (!GpuCreateFromMaker(gpu, &image, &layout, &maker, &memory)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the target");
        return;
    }
    // A byte the clear never reaches shows as 0xa5, which no expected word here holds.
    memset(memory, 0xA5, (size_t)dkImageLayoutGetSize(&layout));
    GpuBeginList(gpu);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    ClearDepthTarget(gpu, &view, stencil);
    // What a game does before the processor reads a target: the work done and the level two
    // cache, where the clear sits, written back to memory.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    GpuSubmit(gpu);
    if (!gpu->ready) {
        ReportCase(SUITE, name, CHECK_NONE, false, gpu->failure);
        return;
    }
    uint32_t wrong = 0, seen = 0, seen_high = 0, rest = 0, rest_high = 0;
    bool rest_varies = false;
    for (uint32_t y = 0; y < TEX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TEX_WIDTH; ++x) {
            const uint32_t offset = CohBlockLinearOffset(x * bytes_per_texel, y,
                                                         TEX_WIDTH * bytes_per_texel, 1);
            uint32_t word = 0, high = 0;
            memcpy(&word, memory + offset, bytes_per_texel < 4u ? bytes_per_texel : 4u);
            if (bytes_per_texel == 8u) {
                memcpy(&high, memory + offset + 4u, 4u);
            }
            if (x == 0 && y == 0) {
                rest = word & ~mask;
                rest_high = high & ~mask_high;
            } else if ((word & ~mask) != rest || (high & ~mask_high) != rest_high) {
                rest_varies = true;
            }
            if ((word & mask) != want || (high & mask_high) != want_high) {
                if (wrong == 0) {
                    seen = word;
                    seen_high = high;
                }
                ++wrong;
            }
        }
    }
    const uint32_t full = bytes_per_texel == 1u   ? 0xFFu
                          : bytes_per_texel == 2u ? 0xFFFFu
                                                  : 0xFFFFFFFFu;
    if (wrong == 0 && mask != full) {
        ReportNote("%s %s: the bits outside 0x%08x hold 0x%08x%s", SUITE, name, (unsigned)mask,
                   (unsigned)rest, rest_varies ? ", not the same in every texel" : "");
    }
    if (wrong == 0 && bytes_per_texel == 8u && mask_high != 0xFFFFFFFFu) {
        ReportNote("%s %s: the bits of the second word outside 0x%08x hold 0x%08x%s", SUITE, name,
                   (unsigned)mask_high, (unsigned)rest_high,
                   rest_varies ? ", not the same in every texel" : "");
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u texels wrong; the first holds 0x%08x 0x%08x, expected 0x%08x 0x%08x "
               "under the masks 0x%08x 0x%08x",
               (unsigned)wrong, (unsigned)(TEX_WIDTH * TEX_HEIGHT), (unsigned)seen,
               (unsigned)seen_high, (unsigned)want, (unsigned)want_high, (unsigned)mask,
               (unsigned)mask_high);
}

#define LAYERS 3u

/// A three layer @p format target, each layer cleared alone through a view of it to its own depth
/// (0.2, 0.4 and 0.6, which are 51, 102 and 153 of 255 to within a hundredth), then either each
/// layer copied out by the copy engine, the float bits held exact (ZF32), or every layer sampled
/// onto its own row of pixels (fetch_array.frag.glsl), the 255ths held exact.
static void LayeredCase(GpuContext *gpu, const DkShader *vertex_shader,
                        const DkShader *array_shader, const char *name, DkImageFormat format,
                        bool sampled) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    static const float DEPTHS[LAYERS] = { 0.2f, 0.4f, 0.6f };
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, gpu->device);
    maker.type = DkImageType_2DArray;
    maker.flags = DkImageFlags_UsageRender;
    maker.format = format;
    maker.dimensions[0] = TEX_WIDTH;
    maker.dimensions[1] = TEX_HEIGHT;
    maker.dimensions[2] = LAYERS;
    DkImage image;
    DkImageLayout layout;
    DkGpuAddr out_address = 0, image_set = 0, sampler_set = 0, grid_address = 0;
    const uint32_t layer_bytes = TEX_WIDTH * TEX_HEIGHT * 4u;
    uint8_t *const out = (uint8_t *)GpuAllocData(gpu, layer_bytes * LAYERS, 256u, &out_address);
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    int32_t *const grid =
        (int32_t *)GpuAllocData(gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &grid_address);
    if (out == NULL || images == NULL || samplers == NULL || grid == NULL ||
        !GpuCreateFromMaker(gpu, &image, &layout, &maker, NULL)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the target");
        return;
    }
    memset(out, 0xA5, layer_bytes * LAYERS);
    GpuBeginList(gpu);
    for (uint32_t layer = 0; layer < LAYERS; ++layer) {
        DkImageView view;
        dkImageViewDefaults(&view, &image);
        view.layerOffset = (uint16_t)layer;
        view.layerCount = 1;
        dkCmdBufBindRenderTargets(gpu->cmdbuf, NULL, 0, &view);
        DkDepthStencilState writing;
        dkDepthStencilStateDefaults(&writing);
        writing.depthTestEnable = 0;
        writing.depthWriteEnable = 1;
        writing.stencilTestEnable = 0;
        dkCmdBufBindDepthStencilState(gpu->cmdbuf, &writing);
        dkCmdBufClearDepthStencil(gpu->cmdbuf, true, DEPTHS[layer], 0, 0);
    }
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
    if (!sampled) {
        for (uint32_t layer = 0; layer < LAYERS; ++layer) {
            DkImageView view;
            dkImageViewDefaults(&view, &image);
            view.layerOffset = (uint16_t)layer;
            view.layerCount = 1;
            const DkImageRect rect = { 0, 0, 0, TEX_WIDTH, TEX_HEIGHT, 1 };
            const DkCopyBuf destination = { out_address + layer * layer_bytes, 0, 0 };
            dkCmdBufCopyImageToBuffer(gpu->cmdbuf, &view, &rect, &destination, 0);
        }
        dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);
        GpuSubmit(gpu);
        if (!gpu->ready) {
            ReportCase(SUITE, name, CHECK_NONE, false, gpu->failure);
            return;
        }
        uint32_t wrong = 0, first_layer = 0, seen = 0, want_first = 0;
        for (uint32_t layer = 0; layer < LAYERS; ++layer) {
            uint32_t want = 0;
            memcpy(&want, &DEPTHS[layer], 4);
            for (uint32_t i = 0; i < TEX_WIDTH * TEX_HEIGHT; ++i) {
                uint32_t got = 0;
                memcpy(&got, out + layer * layer_bytes + i * 4u, 4);
                if (got != want) {
                    if (wrong == 0) {
                        first_layer = layer;
                        seen = got;
                        want_first = want;
                    }
                    ++wrong;
                }
            }
        }
        CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
                   "%u of %u texels wrong; the first, in layer %u, holds 0x%08x, expected 0x%08x",
                   (unsigned)wrong, (unsigned)(TEX_WIDTH * TEX_HEIGHT * LAYERS),
                   (unsigned)first_layer, (unsigned)seen, (unsigned)want_first);
        return;
    }
    // Sampled: cells as wide as the texture and as tall, a row of cells per layer.
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_None;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    memset(grid, 0, UNIFORM_SIZE);
    grid[0] = 4; // log2 of 16, the cell width
    grid[1] = 2; // log2 of 4
    grid[2] = 1;
    grid[3] = (int32_t)LAYERS;
    static const GpuVertex quad[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    GpuSubmit(gpu);
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(gpu);
    dkCmdBufBindImageDescriptorSet(gpu->cmdbuf, image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(gpu->cmdbuf, sampler_set, 1);
    dkCmdBufBindTexture(gpu->cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, grid_address, UNIFORM_SIZE);
    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, array_shader, quad, 6);
    GpuSubmit(gpu);
    const uint8_t *const pixels = drawn && gpu->ready ? GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }
    static const uint8_t WANT[LAYERS] = { 51, 102, 153 };
    uint32_t wrong = 0, first_layer = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    for (uint32_t layer = 0; layer < LAYERS; ++layer) {
        for (uint32_t y = 0; y < TEX_HEIGHT; ++y) {
            for (uint32_t x = 0; x < TEX_WIDTH; ++x) {
                const uint8_t *const pixel =
                    GpuPixel(pixels, TARGET_WIDTH, x, layer * TEX_HEIGHT + y);
                const uint8_t want = WANT[layer];
                if (pixel[0] != want || pixel[1] != want || pixel[2] != want || pixel[3] != 255) {
                    if (wrong == 0) {
                        first_layer = layer;
                        memcpy(seen, pixel, 4);
                    }
                    ++wrong;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u texels wrong; the first, in layer %u, is %02x%02x%02x%02x, expected "
               "%02x%02x%02x ff",
               (unsigned)wrong, (unsigned)(TEX_WIDTH * TEX_HEIGHT * LAYERS),
               (unsigned)first_layer, seen[0], seen[1], seen[2], seen[3], WANT[first_layer],
               WANT[first_layer], WANT[first_layer]);
}

void RunZeta(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    // Room for every texture and written back target at the library's image alignment, and for
    // the two layered targets in the ordinary image block.
    spec.cpu_image_size = 16u * 128u * 1024u;
    spec.image_size = 4u * 128u * 1024u;

    if (!ReportStart(SUITE, "graphics device opens with room for the depth textures")) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the depth textures", CHECK_NONE,
               opened, "%s", gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the fetching shader loads")) {
        GpuClose(&gpu);
        return;
    }
    DkShader vertex_shader;
    DkShader fragment_shader;
    DkShader unsigned_shader;
    DkShader array_shader;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_FETCH_FRAG),
                                      ShaderSize(SHADER_FETCH_FRAG)) &&
                        GpuLoadShader(&gpu, &unsigned_shader, ShaderData(SHADER_SWIZZLE_FRAG),
                                      ShaderSize(SHADER_SWIZZLE_FRAG)) &&
                        GpuLoadShader(&gpu, &array_shader, ShaderData(SHADER_FETCH_ARRAY_FRAG),
                                      ShaderSize(SHADER_FETCH_ARRAY_FRAG));
    CHECK_TRUE(SUITE, "the fetching shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    // 0x8080 is 128 * 257 of 255 * 257: exactly 128 / 255.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a Z16 texture the processor wrote samples to its depth", DkImageFormat_Z16, 2,
             0x8080u, 128);
    // 0.75 * 255 = 191.25.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a ZF32 texture the processor wrote samples to its depth", DkImageFormat_ZF32, 4,
             0x3F400000u, 191);
    // Depth 0x808080 = 128 * 0x010101 over 255 * 0x010101, stencil 0x80: the same word read
    // either way round, so only a host that samples the word as a float gets it wrong.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a Z24S8 texture the processor wrote samples to its depth", DkImageFormat_Z24S8, 4,
             0x80808080u, 128);
    // Depth 0x00ff00 above stencil 0x5a: 65280 / 16777215 * 255 = 0.992, so 1. The low 24 bits
    // (0xff005a) would give 254.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a Z24S8 texture the processor wrote takes its depth from the top 24 bits",
             DkImageFormat_Z24S8, 4, 0x00FF005Au, 1);
    // The stencil through a view of it: the low byte of a Z24S8 word, the low byte of the word
    // after a ZF32_X24S8 float. 0x00ff005a's low 24 bits would read 0x5a either way, so the word
    // puts 0x5a only where the stencil is; a host that took the top byte would read 0x00.
    StencilCase(&gpu, &vertex_shader, &unsigned_shader,
                "a Z24S8 texture the processor wrote samples its stencil from the low byte",
                DkImageFormat_Z24S8, 4, 0x00FF005Au, 0, 0x5A);
    // 0.75 over stencil 0x5a, above it bits a host could take for either: 0xc3 in the byte
    // above the stencil.
    ZetaSampleCase(&gpu, &vertex_shader, &fragment_shader,
                   "a ZF32_X24S8 texture the processor wrote samples its depth from the float",
                   DkImageFormat_ZF32_X24S8, 8, 0x3F400000u, 0x0000C35Au, DkDsSource_Depth,
                   (const uint8_t[4]){ 191, 191, 191, 255 });
    StencilCase(&gpu, &vertex_shader, &unsigned_shader,
                "a ZF32_X24S8 texture the processor wrote samples its stencil from the next word",
                DkImageFormat_ZF32_X24S8, 8, 0x3F400000u, 0x0000C35Au, 0x5A);

    // Written back: 0.75 is 0xbfffff in 24 bits and 0xbfff in 16, under either rounding.
    WrittenBackCase(&gpu, "a Z24S8 target cleared to 0.75 over 0x5a holds 0xbfffff5a in memory",
                    DkImageFormat_Z24S8, 4, true, 0xBFFFFF5Au, 0xFFFFFFFFu, 0, 0);
    WrittenBackCase(&gpu, "a Z24X8 target cleared to 0.75 holds 0xbfffff above its low byte",
                    DkImageFormat_Z24X8, 4, false, 0xBFFFFF00u, 0xFFFFFF00u, 0, 0);
    WrittenBackCase(&gpu, "a Z16 target cleared to 0.75 holds 0xbfff in memory", DkImageFormat_Z16,
                    2, false, 0xBFFFu, 0xFFFFu, 0, 0);
    WrittenBackCase(&gpu, "a ZF32 target cleared to 0.75 holds its float in memory",
                    DkImageFormat_ZF32, 4, false, 0x3F400000u, 0xFFFFFFFFu, 0, 0);
    WrittenBackCase(&gpu,
                    "a ZF32_X24S8 target cleared to 0.75 over 0x5a holds the float, then 0x5a",
                    DkImageFormat_ZF32_X24S8, 8, true, 0x3F400000u, 0xFFFFFFFFu, 0x5Au, 0xFFu);
    WrittenBackCase(&gpu, "an S8 target cleared to 0x5a holds 0x5a in memory", DkImageFormat_S8, 1,
                    true, 0x5Au, 0xFFu, 0, 0);

    LayeredCase(&gpu, &vertex_shader, &array_shader,
                "each layer of a three layer ZF32 target keeps its own clear", DkImageFormat_ZF32,
                false);
    LayeredCase(&gpu, &vertex_shader, &array_shader,
                "a three layer Z24S8 target cleared a layer at a time samples each layer's depth",
                DkImageFormat_Z24S8, true);
    GpuClose(&gpu);
}
