// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../rawref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// DRAW_TEXTURE, the console graphics API's DrawTexture: a rectangle of a texture copied onto the
// render target by one method, between ordinary draws. The library has no call for it, so it goes
// in as raw words (rawref.h, RawDrawTexture). An emulator that draws it with a pipeline, a
// viewport and a scissor of its own has to put the guest's back for the draw after it: one with
// the same pipeline as the draw before, and one with another pipeline, each over the bottom
// quarter, which a draw still in the copy's pipeline or viewport never reaches.
//
// A list is: the left half in colour A (the flat pipeline, colour from the vertices), the 16 by
// 16 texture of colour B copied onto x 40 to 56, y 24 to 40, then the bottom quarter in C with
// the same flat pipeline, or in D with the uniform one (colour from a uniform buffer). The copy's
// rows are the same whichever way round the rows are counted, so the window origin decides
// nothing here.

static const char *const SUITE = "drawtex";

#define TARGET_SIZE 64u
#define TEXTURE_SIZE 16u
#define RECT_X 40u
#define RECT_Y 24u
#define BOTTOM_Y 48u

static const uint8_t COLOUR_A[4] = { 0x30, 0x60, 0x90, 0xFF };
static const uint8_t COLOUR_B[4] = { 0xC0, 0x20, 0x40, 0xFF };
static const uint8_t COLOUR_C[4] = { 0x10, 0xE0, 0x70, 0xFF };
static const uint8_t COLOUR_D[4] = { 0xF0, 0xB0, 0x08, 0xFF };
static const uint8_t CLEARED[4] = { 0, 0, 0, 0 };

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader flat;
    DkShader uniform;
    DkGpuAddr image_set;
    DkGpuAddr sampler_set;
    DkGpuAddr colour_d;
} DrawtexContext;

/// Two triangles over pixels [x0, x1) by [y0, y1), rows counted from the top as the other
/// suites count them, in one flat colour.
static void PixelQuad(GpuVertex out[6], uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                      const uint8_t colour[4]) {
    const float left = -1.0f + 2.0f * (float)x0 / (float)TARGET_SIZE;
    const float right = -1.0f + 2.0f * (float)x1 / (float)TARGET_SIZE;
    const float top = 1.0f - 2.0f * (float)y0 / (float)TARGET_SIZE;
    const float bottom = 1.0f - 2.0f * (float)y1 / (float)TARGET_SIZE;
    const float corners[6][2] = { { left, bottom }, { right, bottom }, { right, top },
                                  { left, bottom }, { right, top },    { left, top } };
    for (uint32_t i = 0; i < 6u; ++i) {
        out[i] = (GpuVertex){ corners[i][0],         corners[i][1],         0.0f,
                              0.0f,                  0.0f,                  colour[0] / 255.0f,
                              colour[1] / 255.0f,    colour[2] / 255.0f,    colour[3] / 255.0f };
    }
}

/// What a pixel holds once the list is done, with @p last the colour of the bottom quarter.
static const uint8_t *Expected(uint32_t x, uint32_t y, const uint8_t last[4]) {
    if (y >= BOTTOM_Y) {
        return last;
    }
    if (x >= RECT_X && x < RECT_X + TEXTURE_SIZE && y >= RECT_Y && y < RECT_Y + TEXTURE_SIZE) {
        return COLOUR_B;
    }
    return x < TARGET_SIZE / 2u ? COLOUR_A : CLEARED;
}

