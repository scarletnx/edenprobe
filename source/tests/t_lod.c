// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../lodref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The level of detail: which level a lookup reads, from explicit gradients or an explicit level,
// what the sampler's minimum, maximum and bias do to it, and an offset counted in the texels of
// the level the lookup picked. lodref.c has the rules and the texture: R32_Float, 16 by 8, five
// levels, every texel a whole number that names its level and place, so a pixel's float says
// where its lookup went.
//
// One pixel per lookup, sixteen across a 16 by 1 target, each told by a uniform array what to do
// (lod.frag.glsl), all at the coordinate (13/32, 13/32), which lodref.c's reference cases keep at
// least 3/16 of a texel from any boundary on any level. The gradients are whole powers of two in
// texels along one axis at a time, so lambda is a whole number however a machine measures the
// gradient's length, and nearest mip filtering is half a level from changing its mind. Exact,
// except the one blended lookup, which is the units in the last place tier at a budget of nought.

static const char *const SUITE = "lod";

#define LOOKUPS 16u
#define COORD (13.0 / 32.0)

enum { MODE_GRAD = 0, MODE_GRAD_OFFSET = 1, MODE_LOD = 2, MODE_LOD_OFFSET = 3 };

/// The samplers, in descriptor order.
enum {
    SAMPLER_PLAIN,
    SAMPLER_MIN_2,
    SAMPLER_MAX_1,
    SAMPLER_BIAS_1,
    SAMPLER_LINEAR_MIN_125,
    SAMPLERS,
};

typedef struct {
    float a[LOOKUPS][4]; ///< dPdx.xy, dPdy.xy
    float b[LOOKUPS][4]; ///< the explicit level, unused, unused, the mode
} LookupBlock;

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader lod;
    DkGpuAddr image_set;
    DkGpuAddr sampler_set;
} LodContext;

/// One lookup: @p mode with the gradients @p grad or the level @p level.
typedef struct {
    uint32_t mode;
    float grad[4];
    float level;
} Lookup;

/// The level the lookup reads through a sampler with the clamp [@p lo, @p hi] and @p bias.
static double LookupLambda(const Lookup *l, double bias, double lo, double hi) {
    double lambda = l->mode == MODE_LOD || l->mode == MODE_LOD_OFFSET
                        ? (double)l->level
                        : LodGradientLambda(l->grad[0], l->grad[1], l->grad[2], l->grad[3],
                                            LOD_WIDTH, LOD_HEIGHT);
    lambda += bias;
    if (lambda < lo) {
        lambda = lo;
    }
    if (lambda > hi) {
        lambda = hi;
    }
    return lambda;
}

static float LookupExpected(const Lookup *l, uint32_t sampler) {
    static const double bias[SAMPLERS] = { 0, 0, 0, 1, 0 };
    static const double lo[SAMPLERS] = { 0, 2, 0, 0, 1.25 };
    static const double hi[SAMPLERS] = { 1000, 1000, 1, 1000, 1000 };
    const double lambda = LookupLambda(l, bias[sampler], lo[sampler], hi[sampler]);
    if (sampler == SAMPLER_LINEAR_MIN_125) {
        return (float)LodExpectedLinear(lambda, COORD, COORD);
    }
    const int32_t offset = l->mode == MODE_GRAD_OFFSET || l->mode == MODE_LOD_OFFSET ? 1 : 0;
    return LodExpected(LodNearestLevel(lambda, LOD_LEVELS), COORD, COORD, offset, -offset);
}

