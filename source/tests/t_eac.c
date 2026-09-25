// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../eacref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// EAC textures, the one- and two-channel 11-bit formats of ETC2 (the library's R_ETC2 and
// RG_ETC2), one 4x4 block each written by the processor into image memory and fetched texel by
// texel. The blocks use every index, both clamps and the multiplier of nought. What each texel
// must be is source/eacref.c's answer; the 11-bit value may reach the target as either of the
// two bytes eacref.h's EacUnorm8Answers gives, and green, blue and alpha of a one-channel
// texel are 0, 0 and 1.

static const char *const SUITE = "eac";

#define TARGET_WIDTH 16u
#define TARGET_HEIGHT 16u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

static bool Channel(uint8_t got, uint16_t value) {
    uint8_t direct;
    uint8_t extended;
    EacUnorm8Answers(value, &direct, &extended);
    return got == direct || got == extended;
}

static void SampleCase(GpuContext *gpu, const DkShader *vertex_shader,
                       const DkShader *fragment_shader, const char *name, DkImageFormat format,
                       const uint8_t *bytes, uint32_t size, const uint16_t red[16],
                       const uint16_t *green) {
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
        !GpuCreateImage(gpu, &image, &layout, format, 4, 4, 0, 0, &memory)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the texture");
        return;
    }
    memcpy(memory, bytes, size);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_None;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    memset(size_words, 0, UNIFORM_SIZE);
    size_words[0] = 4;
    size_words[1] = 4;

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
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t first = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    uint8_t want[4] = { 0, 0, 0, 255 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (x >= 4u || y >= 4u) {
                PictureSet(picture, x, y, pixel, PICTURE_UNJUDGED);
                continue;
            }
            const uint32_t t = y * 4u + x;
            uint8_t expect[4] = { 0, 0, 0, 255 };
            uint8_t unused;
            EacUnorm8Answers(red[t], &expect[0], &unused);
            if (green != NULL) {
                EacUnorm8Answers(green[t], &expect[1], &unused);
            }
            const bool right = Channel(pixel[0], red[t]) &&
                               (green != NULL ? Channel(pixel[1], green[t]) : pixel[1] == 0u) &&
                               pixel[2] == 0u && pixel[3] == 255u;
            PictureSet(picture, x, y, expect, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                if (wrong == 0) {
                    first = t;
                    memcpy(seen, pixel, 4);
                    memcpy(want, expect, 4);
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of 16 texels wrong; texel %u is %02x%02x%02x%02x, the specification's "
               "%02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)first, seen[0], seen[1], seen[2], seen[3], want[0],
               want[1], want[2], want[3]);
}

void RunEac(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = 4u * 4096u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the blocks", CHECK_NONE, opened, "%s",
               gpu.failure);
    if (!opened) {
        return;
    }
    DkShader vertex_shader;
    DkShader fragment_shader;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_FETCH_FRAG),
                                      ShaderSize(SHADER_FETCH_FRAG));
    CHECK_TRUE(SUITE, "the fetching shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }

    // Red: every index across the block, a large multiplier so both clamps are reached.
    // Green: the indices in the other order and the multiplier of nought.
    uint8_t forward[16];
    uint8_t backward[16];
    for (uint32_t i = 0; i < 16u; ++i) {
        forward[i] = (uint8_t)(i & 7u);
        backward[i] = (uint8_t)(7u - (i & 7u));
    }
    uint8_t blocks[16];
    EacBuildBlock(blocks, 0x80, 11, 0, forward);
    EacBuildBlock(blocks + 8, 0x30, 0, 13, backward);
    uint16_t red[16];
    uint16_t green[16];
    EacDecodeUnsigned11(blocks, red);
    EacDecodeUnsigned11(blocks + 8, green);

    SampleCase(&gpu, &vertex_shader, &fragment_shader,
               "a one-channel EAC block decodes every index and both clamps",
               DkImageFormat_R_ETC2_Unorm, blocks, 8, red, NULL);
    SampleCase(&gpu, &vertex_shader, &fragment_shader,
               "a two-channel EAC texture decodes each channel from its own block",
               DkImageFormat_RG_ETC2_Unorm, blocks, 16, red, green);
    GpuClose(&gpu);
}
