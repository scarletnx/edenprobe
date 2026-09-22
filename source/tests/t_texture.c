// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "../texref.h"
#include "tests.h"

// Texture sampling, with every pixel of the target landing on a texel this file can name.
//
// A 4 by 4 texture over a 64 by 64 target, with the quads laid out so no pixel centre falls near
// a texel boundary. Nothing is close to a decision, which is what lets the nearest cases claim
// the exact tier. The wrap modes are asked with a quad three copies of the texture wide and
// tall, so each has to fold the same out of range coordinates its own way.
//
// The linear case is the delicate one. One texel per pixel, offset by half a pixel, so the only
// weight the filter should use is one half, and the texels are paired to average to a whole
// byte. That alone is not enough: the hardware quantises the coordinate onto a sub texel grid,
// so the weight can be off by a fraction of a step and the answer moves by that much of the
// difference between two texels. The row below keeps every neighbouring difference small enough
// that no byte can move, wrap around pairs included, and the suite checks that rather than
// trusting this paragraph.

static const char *const SUITE = "texture";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)
#define TEXTURE_SIZE 4u

// Sixteen texels, all different: red counts across and green counts down, so a texture stored
// transposed or sampled with its axes swapped says which way round it went, and blue counts
// through all sixteen so no two share it.
static const uint8_t NEAREST_TEXELS[TEXTURE_SIZE * TEXTURE_SIZE][4] = {
    { 0x10, 0x08, 0x00, 0xFF }, { 0x30, 0x08, 0x11, 0xFF },
    { 0x50, 0x08, 0x22, 0xFF }, { 0x70, 0x08, 0x33, 0xFF },
    { 0x10, 0x48, 0x44, 0xFF }, { 0x30, 0x48, 0x55, 0xFF },
    { 0x50, 0x48, 0x66, 0xFF }, { 0x70, 0x48, 0x77, 0xFF },
    { 0x10, 0x88, 0x88, 0xFF }, { 0x30, 0x88, 0x99, 0xFF },
    { 0x50, 0x88, 0xAA, 0xFF }, { 0x70, 0x88, 0xBB, 0xFF },
    { 0x10, 0xC8, 0xCC, 0xFF }, { 0x30, 0xC8, 0xDD, 0xFF },
    { 0x50, 0xC8, 0xEE, 0xFF }, { 0x70, 0xC8, 0xFF, 0xFF },
};

// The same four texels on every row, so the vertical half of the filter blends two identical
// rows and contributes nothing. Every value is even, which keeps the average of two neighbours a
// whole byte, and every step is small enough that the filter's own precision cannot move it. The
// four channels are the same shape at four phases, so no two columns share an answer.
#define LINEAR_LOW 0x30u
#define LINEAR_MID 0x60u
#define LINEAR_HIGH 0x90u
// The largest difference between two texels this case ever blends, wrap around included.
#define LINEAR_NEIGHBOUR_STEP 0x30u
static const uint8_t LINEAR_ROW[TEXTURE_SIZE][4] = {
    { LINEAR_LOW, LINEAR_HIGH, LINEAR_MID, LINEAR_MID },
    { LINEAR_MID, LINEAR_MID, LINEAR_HIGH, LINEAR_LOW },
    { LINEAR_HIGH, LINEAR_LOW, LINEAR_MID, LINEAR_MID },
    { LINEAR_MID, LINEAR_MID, LINEAR_LOW, LINEAR_HIGH },
};

/// What a case expects, in the terms source/texref.c speaks.
typedef struct {
    /// The sixteen texels, row major.
    const uint8_t (*texels)[4];
    TexWrap wrap;
    bool linear;
    double first_u;
    double last_u;
    double first_v;
    double last_v;
} TextureExpectation;

static GpuVertex Corner(float x, float y, float u, float v) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = 0.0f;
    vertex.u = u;
    vertex.v = v;
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;
    return vertex;
}

/// A quad over the whole target carrying the coordinate span the case wants.
///
/// The corners are in the same order as the target's, so the left edge of the quad carries the
/// first coordinate and the right edge the last one. Clip space plus y is the top of the target,
/// which is why the two corners carrying the first vertical coordinate are the ones with y of
/// plus one.
static void CoordinateQuad(GpuVertex out[6], const TextureExpectation *want) {
    const float u0 = (float)want->first_u;
    const float u1 = (float)want->last_u;
    const float v0 = (float)want->first_v;
    const float v1 = (float)want->last_v;
    const GpuVertex top_left = Corner(-1.0f, 1.0f, u0, v0);
    const GpuVertex top_right = Corner(1.0f, 1.0f, u1, v0);
    const GpuVertex bottom_right = Corner(1.0f, -1.0f, u1, v1);
    const GpuVertex bottom_left = Corner(-1.0f, -1.0f, u0, v1);
    out[0] = top_left;
    out[1] = top_right;
    out[2] = bottom_right;
    out[3] = top_left;
    out[4] = bottom_right;
    out[5] = bottom_left;
}

