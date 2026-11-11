// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coherref.h"
#include "../coverageref.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Multisampled targets as memory holds them, which is where a game finds them when it copies one,
// samples one it rendered or loads one it wrote: a 4x4 target of four samples is an 8x8 image in
// memory, the samples of pixel (x, y) the 2x2 texels at (2x, 2y), sample i at (2x + i % 2,
// 2y + i / 2) (the 3D class's 2x2 layout, which ScarletNX's multisample tests hold its renderer to
// in guest memory; deko3d's DkMsMode_4x is that mode).
//
// Each target is cleared to a blue of 128 and drawn with a green of 128 over the triangle
// (0, 0), (4, 0), (0, 4) at the standard sample positions, in sixteenths (6, 2), (14, 6), (2, 10)
// and (10, 14): a sample is green when 16 (x + y) plus its two sixteenths is under 64, which no
// sample ties. So pixels with x + y up to 2 are all green, those from 4 all blue, and those of 3
// have samples 0 and 2 green and 1 and 3 blue. 128 makes every resolve exact: a quarter of it is
// 32, and the averages of four samples are 0, 32, 64, 96 and 128.
//
// The targets the processor reads live in processor-visible memory, read after the level two
// cache is written back, texel by texel where the block linear rule puts them (an 8x8 image of
// four bytes is one gob). A conservative triangle covers every sample of every pixel it crosses
// and none of a pixel it misses; the pixels its edge only touches at a corner are not judged.

static const char *const SUITE = "msaa";

/// The target's side in pixels, and its samples' in texels.
#define SIDE 4u
#define SAMPLE_SIDE 8u
/// The main target: a row of four fetched samples for every pixel of the 4x4 texture.
#define MAIN_WIDTH 16u
#define MAIN_HEIGHT 4u

#define GREEN 128u
#define BLUE 128u

/// The standard positions' x and y, in sixteenths, sample by sample: COVERAGE_STANDARD_4X.
static const uint8_t SAMPLE_X[4] = { 6, 14, 2, 10 };
static const uint8_t SAMPLE_Y[4] = { 2, 6, 10, 14 };

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader flat;
    DkShader fetch;
} MsaaContext;

static MsaaContext context;

/// Whether the triangle covers sample @p s of pixel (@p x, @p y).
static bool Green(uint32_t x, uint32_t y, uint32_t s) {
    return 16u * (x + y) + SAMPLE_X[s] + SAMPLE_Y[s] < 64u;
}

/// The texel of an 8x8 image in memory that holds sample @p s of pixel (@p x, @p y).
static void SampleTexel(uint32_t x, uint32_t y, uint32_t s, uint32_t *tx, uint32_t *ty) {
    *tx = 2u * x + s % 2u;
    *ty = 2u * y + s / 2u;
}

static uint32_t TexelOffset(uint32_t tx, uint32_t ty) {
    return CohBlockLinearOffset(tx * 4u, ty, SAMPLE_SIDE * 4u, 1);
}

static bool Fail(const char *name, const char *why) {
    ReportCase(SUITE, name, CHECK_NONE, false,
               context.gpu.failure[0] != '\0' ? context.gpu.failure : why);
    return false;
}

/// A 4x4 image of four samples, @p format, rendered into; in processor-visible memory when
/// @p bytes is given.
static bool Target(DkImage *out, DkImageFormat format, uint32_t flags, uint8_t **bytes) {
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, context.gpu.device);
    maker.type = DkImageType_2DMS;
    maker.flags = flags;
    maker.format = format;
    maker.msMode = DkMsMode_4x;
    maker.dimensions[0] = SIDE;
    maker.dimensions[1] = SIDE;
    DkImageLayout layout;
    if (!GpuCreateFromMaker(&context.gpu, out, &layout, &maker, bytes)) {
        return false;
    }
    if (bytes != NULL) {
        memset(*bytes, 0xA5, (size_t)dkImageLayoutGetSize(&layout));
    }
    return true;
}

