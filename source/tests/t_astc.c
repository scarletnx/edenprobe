// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../astcblocks.h"
#include "../astcref.h"
#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// ASTC textures of one block each, the block written by the processor straight into image
// memory, and every texel fetched onto a pixel of its own. The console decodes ASTC in its
// texture unit; an emulator on a host without ASTC decodes it itself, on the processor or in a
// compute shader, and this is where the two are compared.
//
// What each texel must be is source/astcref.c's answer, held to ARM's reference decoder by the
// reference suite. An UNORM ASTC texel reaches the shader as C / 65535 or, where the texture unit
// decodes to eight bits, as C's top byte; the target keeps eight bits, so each channel may be
// either of the two bytes those give (astcref.h, AstcUnorm8Answers) and nothing else. A weight
// one step off moves a channel by a 64th of the endpoints' distance, four bytes on the blocks
// built for it, which no rounding rule allows.

static const char *const SUITE = "astc";

#define TARGET_WIDTH 16u
#define TARGET_HEIGHT 16u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

static DkImageFormat FormatFor(uint32_t width, uint32_t height) {
    if (width == 4u && height == 4u) {
        return DkImageFormat_RGBA_ASTC_4x4;
    }
    if (width == 6u && height == 6u) {
        return DkImageFormat_RGBA_ASTC_6x6;
    }
    if (width == 8u && height == 8u) {
        return DkImageFormat_RGBA_ASTC_8x8;
    }
    if (width == 12u && height == 12u) {
        return DkImageFormat_RGBA_ASTC_12x12;
    }
    return DkImageFormat_None;
}

static void SampleBlock(GpuContext *gpu, const DkShader *vertex_shader,
                        const DkShader *fragment_shader, DkImageDescriptor *image_descriptor,
                        DkGpuAddr image_set, DkGpuAddr sampler_set, uint32_t *size_words,
                        DkGpuAddr size_address, const AstcTestBlock *b) {
    DkImage image;
    DkImageLayout layout;
    uint8_t *bytes = NULL;
    const DkImageFormat format = FormatFor(b->width, b->height);
    if (format == DkImageFormat_None ||
        !GpuCreateImage(gpu, &image, &layout, format, b->width, b->height, 0, 0, &bytes)) {
        ReportCase(SUITE, b->sentence, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no image for the block");
        return;
    }
    // Block (0, 0) of a block linear image is its first sixteen bytes.
    memcpy(bytes, b->bytes, 16);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    dkImageDescriptorInitialize(image_descriptor, &view, false, false);
    size_words[0] = b->width;
    size_words[1] = b->height;

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
        ReportCase(SUITE, b->sentence, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }

    static uint16_t texels[ASTC_MAX_TEXELS][4];
    AstcDecodeBlock(b->bytes, b->width, b->height, texels);
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t worst = 0;
    uint32_t first = 0;
    uint8_t got[4] = { 0, 0, 0, 0 };
    uint8_t want[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (x >= b->width || y >= b->height) {
                PictureSet(picture, x, y, pixel, PICTURE_UNJUDGED);
                continue;
            }
            const uint16_t *const texel = texels[y * b->width + x];
            uint8_t expect[4];
            bool right = true;
            uint32_t distance = 0;
            for (uint32_t c = 0; c < 4u; ++c) {
                uint8_t rounded;
                uint8_t truncated;
                AstcUnorm8Answers(texel[c], &rounded, &truncated);
                expect[c] = rounded;
                if (pixel[c] != rounded && pixel[c] != truncated) {
                    right = false;
                    const uint32_t d = pixel[c] > rounded ? pixel[c] - rounded : rounded - pixel[c];
                    if (d > distance) {
                        distance = d;
                    }
                }
            }
            PictureSet(picture, x, y, expect, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (right) {
                continue;
            }
            if (wrong == 0) {
                first = y * b->width + x;
                memcpy(got, pixel, 4);
                memcpy(want, expect, 4);
            }
            if (distance > worst) {
                worst = distance;
            }
            ++wrong;
        }
    }
    CHECK_TRUE(SUITE, b->sentence, CHECK_EXACT, wrong == 0,
               "%s %ux%u: %u of %u texels wrong, worst channel %u away; texel %u is "
               "%02x%02x%02x%02x, the specification's %02x%02x%02x%02x",
               b->name, (unsigned)b->width, (unsigned)b->height, (unsigned)wrong,
               (unsigned)(b->width * b->height), (unsigned)worst, (unsigned)first, got[0], got[1],
               got[2], got[3], want[0], want[1], want[2], want[3]);
}

void RunAstc(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = ASTC_TEST_BLOCKS * 4096u;

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
    for (uint32_t i = 0; i < ASTC_TEST_BLOCKS; ++i) {
        // Fresh descriptors and a fresh size per block, never rewritten while a list reads them.
        DkGpuAddr image_set = 0;
        DkGpuAddr sampler_set = 0;
        DkGpuAddr size_address = 0;
        DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
            &gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
        DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
            &gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
            &sampler_set);
        uint32_t *const size_words = (uint32_t *)GpuAllocData(
            &gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &size_address);
        if (images == NULL || samplers == NULL || size_words == NULL) {
            ReportCase(SUITE, ASTC_TEST_BLOCK[i].sentence, CHECK_NONE, false,
                       "no scratch left for the descriptors");
            continue;
        }
        memset(size_words, 0, UNIFORM_SIZE);
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = DkFilter_Nearest;
        sampler.magFilter = DkFilter_Nearest;
        sampler.mipFilter = DkMipFilter_None;
        dkSamplerDescriptorInitialize(&samplers[0], &sampler);
        SampleBlock(&gpu, &vertex_shader, &fragment_shader, &images[0], image_set, sampler_set,
                    size_words, size_address, &ASTC_TEST_BLOCK[i]);
    }
    GpuClose(&gpu);
}