/// Runs @p count lookups through @p sampler and judges them as @p name.
static void LookupCase(LodContext *c, const char *name, uint32_t sampler, const Lookup *lookups,
                       uint32_t count) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    DkGpuAddr block_addr = 0;
    LookupBlock *const block = (LookupBlock *)GpuAllocData(
        &c->gpu, (uint32_t)sizeof(LookupBlock), DK_UNIFORM_BUF_ALIGNMENT, &block_addr);
    if (block == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, "the scratch block had no room for the lookups");
        return;
    }
    memset(block, 0, sizeof(*block));
    for (uint32_t i = 0; i < LOOKUPS; ++i) {
        // Pixels without a lookup of their own read level 0 by an explicit level.
        const Lookup *const l = i < count ? &lookups[i] : NULL;
        if (l != NULL) {
            memcpy(block->a[i], l->grad, sizeof(l->grad));
            block->b[i][0] = l->level;
            block->b[i][3] = (float)l->mode;
        } else {
            block->b[i][3] = (float)MODE_LOD;
        }
    }
    GpuBeginPass(&c->gpu);
    dkCmdBufBindImageDescriptorSet(c->gpu.cmdbuf, c->image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(c->gpu.cmdbuf, c->sampler_set, SAMPLERS);
    dkCmdBufBindTexture(c->gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, sampler));
    dkCmdBufBindUniformBuffer(c->gpu.cmdbuf, DkStage_Fragment, 0, block_addr,
                              (uint32_t)sizeof(LookupBlock));
    static const GpuVertex full[6] = {
        { -1, -1, 0, 0, 0, 1, 1, 1, 1 }, { 1, -1, 0, 0, 0, 1, 1, 1, 1 },
        { 1, 1, 0, 0, 0, 1, 1, 1, 1 },   { -1, -1, 0, 0, 0, 1, 1, 1, 1 },
        { 1, 1, 0, 0, 0, 1, 1, 1, 1 },   { -1, 1, 0, 0, 0, 1, 1, 1, 1 },
    };
    const bool drawn = GpuDrawTriangles(&c->gpu, &c->vertex, &c->lod, full, 6);
    GpuSubmit(&c->gpu);
    const uint8_t *const pixels = drawn && c->gpu.ready ? GpuReadBack(&c->gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure
                                             : "the list or the readback did not happen");
        return;
    }
    uint32_t wrong = 0;
    uint32_t first = 0;
    float got_first = 0.0f;
    float want_first = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        float got;
        memcpy(&got, pixels + 4u * i, 4);
        const float want = LookupExpected(&lookups[i], sampler);
        const bool right = sampler == SAMPLER_LINEAR_MIN_125 ? CheckFloatUlp(got, want, 0.0f)
                                                              : memcmp(&got, &want, 4) == 0;
        if (!right && wrong++ == 0u) {
            first = i;
            got_first = got;
            want_first = want;
        }
    }
    // A value names its level (the thousands) and its texel (16 y + x), which the detail spells
    // out for the first wrong lookup.
    const int32_t got_level = (int32_t)(got_first / 1000.0f);
    const int32_t got_rest = (int32_t)(got_first - 1000.0f * (float)got_level);
    CHECK_TRUE(SUITE, name, sampler == SAMPLER_LINEAR_MIN_125 ? CHECK_ULP : CHECK_EXACT,
               wrong == 0u,
               "%u of %u lookups wrong; lookup %u read %.2f (level %d, texel %d,%d), wanted %.2f",
               (unsigned)wrong, (unsigned)count, (unsigned)first, (double)got_first,
               (int)got_level, (int)(got_rest % 16), (int)(got_rest / 16), (double)want_first);
}

/// Four lookups at lambdas 0 to 3 along one of the four gradient components: @p component 0 and
/// 2 are along u (the width), 1 and 3 along v (the height).
static void FourLevels(Lookup out[4], uint32_t mode, uint32_t component) {
    const float size = component % 2u == 0u ? (float)LOD_WIDTH : (float)LOD_HEIGHT;
    for (uint32_t k = 0; k < 4u; ++k) {
        memset(&out[k], 0, sizeof(out[k]));
        out[k].mode = mode;
        out[k].grad[component] = (float)(1u << k) / size;
    }
}

static bool Upload(LodContext *c, DkImage *image) {
    DkCopyBuf staged[LOD_LEVELS];
    for (uint32_t level = 0; level < LOD_LEVELS; ++level) {
        const uint32_t w = LodLevelSize(LOD_WIDTH, level);
        const uint32_t h = LodLevelSize(LOD_HEIGHT, level);
        float texels[LOD_WIDTH * LOD_HEIGHT];
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                texels[y * w + x] = LodTexelValue(level, x, y);
            }
        }
        if (!GpuStagePixels(&c->gpu, w, h, texels, &staged[level])) {
            return false;
        }
    }
    dkCmdBufClear(c->gpu.cmdbuf);
    dkCmdBufBarrier(c->gpu.cmdbuf, DkBarrier_Full, 0);
    for (uint32_t level = 0; level < LOD_LEVELS; ++level) {
        DkImageView view;
        dkImageViewDefaults(&view, image);
        view.mipLevelOffset = (uint8_t)level;
        view.mipLevelCount = 1;
        const DkImageRect rect = { 0, 0, 0, LodLevelSize(LOD_WIDTH, level),
                                   LodLevelSize(LOD_HEIGHT, level), 1 };
        dkCmdBufCopyBufferToImage(c->gpu.cmdbuf, &staged[level], &view, &rect, 0);
    }
    GpuSubmit(&c->gpu);
    return c->gpu.ready;
}