/// Records into the open list: @p target bound with four samples at the standard positions,
/// cleared blue, and the triangle drawn green, conservatively when asked.
static bool Draw(const DkImage *target, bool conservative) {
    GpuSetTarget(&context.gpu, target, SIDE, SIDE);
    DkMultisampleState ms;
    dkMultisampleStateDefaults(&ms);
    ms.mode = DkMsMode_4x;
    ms.rasterizerMode = DkMsMode_4x;
    memcpy(ms.sampleLocations, COVERAGE_STANDARD_4X, sizeof(ms.sampleLocations));
    dkCmdBufBindMultisampleState(context.gpu.cmdbuf, &ms);
    dkCmdBufClearColorFloat(context.gpu.cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f,
                            (float)BLUE / 255.0f, 1.0f);
    dkCmdBufSetConservativeRasterEnable(context.gpu.cmdbuf, conservative);
    const float g = (float)GREEN / 255.0f;
    const GpuVertex triangle[3] = {
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, g, 0.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, g, 0.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, g, 0.0f, 1.0f },
    };
    const bool drawn = GpuDrawTriangles(&context.gpu, &context.vertex, &context.flat, triangle, 3);
    dkCmdBufSetConservativeRasterEnable(context.gpu.cmdbuf, false);
    return drawn;
}

/// Holds the 8x8 texels at @p bytes to the triangle's samples: green or blue, opaque, for every
/// sample @p judged leaves in.
static void JudgeSamples(const char *name, const uint8_t *bytes, bool conservative) {
    uint32_t wrong = 0, count = 0, fx = 0, fy = 0, fs = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 }, wanted[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t x = 0; x < SIDE; ++x) {
            if (conservative && x + y == 4u) {
                continue;
            }
            for (uint32_t s = 0; s < 4u; ++s) {
                const bool green = conservative ? x + y <= 3u : Green(x, y, s);
                const uint8_t want[4] = { 0, green ? GREEN : 0u, green ? 0u : BLUE, 255 };
                uint32_t tx, ty;
                SampleTexel(x, y, s, &tx, &ty);
                const uint8_t *const got = bytes + TexelOffset(tx, ty);
                ++count;
                if (memcmp(got, want, 4) != 0) {
                    if (wrong == 0) {
                        fx = x;
                        fy = y;
                        fs = s;
                        memcpy(seen, got, 4);
                        memcpy(wanted, want, 4);
                    }
                    ++wrong;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u samples wrong; the first, sample %u of pixel (%u, %u), is "
               "%02x%02x%02x%02x, expected %02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)count, (unsigned)fs, (unsigned)fx, (unsigned)fy, seen[0],
               seen[1], seen[2], seen[3], wanted[0], wanted[1], wanted[2], wanted[3]);
}

/// The triangle drawn into a processor-visible target, plainly or conservatively, and its
/// samples read from memory.
static void LayoutCase(const char *name, bool conservative) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    DkImage target;
    uint8_t *bytes = NULL;
    if (!Target(&target, DkImageFormat_RGBA8_Unorm, DkImageFlags_UsageRender, &bytes)) {
        Fail(name, "no room for the target");
        return;
    }
    GpuBeginList(&context.gpu);
    const bool drawn = Draw(&target, conservative);
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    GpuSubmit(&context.gpu);
    if (!drawn || !context.gpu.ready) {
        Fail(name, "the draw was not recorded");
        return;
    }
    JudgeSamples(name, bytes, conservative);
}