/// The four bytes the pixel at @p x, @p y should hold.
static void ExpectedPixel(const TextureExpectation *want, uint32_t x, uint32_t y,
                          uint8_t out[4]) {
    const double u =
        TexCoordInTexels(want->first_u, want->last_u, TARGET_WIDTH, x, TEXTURE_SIZE);
    const double v =
        TexCoordInTexels(want->first_v, want->last_v, TARGET_HEIGHT, y, TEXTURE_SIZE);

    if (!want->linear) {
        const int32_t tx = TexNearestTexel(want->wrap, u, (int32_t)TEXTURE_SIZE);
        const int32_t ty = TexNearestTexel(want->wrap, v, (int32_t)TEXTURE_SIZE);
        memcpy(out, want->texels[(size_t)ty * TEXTURE_SIZE + (size_t)tx], 4);
        return;
    }

    // Only the horizontal pair matters: the texture this case uses has four identical rows, so
    // whichever two of them the vertical half of the filter picks, and with whatever weight, the
    // two are the same and the blend between them is that value again.
    int32_t pair[2];
    double weight;
    TexLinearTexels(want->wrap, u, (int32_t)TEXTURE_SIZE, pair, &weight);
    for (int channel = 0; channel < 4; ++channel) {
        const uint32_t a = want->texels[pair[0]][channel];
        const uint32_t b = want->texels[pair[1]][channel];
        out[channel] = (uint8_t)((a + b) / 2u);
    }
}

static bool CheckAgainstTexels(const uint8_t *pixels, const TextureExpectation *want,
                               Picture *picture, char *detail, size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t first[4] = { 0, 0, 0, 0 };
    uint8_t first_expected[4] = { 0, 0, 0, 0 };

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            uint8_t expected[4];
            ExpectedPixel(want, x, y, expected);
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            const bool right = memcmp(pixel, expected, 4) == 0;
            PictureSet(picture, x, y, expected, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (right) {
                continue;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                memcpy(first, pixel, sizeof(first));
                memcpy(first_expected, expected, sizeof(first_expected));
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        detail[0] = '\0';
        return true;
    }
    snprintf(detail, detail_size,
             "%u of %u pixels wrong, first at (%u,%u) is %02x%02x%02x%02x, expected "
             "%02x%02x%02x%02x",
             (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)first_x, (unsigned)first_y,
             first[0], first[1], first[2], first[3], first_expected[0], first_expected[1],
             first_expected[2], first_expected[3]);
    return false;
}

static void SampleCase(GpuContext *gpu, const DkShader *vertex_shader,
                       const DkShader *fragment_shader, const char *name, DkGpuAddr image_set,
                       uint32_t image_count, DkGpuAddr sampler_set, uint32_t sampler_count,
                       uint32_t image_index, uint32_t sampler_index,
                       const TextureExpectation *want) {
    // Cleared to something no texel holds, so a pixel the quad missed is not mistaken for a
    // texel that happened to match.
    GpuClear(gpu, 1.0f, 0.0f, 1.0f, 0.0f);

    GpuVertex vertices[6];
    CoordinateQuad(vertices, want);

    GpuBeginPass(gpu);
    dkCmdBufBindImageDescriptorSet(gpu->cmdbuf, image_set, image_count);
    dkCmdBufBindSamplerDescriptorSet(gpu->cmdbuf, sampler_set, sampler_count);
    dkCmdBufBindTexture(gpu->cmdbuf, DkStage_Fragment, 0,
                        dkMakeTextureHandle(image_index, sampler_index));

    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, 6);
    GpuSubmit(gpu);

    if (!drawn) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw was not recorded");
        return;
    }

    const uint8_t *const pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure
                                            : "the readback handed back nothing");
        return;
    }

    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);

    char detail[320];
    const bool matched = CheckAgainstTexels(pixels, want, picture, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, CHECK_EXACT, matched, "%s", detail);
}

