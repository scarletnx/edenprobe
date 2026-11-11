// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coherref.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// What the sampler hands a shader for a texel, by the texture's format and the sampler's and the
// descriptor's own settings: the formats a game's driver describes (ScarletNX's descriptor tests
// encode them one by one), the sampler's min/max reduction, and a rectangle texture's texel
// coordinates.
//
// Every texture is 4x4 (2x1 for the reduction), written by the processor where the block linear
// rule puts each texel, and fetched or sampled onto a 4x4 target of 32-bit floats, so what the
// sampler returned arrives unrounded. The formats: one channel's place and the defaults of the
// channels a format lacks (nought, and one for alpha: deko3d's format table), alpha one in a
// format that keeps none whatever memory holds there, the bytes of BGRA8 and the bit fields of the
// packed formats, a signed byte of -128 clamped to -1, and the small floats. Every value is one a
// format holds exactly and every conversion lands on it, so the tier is exact.
//
// The reduction: a linear lookup halfway between an R32F texel of nought and one of one, plain
// (their average), with the minimum and with the maximum reduction (the sampler's word 1, bits 10
// and 11). Reduction 3 names no mode; what the console makes of it goes into a note, which
// ScarletNX's sampler test holds to the average. The rectangle: a view of a texture as a
// rectangle takes texel coordinates, so (3.5, 0.5) reads texel (3, 0), which a normalised view
// reads at (0.875, 0.125), and where a normalised view repeats (3.5, 0.5) to the middle, texel
// (2, 2).

static const char *const SUITE = "sampling";

#define SIDE 4u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader fetch;
    DkShader at;
} SamplingContext;

static SamplingContext context;

/// Set while a lookup only feeds a note: a failure then goes into a note too, since no case
/// started.
static bool noting;

static bool Fail(const char *name, const char *why) {
    const char *const text = context.gpu.failure[0] != '\0' ? context.gpu.failure : why;
    if (noting) {
        ReportNote("sampling %s: %s", name, text);
    } else {
        ReportCase(SUITE, name, CHECK_NONE, false, text);
    }
    return false;
}

/// A @p width by @p height texture of @p format in processor-visible memory, texel (x, y)
/// holding words[y * width + x], @p bytes of each.
static bool Texture(DkImage *out, DkImageFormat format, uint32_t bytes, uint32_t width,
                    uint32_t height, const uint32_t *words) {
    DkImageLayout layout;
    uint8_t *memory = NULL;
    if (!GpuCreateImage(&context.gpu, out, &layout, format, width, height, 0, 0, &memory)) {
        return false;
    }
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t offset = CohBlockLinearOffset(x * bytes, y, width * bytes, 1);
            memcpy(memory + offset, &words[y * width + x], bytes);
        }
    }
    return true;
}

