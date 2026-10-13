// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../etc2blocks.h"
#include "../etc2ref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// ETC2 colour textures (the library's RGB_ETC2, RGB_PTA_ETC2 and RGBA_ETC2), one 4x4 block each
// written by the processor into image memory and fetched texel by texel: every mode (individual,
// differential, T, H and planar), both orders of the H mode bases, the punch-through rules and
// the RGBA alpha block. What each texel must be is source/etc2ref.c's answer, which the
// reference suite holds to Mesa's decoder block by block. ETC2 decodes to exact bytes, so the
// tier is exact. A host without ETC2 textures has to decode them itself; an emulator that
// uploads the blocks as they are shows bytes of block data instead of colours.

static const char *const SUITE = "etc2";

#define TARGET_WIDTH 16u
#define TARGET_HEIGHT 16u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

static DkImageFormat Format(uint32_t kind) {
    return kind == 0u ? DkImageFormat_RGB_ETC2
                      : kind == 1u ? DkImageFormat_RGB_PTA_ETC2 : DkImageFormat_RGBA_ETC2;
}

static void SampleCase(GpuContext *gpu, const DkShader *vertex_shader,
                       const DkShader *fragment_shader, const Etc2TestBlock *block) {
    const char *const name = block->sentence;
    if (!ReportStart(SUITE, name)) {
        return;
    }
    uint8_t want[16][4];
    if (block->kind == 2u) {
        Etc2DecodeRgba(block->bytes, want);
    } else {
        Etc2DecodeRgb(block->bytes, block->kind == 1u, want);
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
        !GpuCreateImage(gpu, &image, &layout, Format(block->kind), 4, 4, 0, 0, &memory)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the texture");
        return;
    }
    // One block is the first bytes of the first gob, so it is written as it is.
    memcpy(memory, block->bytes, block->kind == 2u ? 16u : 8u);
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
    uint8_t got[16][4];
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (x >= 4u || y >= 4u) {
                PictureSet(picture, x, y, pixel, PICTURE_UNJUDGED);
                continue;
            }
            const uint32_t t = y * 4u + x;
            memcpy(got[t], pixel, 4);
            const bool right = memcmp(pixel, want[t], 4) == 0;
            PictureSet(picture, x, y, want[t], right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                if (wrong == 0) {
                    first = t;
                    memcpy(seen, pixel, 4);
                }
                ++wrong;
            }
        }
    }
    // The signature the detail names, as the order and coherence suites name theirs.
    const char *const raw = Etc2LooksRaw(block->bytes, (const uint8_t(*)[4])got)
                                ? "; texels 0 and 1 are the block's own bytes: it was uploaded "
                                  "as it is and never decoded"
                                : "";
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of 16 texels wrong; texel %u is %02x%02x%02x%02x, the specification's "
               "%02x%02x%02x%02x%s",
               (unsigned)wrong, (unsigned)first, seen[0], seen[1], seen[2], seen[3],
               want[first][0], want[first][1], want[first][2], want[first][3], raw);
}

void RunEtc2(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = ETC2_TEST_BLOCKS * 8192u;

    if (!ReportStart(SUITE, "graphics device opens with room for the blocks")) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the blocks", CHECK_NONE, opened, "%s",
               gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the fetching shader loads")) {
        GpuClose(&gpu);
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
    for (uint32_t i = 0; i < ETC2_TEST_BLOCKS; ++i) {
        SampleCase(&gpu, &vertex_shader, &fragment_shader, &ETC2_TEST_BLOCK[i]);
    }
    GpuClose(&gpu);
}