/// Fills a sampler descriptor with one filter and one wrap mode.
static void MakeSampler(DkSamplerDescriptor *out, DkFilter filter, DkWrapMode wrap) {
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    // Both filters the same, so which of the two the hardware picks from the level of detail is
    // one fewer thing a failing case could be about.
    sampler.minFilter = filter;
    sampler.magFilter = filter;
    sampler.mipFilter = DkMipFilter_None;
    sampler.wrapMode[0] = wrap;
    sampler.wrapMode[1] = wrap;
    sampler.wrapMode[2] = wrap;
    dkSamplerDescriptorInitialize(out, &sampler);
}

void RunTexture(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 16u * 1024u;
    spec.data_size = 64u * 1024u;
    // Two small textures, and block linear storage rounds each of them up to a whole tile, so
    // the number here is nothing like four by four by four bytes.
    spec.image_size = 64u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for textures", CHECK_NONE, opened, "%s",
               gpu.failure);
    if (!opened) {
        return;
    }

    DkShader vertex_shader;
    DkShader fragment_shader;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_TEXTURE_FRAG),
                                      ShaderSize(SHADER_TEXTURE_FRAG));
    CHECK_TRUE(SUITE, "the sampling shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }

    // The four identical rows the linear case needs, built from the one row above. Copied texel
    // by texel rather than a row at a time: a row is four separate four byte texels in this
    // array, and one copy across all four would be reaching past the end of the first of them.
    uint8_t linear_texels[TEXTURE_SIZE * TEXTURE_SIZE][4];
    for (uint32_t row = 0; row < TEXTURE_SIZE; ++row) {
        for (uint32_t texel = 0; texel < TEXTURE_SIZE; ++texel) {
            for (uint32_t channel = 0; channel < 4u; ++channel) {
                linear_texels[row * TEXTURE_SIZE + texel][channel] = LINEAR_ROW[texel][channel];
            }
        }
    }

    // The two claims the linear case rests on, checked where the data is rather than asserted:
    // every neighbouring pair adds to an even number, and none differs by more than the step
    // above. The wrap around pair is in the sweep on purpose, since leaving it out is how a row
    // that looks safe stops being safe.
    uint32_t odd_sums = 0;
    uint32_t too_far = 0;
    uint32_t widest = 0;
    for (uint32_t texel = 0; texel < TEXTURE_SIZE; ++texel) {
        const uint32_t next = (texel + 1u) % TEXTURE_SIZE;
        for (int channel = 0; channel < 4; ++channel) {
            const uint32_t a = LINEAR_ROW[texel][channel];
            const uint32_t b = LINEAR_ROW[next][channel];
            if ((a + b) % 2u != 0u) {
                ++odd_sums;
            }
            const uint32_t difference = a > b ? a - b : b - a;
            if (difference > widest) {
                widest = difference;
            }
            if (difference > LINEAR_NEIGHBOUR_STEP) {
                ++too_far;
            }
        }
    }
    CHECK_TRUE(SUITE, "every neighbouring pair of texels averages to a whole byte", CHECK_NONE,
               odd_sums == 0, "%u of the 16 neighbouring channel pairs add to an odd number",
               (unsigned)odd_sums);
    CHECK_TRUE(SUITE, "no pair of texels is far enough apart for a filter weight to matter",
               CHECK_NONE, too_far == 0,
               "the widest neighbouring pair differs by %u, which a weight off by one in 256 "
               "would move by %u parts in 256 of a byte; half a byte changes the answer",
               (unsigned)widest, (unsigned)widest);

    DkImage nearest_image;
    DkImageLayout nearest_layout;
    DkImage linear_image;
    DkImageLayout linear_layout;
    const bool created =
        GpuCreateTexture(&gpu, &nearest_image, &nearest_layout, DkImageFormat_RGBA8_Unorm,
                         TEXTURE_SIZE, TEXTURE_SIZE) &&
        GpuCreateTexture(&gpu, &linear_image, &linear_layout, DkImageFormat_RGBA8_Unorm,
                         TEXTURE_SIZE, TEXTURE_SIZE);
    CHECK_TRUE(SUITE, "the textures have somewhere to live", CHECK_NONE, created, "%s",
               gpu.failure);
    if (!created) {
        GpuClose(&gpu);
        return;
    }

    const bool uploaded =
        GpuUploadTexture(&gpu, &nearest_image, DkImageFormat_RGBA8_Unorm, TEXTURE_SIZE,
                         TEXTURE_SIZE, NEAREST_TEXELS) &&
        GpuUploadTexture(&gpu, &linear_image, DkImageFormat_RGBA8_Unorm, TEXTURE_SIZE,
                         TEXTURE_SIZE, linear_texels);
    // Reported rather than assumed: an upload that did nothing and a sampler reading the wrong
    // texels look identical in every case below this line.
    CHECK_TRUE(SUITE, "the texels reach the textures", CHECK_NONE, uploaded, "%s", gpu.failure);
    if (!uploaded) {
        GpuClose(&gpu);
        return;
    }

    // Arrays the hardware indexes into with the handle a draw carries, so they are allocated
    // once and bound unchanged by every case. The barrier at the top of every pass is what
    // drops the caches that might still hold their previous contents.
    DkGpuAddr image_set = 0;
    DkGpuAddr sampler_set = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &gpu, 2u * (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT,
        &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        &gpu, 4u * (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
        &sampler_set);
    CHECK_TRUE(SUITE, "the descriptor sets have somewhere to live", CHECK_NONE,
               images != NULL && samplers != NULL,
               "the scratch block had no room for the descriptor sets");
    if (images == NULL || samplers == NULL) {
        GpuClose(&gpu);
        return;
    }

    {
        DkImageView view;
        dkImageViewDefaults(&view, &nearest_image);
        dkImageDescriptorInitialize(&images[0], &view, false, false);
        dkImageViewDefaults(&view, &linear_image);
        dkImageDescriptorInitialize(&images[1], &view, false, false);
    }
    MakeSampler(&samplers[0], DkFilter_Nearest, DkWrapMode_Repeat);
    MakeSampler(&samplers[1], DkFilter_Nearest, DkWrapMode_ClampToEdge);
    MakeSampler(&samplers[2], DkFilter_Nearest, DkWrapMode_MirroredRepeat);
    MakeSampler(&samplers[3], DkFilter_Linear, DkWrapMode_Repeat);

    ReportNote("texture %ux%u RGBA8_Unorm onto %ux%u, %u pixels per texel", TEXTURE_SIZE,
               TEXTURE_SIZE, TARGET_WIDTH, TARGET_HEIGHT,
               (TARGET_WIDTH / TEXTURE_SIZE) * (TARGET_HEIGHT / TEXTURE_SIZE));

    // Inside the texture, where no wrap mode has anything to do. Sixteen by sixteen pixels of
    // target per texel, and the case names every one of them.
    TextureExpectation want;
    want.texels = NEAREST_TEXELS;
    want.wrap = TEXWRAP_CLAMP_TO_EDGE;
    want.linear = false;
    want.first_u = 0.0;
    want.last_u = 1.0;
    want.first_v = 0.0;
    want.last_v = 1.0;
    SampleCase(&gpu, &vertex_shader, &fragment_shader,
               "nearest sampling inside the texture picks the texel under the pixel", image_set,
               2, sampler_set, 4, 0, 1, &want);

    // Three copies wide and three tall, so two thirds of each axis is outside the texture and
    // each wrap mode has to fold it back its own way.
    want.first_u = -1.0;
    want.last_u = 2.0;
    want.first_v = -1.0;
    want.last_v = 2.0;

    want.wrap = TEXWRAP_REPEAT;
    SampleCase(&gpu, &vertex_shader, &fragment_shader, "repeating tiles the texture", image_set,
               2, sampler_set, 4, 0, 0, &want);

    want.wrap = TEXWRAP_CLAMP_TO_EDGE;
    SampleCase(&gpu, &vertex_shader, &fragment_shader,
               "clamping to the edge stretches the border texels", image_set, 2, sampler_set, 4,
               0, 1, &want);

    want.wrap = TEXWRAP_MIRRORED_REPEAT;
    SampleCase(&gpu, &vertex_shader, &fragment_shader,
               "mirroring tiles the texture with no seam", image_set, 2, sampler_set, 4, 0, 2,
               &want);

    // One texel per pixel, offset by half a pixel, so every pixel centre falls exactly between
    // two texel centres and the only weight the filter can use is one half.
    want.texels = (const uint8_t (*)[4])linear_texels;
    want.wrap = TEXWRAP_REPEAT;
    want.linear = true;
    want.first_u = -0.125;
    want.last_u = 15.875;
    want.first_v = 0.5;
    want.last_v = 0.5;
    SampleCase(&gpu, &vertex_shader, &fragment_shader,
               "linear sampling halfway between two texels is their average", image_set, 2,
               sampler_set, 4, 1, 3, &want);

    GpuClose(&gpu);
}