/// Draws a quad over the target with @p shader, the texture under @p view and @p sampler, and
/// @p uniform's four words as its uniform buffer; hands back the target's pixels, four floats
/// each, or NULL with the case failed.
static const float *Draw(const char *name, const DkImageView *view, const DkSampler *sampler,
                         const DkShader *shader, const uint32_t uniform[4],
                         uint32_t sampler_word1_reduction) {
    DkGpuAddr image_set = 0, sampler_set = 0, uniform_address = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &context.gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT,
        &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        &context.gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
        &sampler_set);
    uint32_t *const words = (uint32_t *)GpuAllocData(&context.gpu, UNIFORM_SIZE,
                                                     DK_UNIFORM_BUF_ALIGNMENT, &uniform_address);
    if (images == NULL || samplers == NULL || words == NULL) {
        Fail(name, "no scratch left for the descriptors");
        return NULL;
    }
    dkImageDescriptorInitialize(&images[0], view, false, false);
    dkSamplerDescriptorInitialize(&samplers[0], sampler);
    if (sampler_word1_reduction != 0u) {
        // The reduction field, word 1 bits 10 and 11 (deko3d's TextureSamplerControl), set to a
        // value the library's enumeration has no name for.
        uint32_t raw[8];
        memcpy(raw, &samplers[0], sizeof(raw));
        raw[1] = (raw[1] & ~(3u << 10)) | (sampler_word1_reduction & 3u) << 10;
        memcpy(&samplers[0], raw, sizeof(raw));
    }
    memset(words, 0, UNIFORM_SIZE);
    memcpy(words, uniform, 16);
    static const GpuVertex quad[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    GpuClear(&context.gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&context.gpu);
    dkCmdBufBindImageDescriptorSet(context.gpu.cmdbuf, image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(context.gpu.cmdbuf, sampler_set, 1);
    dkCmdBufBindTexture(context.gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    dkCmdBufBindUniformBuffer(context.gpu.cmdbuf, DkStage_Fragment, 0, uniform_address,
                              UNIFORM_SIZE);
    const bool drawn = GpuDrawTriangles(&context.gpu, &context.vertex, shader, quad, 6);
    GpuSubmit(&context.gpu);
    const uint8_t *const pixels = drawn && context.gpu.ready ? GpuReadBack(&context.gpu) : NULL;
    if (pixels == NULL) {
        Fail(name, "the draw or the readback did not happen");
        return NULL;
    }
    return (const float *)pixels;
}

/// Every pixel of @p got, four floats each, against @p want, bit for bit (a nought of either
/// sign counting as nought).
static void JudgeAll(const char *name, const float *got, const float want[4]) {
    uint32_t wrong = 0, first = 0;
    for (uint32_t i = 0; i < SIDE * SIDE; ++i) {
        for (uint32_t c = 0; c < 4u; ++c) {
            if (!(got[i * 4u + c] == want[c])) {
                if (wrong == 0) {
                    first = i;
                }
                ++wrong;
                break;
            }
        }
    }
    const float *const seen = got + first * 4u;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong; the first, (%u, %u), is (%.9g, %.9g, %.9g, %.9g), expected "
               "(%.9g, %.9g, %.9g, %.9g)",
               (unsigned)wrong, (unsigned)(SIDE * SIDE), (unsigned)(first % SIDE),
               (unsigned)(first / SIDE), (double)seen[0], (double)seen[1], (double)seen[2],
               (double)seen[3], (double)want[0], (double)want[1], (double)want[2],
               (double)want[3]);
}

static void NearestSampler(DkSampler *sampler) {
    dkSamplerDefaults(sampler);
    sampler->minFilter = DkFilter_Nearest;
    sampler->magFilter = DkFilter_Nearest;
    sampler->mipFilter = DkMipFilter_None;
}

/// A 4x4 @p format texture, every texel @p word (its first @p bytes bytes), fetched: every
/// pixel must be @p want.
static void FormatCase(const char *name, DkImageFormat format, uint32_t bytes, uint32_t word,
                       float r, float g, float b, float a) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    uint32_t words[SIDE * SIDE];
    for (uint32_t i = 0; i < SIDE * SIDE; ++i) {
        words[i] = word;
    }
    DkImage image;
    if (!Texture(&image, format, bytes, SIDE, SIDE, words)) {
        Fail(name, "no room for the texture");
        return;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    DkSampler sampler;
    NearestSampler(&sampler);
    const uint32_t size[4] = { SIDE, SIDE, 0, 0 };
    const float *const got = Draw(name, &view, &sampler, &context.fetch, size, 0);
    if (got != NULL) {
        const float want[4] = { r, g, b, a };
        JudgeAll(name, got, want);
    }
}

static uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/// Looks the 2x1 R32F texture of nought and one up linearly, halfway between the two, with
/// @p mode as the sampler's reduction, or with @p raw_mode written into the field when it is not
/// nought. Hands back the pixels, or NULL with @p name's case failed.
static const float *Reduce(const char *name, DkSamplerReduction mode, uint32_t raw_mode) {
    const uint32_t words[2] = { Bits(0.0f), Bits(1.0f) };
    DkImage image;
    if (!Texture(&image, DkImageFormat_R32_Float, 4, 2, 1, words)) {
        Fail(name, "no room for the texture");
        return NULL;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Linear;
    sampler.magFilter = DkFilter_Linear;
    sampler.mipFilter = DkMipFilter_None;
    sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
    sampler.reductionMode = mode;
    // u 0.5 is the edge the two texels share, both a half texel away: weights of one half each.
    const uint32_t at[4] = { Bits(0.5f), Bits(0.5f), 0, 0 };
    return Draw(name, &view, &sampler, &context.at, at, raw_mode);
}

static void ReductionCase(const char *name, DkSamplerReduction mode, float want) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const float *const got = Reduce(name, mode, 0);
    if (got != NULL) {
        const float wanted[4] = { want, 0.0f, 0.0f, 1.0f };
        JudgeAll(name, got, wanted);
    }
}

/// Reduction 3, which the library's enumeration has no name for: what the lookup gives goes into
/// a note, since no specification says.
static void ReductionNote(void) {
    noting = true;
    const float *const got = Reduce("a reduction of 3", DkSamplerReduction_WeightedAverage, 3);
    noting = false;
    if (got != NULL) {
        ReportNote("sampling a reduction of 3, which names no mode: the lookup halfway between 0 "
                   "and 1 gave %.9g (the average is 0.5, the minimum 0, the maximum 1)",
                   (double)got[0]);
    }
}

/// The 4x4 R32F texture whose texel (x, y) holds 10 y + x, under a @p rectangle view or a plain
/// one, nearest and repeating, looked up at (@p u, @p v): every pixel must read texel
/// (@p tx, @p ty).
static void RectangleCase(const char *name, bool rectangle, float u, float v, uint32_t tx,
                          uint32_t ty) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    uint32_t words[SIDE * SIDE];
    for (uint32_t i = 0; i < SIDE * SIDE; ++i) {
        words[i] = Bits((float)(10u * (i / SIDE) + i % SIDE));
    }
    DkImage image;
    if (!Texture(&image, DkImageFormat_R32_Float, 4, SIDE, SIDE, words)) {
        Fail(name, "no room for the texture");
        return;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    if (rectangle) {
        view.type = DkImageType_Rectangle;
    }
    DkSampler sampler;
    NearestSampler(&sampler);
    sampler.wrapMode[0] = DkWrapMode_Repeat;
    sampler.wrapMode[1] = DkWrapMode_Repeat;
    const uint32_t at[4] = { Bits(u), Bits(v), 0, 0 };
    const float *const got = Draw(name, &view, &sampler, &context.at, at, 0);
    if (got != NULL) {
        const float want[4] = { (float)(10u * ty + tx), 0.0f, 0.0f, 1.0f };
        JudgeAll(name, got, want);
    }
}

void RunSampling(void) {
    static const char *const OPEN = "graphics device opens with a target of floats";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    memset(&context, 0, sizeof(context));
    GpuSpec spec;
    GpuSpecDefaults(&spec, SIDE, SIDE);
    spec.color_format = DkImageFormat_RGBA32_Float;
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = 32u * 128u * 1024u;
    const bool opened = GpuOpenSpec(&context.gpu, &spec);
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, opened, "%s", context.gpu.failure);
    if (!opened) {
        return;
    }
    static const char *const LOAD = "the sampling shaders load";
    if (!ReportStart(SUITE, LOAD)) {
        GpuClose(&context.gpu);
        return;
    }
    const bool loaded = GpuLoadShader(&context.gpu, &context.vertex, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&context.gpu, &context.fetch, ShaderData(SHADER_FETCH_FRAG),
                                      ShaderSize(SHADER_FETCH_FRAG)) &&
                        GpuLoadShader(&context.gpu, &context.at, ShaderData(SHADER_SAMPLE_AT_FRAG),
                                      ShaderSize(SHADER_SAMPLE_AT_FRAG));
    CHECK_TRUE(SUITE, LOAD, CHECK_NONE, loaded, "%s", context.gpu.failure);
    if (!loaded) {
        GpuClose(&context.gpu);
        return;
    }

    FormatCase("R8 0xff samples as 1 0 0 1", DkImageFormat_R8_Unorm, 1, 0xFFu, 1.0f, 0.0f, 0.0f,
               1.0f);
    FormatCase("RG8 00 ff samples as 0 1 0 1", DkImageFormat_RG8_Unorm, 2, 0xFF00u, 0.0f, 1.0f,
               0.0f, 1.0f);
    FormatCase("RGBX8 ff 00 ff 00 samples its alpha as 1", DkImageFormat_RGBX8_Unorm, 4,
               0x00FF00FFu, 1.0f, 0.0f, 1.0f, 1.0f);
    FormatCase("BGRA8 ff 00 00 ff samples blue from its first byte", DkImageFormat_BGRA8_Unorm, 4,
               0xFF0000FFu, 0.0f, 0.0f, 1.0f, 1.0f);
    FormatCase("R8 snorm 0x80 samples as -1", DkImageFormat_R8_Snorm, 1, 0x80u, -1.0f, 0.0f, 0.0f,
               1.0f);
    FormatCase("RGB565 0x07ff samples red from its low five bits", DkImageFormat_RGB565_Unorm, 2,
               0x07FFu, 1.0f, 1.0f, 0.0f, 1.0f);
    FormatCase("BGR565 0x07ff samples blue from its low five bits", DkImageFormat_BGR565_Unorm, 2,
               0x07FFu, 0.0f, 1.0f, 1.0f, 1.0f);
    FormatCase("RGB5A1 0x801f samples red low and alpha from the top bit",
               DkImageFormat_RGB5A1_Unorm, 2, 0x801Fu, 1.0f, 0.0f, 0.0f, 1.0f);
    FormatCase("RGBA4 0x0f0f samples red from the low nibble", DkImageFormat_RGBA4_Unorm, 2,
               0x0F0Fu, 1.0f, 0.0f, 1.0f, 0.0f);
    FormatCase("RGB10A2 0x3ff003ff samples as 1 0 1 0", DkImageFormat_RGB10A2_Unorm, 4,
               0x3FF003FFu, 1.0f, 0.0f, 1.0f, 0.0f);
    FormatCase("R16F 0xb555 samples as its half", DkImageFormat_R16_Float, 2, 0xB555u,
               -0.333251953125f, 0.0f, 0.0f, 1.0f);
    FormatCase("RG11B10F 0x801c03c0 samples as 1 0.5 2 1", DkImageFormat_RG11B10_Float, 4,
               0x801C03C0u, 1.0f, 0.5f, 2.0f, 1.0f);
    // Exponent 15, the bias: each nine bit mantissa over 512.
    FormatCase("E5BGR9 0x7807ff00 samples its shared exponent", DkImageFormat_E5BGR9_Float, 4,
               0x7807FF00u, 0.5f, 0.998046875f, 0.001953125f, 1.0f);

    ReductionCase("a linear lookup halfway between 0 and 1 averages them",
                  DkSamplerReduction_WeightedAverage, 0.5f);
    ReductionCase("the minimum reduction takes the lesser texel", DkSamplerReduction_Min, 0.0f);
    ReductionCase("the maximum reduction takes the greater texel", DkSamplerReduction_Max, 1.0f);
    ReductionNote();

    RectangleCase("a rectangle view reads texel (3, 0) at (3.5, 0.5)", true, 3.5f, 0.5f, 3, 0);
    RectangleCase("a normalised view reads texel (3, 0) at (0.875, 0.125)", false, 0.875f, 0.125f,
                  3, 0);
    RectangleCase("a normalised view repeats (3.5, 0.5) to texel (2, 2)", false, 3.5f, 0.5f, 2, 2);
    GpuClose(&context.gpu);
}