/// The triangle drawn, then the target resolved into a 4x4 image of one sample: every pixel the
/// average of its four samples, exact since each is 0 or 128.
static void ResolveCase(void) {
    static const char *const NAME = "a resolve of a 4x target averages each pixel's four samples";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    DkImage target, resolved;
    DkImageLayout layout;
    if (!Target(&target, DkImageFormat_RGBA8_Unorm, DkImageFlags_UsageRender, NULL) ||
        !GpuCreateRenderImage(&context.gpu, &resolved, &layout, DkImageFormat_RGBA8_Unorm, SIDE,
                              SIDE, DkMsMode_1x)) {
        Fail(NAME, "no room for the targets");
        return;
    }
    GpuBeginList(&context.gpu);
    const bool drawn = Draw(&target, false);
    // The resolve runs on the 2D engine, which the 3D pipeline's own barriers do not order.
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full, 0);
    DkImageView from, to;
    dkImageViewDefaults(&from, &target);
    dkImageViewDefaults(&to, &resolved);
    dkCmdBufResolveImage(context.gpu.cmdbuf, &from, &to);
    GpuSubmit(&context.gpu);
    const uint8_t *const pixels =
        drawn && context.gpu.ready ? GpuReadBackImage(&context.gpu, &resolved, SIDE, SIDE) : NULL;
    if (pixels == NULL) {
        Fail(NAME, "the draw, the resolve or the readback did not happen");
        return;
    }
    uint32_t wrong = 0, fx = 0, fy = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 }, wanted[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t x = 0; x < SIDE; ++x) {
            uint32_t greens = 0;
            for (uint32_t s = 0; s < 4u; ++s) {
                greens += Green(x, y, s) ? 1u : 0u;
            }
            const uint8_t want[4] = { 0, (uint8_t)(GREEN * greens / 4u),
                                      (uint8_t)(BLUE * (4u - greens) / 4u), 255 };
            const uint8_t *const got = GpuPixel(pixels, SIDE, x, y);
            if (memcmp(got, want, 4) != 0) {
                if (wrong == 0) {
                    fx = x;
                    fy = y;
                    memcpy(seen, got, 4);
                    memcpy(wanted, want, 4);
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong; the first, (%u, %u), is %02x%02x%02x%02x, expected "
               "%02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)(SIDE * SIDE), (unsigned)fx, (unsigned)fy, seen[0],
               seen[1], seen[2], seen[3], wanted[0], wanted[1], wanted[2], wanted[3]);
}

/// A 4x ZF32 target cleared to 0.75: every sample of every pixel holds the float in memory.
static void DepthCase(void) {
    static const char *const NAME = "a 4x ZF32 target cleared to 0.75 holds the float at every "
                                    "sample";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    DkImage target;
    uint8_t *bytes = NULL;
    if (!Target(&target, DkImageFormat_ZF32, DkImageFlags_UsageRender, &bytes)) {
        Fail(NAME, "no room for the target");
        return;
    }
    GpuBeginList(&context.gpu);
    DkImageView view;
    dkImageViewDefaults(&view, &target);
    dkCmdBufBindRenderTargets(context.gpu.cmdbuf, NULL, 0, &view);
    DkMultisampleState ms;
    dkMultisampleStateDefaults(&ms);
    ms.mode = DkMsMode_4x;
    ms.rasterizerMode = DkMsMode_4x;
    dkCmdBufBindMultisampleState(context.gpu.cmdbuf, &ms);
    DkDepthStencilState writing;
    dkDepthStencilStateDefaults(&writing);
    writing.depthTestEnable = 0;
    writing.depthWriteEnable = 1;
    writing.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(context.gpu.cmdbuf, &writing);
    dkCmdBufClearDepthStencil(context.gpu.cmdbuf, true, 0.75f, 0, 0);
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    GpuSubmit(&context.gpu);
    if (!context.gpu.ready) {
        Fail(NAME, "the clear was not recorded");
        return;
    }
    uint32_t wrong = 0, first = 0, seen = 0;
    for (uint32_t ty = 0; ty < SAMPLE_SIDE; ++ty) {
        for (uint32_t tx = 0; tx < SAMPLE_SIDE; ++tx) {
            uint32_t word = 0;
            memcpy(&word, bytes + TexelOffset(tx, ty), 4);
            if (word != 0x3F400000u) {
                if (wrong == 0) {
                    first = ty * SAMPLE_SIDE + tx;
                    seen = word;
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, wrong == 0,
               "%u of %u samples wrong; the first, texel (%u, %u), holds 0x%08x, expected "
               "0x3f400000",
               (unsigned)wrong, (unsigned)(SAMPLE_SIDE * SAMPLE_SIDE),
               (unsigned)(first % SAMPLE_SIDE), (unsigned)(first / SAMPLE_SIDE), (unsigned)seen);
}

/// A word unique to texel (@p tx, @p ty) of the 8x8 image, every byte of it different from the
/// others' and from the main target's clear.
static uint32_t Pattern(uint32_t tx, uint32_t ty) {
    const uint32_t i = ty * SAMPLE_SIDE + tx;
    return (0x11u + i) | (0x60u + i) << 8 | (0xA0u ^ i) << 16 | (0xC0u + (i ^ 0x15u)) << 24;
}

/// A 4x RGBA8_Uint texture the processor wrote as an 8x8 image, each sample of each pixel fetched
/// onto its own pixel (msfetch.frag.glsl: sample p.x % 4 of pixel (p.x / 4, p.y)): the word at
/// (4x + i, y) is the texel at (2x + i % 2, 2y + i / 2).
static void FetchCase(void) {
    static const char *const NAME = "each sample of a 4x texture the processor wrote is fetched "
                                    "from its place in the 2x2 layout";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    DkImage texture;
    uint8_t *bytes = NULL;
    DkGpuAddr image_set = 0, sampler_set = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &context.gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT,
        &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        &context.gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
        &sampler_set);
    if (images == NULL || samplers == NULL ||
        !Target(&texture, DkImageFormat_RGBA8_Uint, 0, &bytes)) {
        Fail(NAME, "no room for the texture");
        return;
    }
    for (uint32_t ty = 0; ty < SAMPLE_SIDE; ++ty) {
        for (uint32_t tx = 0; tx < SAMPLE_SIDE; ++tx) {
            const uint32_t word = Pattern(tx, ty);
            memcpy(bytes + TexelOffset(tx, ty), &word, 4);
        }
    }
    DkImageView view;
    dkImageViewDefaults(&view, &texture);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    GpuClearUint(&context.gpu, 0, 0, 0, 0);
    GpuBeginPass(&context.gpu);
    dkCmdBufBindImageDescriptorSet(context.gpu.cmdbuf, image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(context.gpu.cmdbuf, sampler_set, 1);
    dkCmdBufBindTexture(context.gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    static const GpuVertex quad[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    const bool drawn = GpuDrawTriangles(&context.gpu, &context.vertex, &context.fetch, quad, 6);
    GpuSubmit(&context.gpu);
    const uint8_t *const pixels = drawn && context.gpu.ready ? GpuReadBack(&context.gpu) : NULL;
    if (pixels == NULL) {
        Fail(NAME, "the draw or the readback did not happen");
        return;
    }
    uint32_t wrong = 0, fx = 0, fy = 0, fs = 0, seen = 0, wanted = 0;
    for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t x = 0; x < SIDE; ++x) {
            for (uint32_t s = 0; s < 4u; ++s) {
                uint32_t tx, ty, got = 0;
                SampleTexel(x, y, s, &tx, &ty);
                memcpy(&got, GpuPixel(pixels, MAIN_WIDTH, 4u * x + s, y), 4);
                if (got != Pattern(tx, ty)) {
                    if (wrong == 0) {
                        fx = x;
                        fy = y;
                        fs = s;
                        seen = got;
                        wanted = Pattern(tx, ty);
                    }
                    ++wrong;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, wrong == 0,
               "%u of %u samples wrong; the first, sample %u of pixel (%u, %u), is 0x%08x, "
               "expected 0x%08x",
               (unsigned)wrong, (unsigned)(SIDE * SIDE * 4u), (unsigned)fs, (unsigned)fx,
               (unsigned)fy, (unsigned)seen, (unsigned)wanted);
}

void RunMsaa(void) {
    static const char *const OPEN = "graphics device opens with room for the targets";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    memset(&context, 0, sizeof(context));
    GpuSpec spec;
    GpuSpecDefaults(&spec, MAIN_WIDTH, MAIN_HEIGHT);
    spec.color_format = DkImageFormat_RGBA8_Uint;
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.image_size = 1024u * 1024u;
    spec.cpu_image_size = 1024u * 1024u;
    const bool opened = GpuOpenSpec(&context.gpu, &spec);
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, opened, "%s", context.gpu.failure);
    if (!opened) {
        return;
    }
    static const char *const LOAD = "the shaders load";
    if (!ReportStart(SUITE, LOAD)) {
        GpuClose(&context.gpu);
        return;
    }
    const bool loaded = GpuLoadShader(&context.gpu, &context.vertex, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&context.gpu, &context.flat, ShaderData(SHADER_FLAT_FRAG),
                                      ShaderSize(SHADER_FLAT_FRAG)) &&
                        GpuLoadShader(&context.gpu, &context.fetch, ShaderData(SHADER_MSFETCH_FRAG),
                                      ShaderSize(SHADER_MSFETCH_FRAG));
    CHECK_TRUE(SUITE, LOAD, CHECK_NONE, loaded, "%s", context.gpu.failure);
    if (!loaded) {
        GpuClose(&context.gpu);
        return;
    }
    LayoutCase("a 4x target's samples lie in memory two by two, sample i at (2x + i % 2, "
               "2y + i / 2)",
               false);
    ResolveCase();
    DepthCase();
    FetchCase();
    LayoutCase("a conservative triangle covers every sample of each pixel it crosses", true);
    GpuClose(&context.gpu);
}
