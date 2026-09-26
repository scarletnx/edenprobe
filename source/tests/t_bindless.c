// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// A fragment shader that picks one of 256 textures by a handle read from a uniform array at a
// dynamic index, the way games index texture handle tables. The console fetches through any
// handle in the image descriptor set; an emulator that turns the array into a fixed number of
// host descriptors has to make room for every index the shader can reach.
//
// Each texture is one texel, written by the processor straight into image memory, with four bytes
// derived from its own index, so column i of the target must hold texture i's texel.

static const char *const SUITE = "bindless";

#define TEXTURES 256u
#define TARGET_WIDTH TEXTURES
#define TARGET_HEIGHT 4u
#define HANDLES_SIZE (TEXTURES * 16u)

/// Texture @p i's one texel: every byte different between neighbours and none nought.
static void Texel(uint32_t i, uint8_t out[4]) {
    out[0] = (uint8_t)(i + 1u);
    out[1] = (uint8_t)(0xFFu - i);
    out[2] = (uint8_t)((i * 37u + 11u) & 0xFFu);
    out[3] = 0xFFu;
}

static void Judge(GpuContext *gpu, const uint8_t *pixels, const char *name, uint32_t first,
                  uint32_t last) {
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t first_wrong = 0;
    int32_t looks_like = -1;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            uint8_t want[4];
            Texel(x, want);
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            const bool judged = x >= first && x <= last;
            const bool right = memcmp(pixel, want, 4) == 0;
            PictureSet(picture, x, y, want,
                       !judged ? PICTURE_UNJUDGED : right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!judged || right) {
                continue;
            }
            if (wrong == 0) {
                first_wrong = x;
                memcpy(seen, pixel, 4);
                for (uint32_t t = 0; t < TEXTURES; ++t) {
                    uint8_t other[4];
                    Texel(t, other);
                    if (memcmp(pixel, other, 4) == 0) {
                        looks_like = (int32_t)t;
                        break;
                    }
                }
            }
            ++wrong;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong; column %u holds %02x%02x%02x%02x, texture %d's texel "
               "(-1: none of the 256)",
               (unsigned)wrong, (unsigned)((last - first + 1u) * TARGET_HEIGHT),
               (unsigned)first_wrong, seen[0], seen[1], seen[2], seen[3], (int)looks_like);
    (void)gpu;
}

void RunBindless(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = TEXTURES * 4096u;

    if (!ReportStart(SUITE, "graphics device opens with room for 256 textures")) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for 256 textures", CHECK_NONE, opened,
               "%s", gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the bindless sampling shader loads")) {
        GpuClose(&gpu);
        return;
    }
    DkShader vertex_shader;
    DkShader fragment_shader;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_BINDLESS_FRAG),
                                      ShaderSize(SHADER_BINDLESS_FRAG));
    CHECK_TRUE(SUITE, "the bindless sampling shader loads", CHECK_NONE, loaded, "%s",
               gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }

    if (!ReportStart(SUITE, "256 one-texel textures, their descriptors and handles are in place")) {
        GpuClose(&gpu);
        return;
    }
    DkGpuAddr image_set = 0;
    DkGpuAddr sampler_set = 0;
    DkGpuAddr handles_address = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &gpu, TEXTURES * (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT,
        &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        &gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
        &sampler_set);
    uint32_t *const handles = (uint32_t *)GpuAllocData(&gpu, HANDLES_SIZE,
                                                       DK_UNIFORM_BUF_ALIGNMENT, &handles_address);
    bool created = images != NULL && samplers != NULL && handles != NULL;
    if (handles != NULL) {
        memset(handles, 0, HANDLES_SIZE);
    }
    for (uint32_t i = 0; i < TEXTURES && created; ++i) {
        DkImage image;
        DkImageLayout layout;
        uint8_t *bytes = NULL;
        created = GpuCreateImage(&gpu, &image, &layout, DkImageFormat_RGBA8_Unorm, 1, 1, 0, 0,
                                 &bytes);
        if (!created) {
            break;
        }
        // Texel (0, 0) of a block linear image is its first four bytes.
        Texel(i, bytes);
        DkImageView view;
        dkImageViewDefaults(&view, &image);
        dkImageDescriptorInitialize(&images[i], &view, false, false);
        handles[i * 4u] = dkMakeTextureHandle(i, 0);
    }
    CHECK_TRUE(SUITE, "256 one-texel textures, their descriptors and handles are in place",
               CHECK_NONE, created, "%s", gpu.failure);
    if (!created) {
        GpuClose(&gpu);
        return;
    }
    {
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = DkFilter_Nearest;
        sampler.magFilter = DkFilter_Nearest;
        sampler.mipFilter = DkMipFilter_None;
        dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    }

    // One draw for both halves: it is the first case's work, and the second is judged from the
    // same readback, so it has nothing to judge when the first did not run.
    static const char *const BELOW = "a handle index below 128 samples its own texture";
    static const char *const ABOVE = "a handle index from 128 to 255 samples its own texture";
    if (!ReportStart(SUITE, BELOW)) {
        ReportSkip(SUITE, ABOVE, REPORT_REASON_DEPENDS,
                   "judged from the draw of the case before it, which did not run");
        GpuClose(&gpu);
        return;
    }
    GpuClear(&gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    static const GpuVertex quad[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    GpuBeginPass(&gpu);
    dkCmdBufBindImageDescriptorSet(gpu.cmdbuf, image_set, TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(gpu.cmdbuf, sampler_set, 1);
    dkCmdBufBindUniformBuffer(gpu.cmdbuf, DkStage_Fragment, 0, handles_address, HANDLES_SIZE);
    const bool drawn = GpuDrawTriangles(&gpu, &vertex_shader, &fragment_shader, quad, 6);
    GpuSubmit(&gpu);
    const uint8_t *const pixels = drawn && gpu.ready ? GpuReadBack(&gpu) : NULL;
    if (pixels == NULL) {
        const char *const why =
            gpu.failure[0] != '\0' ? gpu.failure : "the draw or the readback did not happen";
        ReportCase(SUITE, BELOW, CHECK_NONE, false, why);
        if (ReportStart(SUITE, ABOVE)) {
            ReportCase(SUITE, ABOVE, CHECK_NONE, false, why);
        }
        GpuClose(&gpu);
        return;
    }
    Judge(&gpu, pixels, BELOW, 0, 127);
    if (ReportStart(SUITE, ABOVE)) {
        Judge(&gpu, pixels, ABOVE, 128, 255);
    }
    GpuClose(&gpu);
}