void RunLod(void) {
    if (!ReportStart(SUITE, "graphics device opens with a float target")) {
        return;
    }
    LodContext c;
    memset(&c, 0, sizeof(c));
    GpuSpec spec;
    GpuSpecDefaults(&spec, LOOKUPS, 1);
    spec.color_format = DkImageFormat_R32_Float;
    spec.code_size = 32u * 1024u;
    spec.data_size = 256u * 1024u;
    spec.image_size = 256u * 1024u;
    const bool opened = GpuOpenSpec(&c.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with a float target", CHECK_NONE, opened, "%s",
               c.gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the shaders load")) {
        GpuClose(&c.gpu);
        return;
    }
    const bool loaded = GpuLoadShader(&c.gpu, &c.vertex, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&c.gpu, &c.lod, ShaderData(SHADER_LOD_FRAG),
                                      ShaderSize(SHADER_LOD_FRAG));
    CHECK_TRUE(SUITE, "the shaders load", CHECK_NONE, loaded, "%s", c.gpu.failure);
    if (!loaded) {
        GpuClose(&c.gpu);
        return;
    }
    if (!ReportStart(SUITE, "the texture's five levels and the samplers are in place")) {
        GpuClose(&c.gpu);
        return;
    }
    DkImage image;
    DkImageLayout layout;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &c.gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &c.image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        &c.gpu, (uint32_t)(SAMPLERS * sizeof(DkSamplerDescriptor)),
        DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &c.sampler_set);
    bool ready = images != NULL && samplers != NULL &&
                 GpuCreateMipTexture(&c.gpu, &image, &layout, DkImageFormat_R32_Float, LOD_WIDTH,
                                     LOD_HEIGHT, LOD_LEVELS) &&
                 Upload(&c, &image);
    if (ready) {
        DkImageView view;
        dkImageViewDefaults(&view, &image);
        dkImageDescriptorInitialize(&images[0], &view, false, false);
        for (uint32_t s = 0; s < SAMPLERS; ++s) {
            DkSampler sampler;
            dkSamplerDefaults(&sampler);
            sampler.minFilter = DkFilter_Nearest;
            sampler.magFilter = DkFilter_Nearest;
            sampler.mipFilter = s == SAMPLER_LINEAR_MIN_125 ? DkMipFilter_Linear
                                                            : DkMipFilter_Nearest;
            sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
            sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
            sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
            sampler.lodClampMin = s == SAMPLER_MIN_2 ? 2.0f
                                  : s == SAMPLER_LINEAR_MIN_125 ? 1.25f
                                                                : 0.0f;
            sampler.lodClampMax = s == SAMPLER_MAX_1 ? 1.0f : 1000.0f;
            sampler.lodBias = s == SAMPLER_BIAS_1 ? 1.0f : 0.0f;
            dkSamplerDescriptorInitialize(&samplers[s], &sampler);
        }
    }
    CHECK_TRUE(SUITE, "the texture's five levels and the samplers are in place", CHECK_NONE, ready,
               "%s", c.gpu.failure[0] != '\0' ? c.gpu.failure : "the scratch block had no room");
    if (!ready) {
        GpuClose(&c.gpu);
        return;
    }

    Lookup l[8];
    FourLevels(l, MODE_GRAD, 0);
    LookupCase(&c, "textureGrad along x picks levels 0 to 3", SAMPLER_PLAIN, l, 4);
    FourLevels(l, MODE_GRAD, 1);
    LookupCase(&c, "textureGrad along y counts the height's texels", SAMPLER_PLAIN, l, 4);
    FourLevels(l, MODE_GRAD, 2);
    FourLevels(l + 4, MODE_GRAD, 3);
    LookupCase(&c, "the y derivative picks the level as the x derivative does", SAMPLER_PLAIN, l,
               8);
    {
        // One texel along x and four along y, four along x and one along y, eight and two.
        const Lookup larger[3] = {
            { MODE_GRAD, { 1.0f / LOD_WIDTH, 0, 0, 4.0f / LOD_HEIGHT }, 0 },
            { MODE_GRAD, { 4.0f / LOD_WIDTH, 0, 0, 1.0f / LOD_HEIGHT }, 0 },
            { MODE_GRAD, { 0, 8.0f / LOD_HEIGHT, 2.0f / LOD_WIDTH, 0 }, 0 },
        };
        LookupCase(&c, "the longer of the two derivatives decides", SAMPLER_PLAIN, larger, 3);
    }
    FourLevels(l, MODE_GRAD_OFFSET, 0);
    LookupCase(&c, "textureGradOffset counts its offset in the picked level's texels",
               SAMPLER_PLAIN, l, 4);
    {
        Lookup levels[5];
        Lookup offsets[4];
        for (uint32_t k = 0; k < 5u; ++k) {
            levels[k] = (Lookup){ MODE_LOD, { 0, 0, 0, 0 }, (float)k };
            if (k < 4u) {
                offsets[k] = (Lookup){ MODE_LOD_OFFSET, { 0, 0, 0, 0 }, (float)k };
            }
        }
        LookupCase(&c, "textureLod picks the level it names", SAMPLER_PLAIN, levels, 5);
        LookupCase(&c, "textureLodOffset counts its offset in that level's texels", SAMPLER_PLAIN,
                   offsets, 4);
        FourLevels(l, MODE_GRAD, 0);
        LookupCase(&c, "the sampler's minimum level of detail lifts a gradient's level",
                   SAMPLER_MIN_2, l, 4);
        Lookup capped[5];
        FourLevels(capped, MODE_GRAD, 0);
        capped[4] = levels[3];
        LookupCase(&c, "the sampler's maximum level of detail caps gradients and explicit levels",
                   SAMPLER_MAX_1, capped, 5);
        LookupCase(&c, "the sampler's bias adds to a gradient's level", SAMPLER_BIAS_1, l, 4);
        LookupCase(&c, "a minimum of 1.25 with linear mip filtering blends levels 1 and 2",
                   SAMPLER_LINEAR_MIN_125, l, 1);
    }
    GpuClose(&c.gpu);
}
