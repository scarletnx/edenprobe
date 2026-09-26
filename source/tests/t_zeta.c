// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coherref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Depth textures the processor writes and a draw samples, texel by texel: the depth an emulator
// uploads from guest memory rather than renders. Every texel of a 16x4 image holds one word;
// the library's formats read the depth from the component their swizzle names (deko3d's format
// table: G of Z16 and Z24S8, R of ZF32), and Z24S8 keeps its 8-bit stencil in the low byte and
// its 24-bit depth above it. Each depth is chosen so the byte the RGBA8 target stores is a
// whole number of 255ths or at least a quarter away from a half, so the tier is exact. Z16 and
// ZF32 are the controls: every host has a depth format of their size. A host without
// D24_UNORM_S8_UINT has to convert the 24-bit words; one that copies them as they are samples
// the words as floats.

static const char *const SUITE = "zeta";

#define TARGET_WIDTH 16u
#define TARGET_HEIGHT 16u
#define TEX_WIDTH 16u
#define TEX_HEIGHT 4u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

static void ZetaCase(GpuContext *gpu, const DkShader *vertex_shader,
                     const DkShader *fragment_shader, const char *name, DkImageFormat format,
                     uint32_t bytes_per_texel, uint32_t word, uint8_t want) {
    if (!ReportStart(SUITE, name)) {
        return;
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
        !GpuCreateImage(gpu, &image, &layout, format, TEX_WIDTH, TEX_HEIGHT, 0, 0, &memory)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the texture");
        return;
    }
    // Four rows tall is one gob tall, so the library's tile is one gob.
    for (uint32_t y = 0; y < TEX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TEX_WIDTH; ++x) {
            const uint32_t offset = CohBlockLinearOffset(x * bytes_per_texel, y,
                                                         TEX_WIDTH * bytes_per_texel, 1);
            memcpy(memory + offset, &word, bytes_per_texel);
        }
    }
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
    size_words[0] = TEX_WIDTH;
    size_words[1] = TEX_HEIGHT;

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
    const uint8_t expect[4] = { want, want, want, 255 };
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (y >= TEX_HEIGHT) {
                PictureSet(picture, x, y, pixel, PICTURE_UNJUDGED);
                continue;
            }
            const bool right = memcmp(pixel, expect, 4) == 0;
            PictureSet(picture, x, y, expect, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                if (wrong == 0) {
                    memcpy(seen, pixel, 4);
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u texels wrong; the first is %02x%02x%02x%02x, expected %02x%02x%02x%02x "
               "(word 0x%08x)",
               (unsigned)wrong, (unsigned)(TEX_WIDTH * TEX_HEIGHT), seen[0], seen[1], seen[2],
               seen[3], expect[0], expect[1], expect[2], expect[3], (unsigned)word);
}

void RunZeta(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = 4u * 128u * 1024u;

    if (!ReportStart(SUITE, "graphics device opens with room for the depth textures")) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the depth textures", CHECK_NONE,
               opened, "%s", gpu.failure);
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
    // 0x8080 is 128 * 257 of 255 * 257: exactly 128 / 255.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a Z16 texture the processor wrote samples to its depth", DkImageFormat_Z16, 2,
             0x8080u, 128);
    // 0.75 * 255 = 191.25.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a ZF32 texture the processor wrote samples to its depth", DkImageFormat_ZF32, 4,
             0x3F400000u, 191);
    // Depth 0x808080 = 128 * 0x010101 over 255 * 0x010101, stencil 0x80: the same word read
    // either way round, so only a host that samples the word as a float gets it wrong.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a Z24S8 texture the processor wrote samples to its depth", DkImageFormat_Z24S8, 4,
             0x80808080u, 128);
    // Depth 0x00ff00 above stencil 0x5a: 65280 / 16777215 * 255 = 0.992, so 1. The low 24 bits
    // (0xff005a) would give 254.
    ZetaCase(&gpu, &vertex_shader, &fragment_shader,
             "a Z24S8 texture the processor wrote takes its depth from the top 24 bits",
             DkImageFormat_Z24S8, 4, 0x00FF005Au, 1);
    GpuClose(&gpu);
}
