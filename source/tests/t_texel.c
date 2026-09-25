// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Buffer textures (RGBA8_Uint, 64 texels) starting a few bytes into a 256 byte aligned region:
// 0, 4, 20, 36 and 68 bytes, all the four the library asks of this format (deko3d aligns a
// buffer texture to its texel size). Column x must show texel x. A host that wants its texel
// buffers aligned more coarsely than that (64 bytes is common) has to make up the difference
// somewhere other than in the texels a shader reads.

static const char *const SUITE = "texel";

#define TEXELS 64u
#define TARGET_WIDTH TEXELS
#define TARGET_HEIGHT 4u

static uint32_t Fold(const uint8_t t[4]) {
    return (uint32_t)t[0] ^ ((uint32_t)t[1] << 8) ^ ((uint32_t)t[2] << 16) ^
           ((uint32_t)t[3] << 24);
}

static void Case(GpuContext *gpu, const DkShader *vs, const DkShader *fs, uint32_t skew) {
    char name[128];
    snprintf(name, sizeof(name), "a buffer texture %u bytes into an aligned region fetches its "
                                 "own texels",
             (unsigned)skew);
    DkGpuAddr region_address = 0;
    uint8_t *const region = (uint8_t *)GpuAllocData(gpu, 1024, 256, &region_address);
    DkGpuAddr image_set = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkGpuAddr sampler_set = 0;
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    if (region == NULL || images == NULL || samplers == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left");
        return;
    }
    // The bytes before the texture are ones a shifted view would read as its first texels.
    for (uint32_t i = 0; i < 1024u; ++i) {
        region[i] = (uint8_t)(0xA5u ^ (i * 29u));
    }
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, gpu->device);
    maker.type = DkImageType_Buffer;
    maker.format = DkImageFormat_RGBA8_Uint;
    maker.dimensions[0] = TEXELS;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &maker);
    DkImage image;
    const uint32_t offset =
        (uint32_t)(region_address - dkMemBlockGetGpuAddr(gpu->data_memory)) + skew;
    dkImageInitialize(&image, &layout, gpu->data_memory, offset);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);

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
    const bool drawn = GpuDrawTriangles(gpu, vs, fs, quad, 6);
    GpuSubmit(gpu);
    const uint8_t *const pixels = drawn && gpu->ready ? GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t first = 0;
    int32_t shift = 0;
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint32_t want = Fold(region + skew + x * 4u);
            const uint8_t expect[4] = { (uint8_t)want, (uint8_t)(want >> 8), (uint8_t)(want >> 16),
                                        (uint8_t)(want >> 24) };
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            const bool right = memcmp(pixel, expect, 4) == 0;
            PictureSet(picture, x, y, expect, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                if (wrong == 0) {
                    first = x;
                    const uint32_t got = (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 |
                                         (uint32_t)pixel[2] << 16 | (uint32_t)pixel[3] << 24;
                    for (int32_t d = -64; d <= 64; d += 4) {
                        const int32_t at = (int32_t)skew + (int32_t)x * 4 + d;
                        if (at >= 0 && at + 4 <= 1024 && Fold(region + at) == got) {
                            shift = d;
                            break;
                        }
                    }
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong; column %u shows the texel %d bytes from its own",
               (unsigned)wrong, (unsigned)(TARGET_WIDTH * TARGET_HEIGHT), (unsigned)first,
               (int)shift);
}

void RunTexel(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens", CHECK_NONE, opened, "%s", gpu.failure);
    if (!opened) {
        return;
    }
    DkShader vs;
    DkShader fs;
    const bool loaded =
        GpuLoadShader(&gpu, &vs, ShaderData(SHADER_COMMON_VERT), ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&gpu, &fs, ShaderData(SHADER_TEXEL_FRAG), ShaderSize(SHADER_TEXEL_FRAG));
    CHECK_TRUE(SUITE, "the buffer texture shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    static const uint32_t SKEWS[5] = { 0, 4, 20, 36, 68 };
    for (uint32_t i = 0; i < 5u; ++i) {
        Case(&gpu, &vs, &fs, SKEWS[i]);
    }
    GpuClose(&gpu);
}