/// Records and runs one list: A, the copy, then the bottom quarter with @p uniform_after (colour
/// D from the uniform buffer) or with the flat pipeline again (colour C).
static bool RunList(DrawtexContext *c, bool uniform_after) {
    GpuClear(&c->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&c->gpu);
    dkCmdBufBindImageDescriptorSet(c->gpu.cmdbuf, c->image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(c->gpu.cmdbuf, c->sampler_set, 1);
    GpuVertex quad[6];
    PixelQuad(quad, 0, 0, TARGET_SIZE / 2u, TARGET_SIZE, COLOUR_A);
    bool ok = GpuDrawTriangles(&c->gpu, &c->vertex, &c->flat, quad, 6);
    uint32_t words[13];
    const size_t count =
        RawDrawTexture(words, 0, 0, RECT_X, RECT_Y, TEXTURE_SIZE, TEXTURE_SIZE, 0, 0);
    ok = ok && GpuRawMethods(&c->gpu, words, (uint32_t)count);
    PixelQuad(quad, 0, BOTTOM_Y, TARGET_SIZE, TARGET_SIZE, uniform_after ? COLOUR_D : COLOUR_C);
    if (uniform_after) {
        dkCmdBufBindUniformBuffer(c->gpu.cmdbuf, DkStage_Fragment, 0, c->colour_d,
                                  DK_UNIFORM_BUF_ALIGNMENT);
    }
    ok = ok && GpuDrawTriangles(&c->gpu, &c->vertex, uniform_after ? &c->uniform : &c->flat,
                                quad, 6);
    GpuSubmit(&c->gpu);
    return ok && c->gpu.ready;
}

/// Judges the pixels of @p pixels for which @p region says yes against Expected.
static void Judge(const uint8_t *pixels, const char *name, const uint8_t last[4],
                  bool (*region)(uint32_t x, uint32_t y)) {
    Picture *const picture = PictureBegin(TARGET_SIZE, TARGET_SIZE, PICTURE_COLOUR, pixels);
    uint32_t judged = 0;
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    uint8_t want[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_SIZE; ++y) {
        for (uint32_t x = 0; x < TARGET_SIZE; ++x) {
            const uint8_t *const expect = Expected(x, y, last);
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_SIZE, x, y);
            const bool mine = region(x, y);
            const bool right = memcmp(pixel, expect, 4) == 0;
            PictureSet(picture, x, y, expect,
                       !mine ? PICTURE_UNJUDGED : right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!mine) {
                continue;
            }
            ++judged;
            if (!right) {
                if (wrong == 0) {
                    first_x = x;
                    first_y = y;
                    memcpy(seen, pixel, 4);
                    memcpy(want, expect, 4);
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong; (%u, %u) holds %02x%02x%02x%02x, wanted %02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)judged, (unsigned)first_x, (unsigned)first_y, seen[0],
               seen[1], seen[2], seen[3], want[0], want[1], want[2], want[3]);
}

static bool InRect(uint32_t x, uint32_t y) {
    return x >= RECT_X && x < RECT_X + TEXTURE_SIZE && y >= RECT_Y && y < RECT_Y + TEXTURE_SIZE;
}

static bool Anywhere(uint32_t x, uint32_t y) {
    (void)x;
    (void)y;
    return true;
}

static void ListCase(DrawtexContext *c, bool uniform_after, const char *rect_name,
                     const char *name) {
    const uint8_t *const last = uniform_after ? COLOUR_D : COLOUR_C;
    const bool ran = RunList(c, uniform_after);
    const uint8_t *const pixels = ran ? GpuReadBack(&c->gpu) : NULL;
    if (pixels == NULL) {
        const char *const why =
            c->gpu.failure[0] != '\0' ? c->gpu.failure : "the list or the readback did not happen";
        if (rect_name != NULL) {
            ReportCase(SUITE, rect_name, CHECK_NONE, false, why);
        }
        ReportCase(SUITE, name, CHECK_NONE, false, why);
        return;
    }
    if (rect_name != NULL) {
        Judge(pixels, rect_name, last, InRect);
    }
    Judge(pixels, name, last, Anywhere);
}

void RunDrawtex(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_SIZE, TARGET_SIZE);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.image_size = 64u * 1024u;

    DrawtexContext c;
    const bool opened = GpuOpenSpec(&c.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for a texture", CHECK_NONE, opened, "%s",
               c.gpu.failure);
    if (!opened) {
        return;
    }
    const bool loaded = GpuLoadShader(&c.gpu, &c.vertex, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&c.gpu, &c.flat, ShaderData(SHADER_FLAT_FRAG),
                                      ShaderSize(SHADER_FLAT_FRAG)) &&
                        GpuLoadShader(&c.gpu, &c.uniform, ShaderData(SHADER_UNIFORM_FRAG),
                                      ShaderSize(SHADER_UNIFORM_FRAG));
    CHECK_TRUE(SUITE, "the drawing shaders load", CHECK_NONE, loaded, "%s", c.gpu.failure);
    if (!loaded) {
        GpuClose(&c.gpu);
        return;
    }

    DkImage texture;
    DkImageLayout layout;
    uint8_t texels[TEXTURE_SIZE * TEXTURE_SIZE][4];
    for (uint32_t i = 0; i < TEXTURE_SIZE * TEXTURE_SIZE; ++i) {
        memcpy(texels[i], COLOUR_B, 4);
    }
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &c.gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &c.image_set);
    DkSamplerDescriptor *const samplers =
        (DkSamplerDescriptor *)GpuAllocData(&c.gpu, (uint32_t)sizeof(DkSamplerDescriptor),
                                            DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &c.sampler_set);
    float *const colour_d = (float *)GpuAllocData(&c.gpu, DK_UNIFORM_BUF_ALIGNMENT,
                                                  DK_UNIFORM_BUF_ALIGNMENT, &c.colour_d);
    bool ready = images != NULL && samplers != NULL && colour_d != NULL &&
                 GpuCreateTexture(&c.gpu, &texture, &layout, DkImageFormat_RGBA8_Unorm,
                                  TEXTURE_SIZE, TEXTURE_SIZE) &&
                 GpuUploadTexture(&c.gpu, &texture, DkImageFormat_RGBA8_Unorm, TEXTURE_SIZE,
                                  TEXTURE_SIZE, texels);
    if (ready) {
        DkImageView view;
        dkImageViewDefaults(&view, &texture);
        dkImageDescriptorInitialize(&images[0], &view, false, false);
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = DkFilter_Nearest;
        sampler.magFilter = DkFilter_Nearest;
        sampler.mipFilter = DkMipFilter_None;
        dkSamplerDescriptorInitialize(&samplers[0], &sampler);
        memset(colour_d, 0, DK_UNIFORM_BUF_ALIGNMENT);
        for (uint32_t i = 0; i < 4u; ++i) {
            colour_d[i] = COLOUR_D[i] / 255.0f;
        }
    }
    CHECK_TRUE(SUITE, "the texture, its descriptors and the uniform colour are in place",
               CHECK_NONE, ready, "%s",
               c.gpu.failure[0] != '\0' ? c.gpu.failure : "the scratch block had no room");
    if (!ready) {
        GpuClose(&c.gpu);
        return;
    }

    ListCase(&c, false, "a draw texture copies its texture onto its rectangle",
             "a draw after a draw texture binds the pipeline the draw before it bound");
    ListCase(&c, true, NULL,
             "a draw with another pipeline after a draw texture sets its own viewport");
    GpuClose(&c.gpu);
}
