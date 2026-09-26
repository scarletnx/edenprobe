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

// One list of 4096 indexed draws, each covering one pixel with the word of a uniform buffer of its
// own, as the stream suite does, through shaders that also read 32 textures each (ring.vert.glsl,
// ring.frag.glsl): 65 descriptors a draw, more than a host pushes, so an emulator writes every
// draw's set into its descriptor ring, and a new one each draw since the uniform buffer moves.
// The ring runs out every few hundred draws, and taking more room than that costs a wait for the
// GPU. An emulator that takes it after it has bound the draw's index and vertex buffers submits
// them with the command buffer before the draw's own, and that draw is lost: its pixel keeps the
// clear. In Eden that is the descriptor buffer ring (renderer_vulkan/vk_descriptor_buffer.cpp)
// and the order of ConfigureImpl (vk_graphics_pipeline.cpp).

static const char *const SUITE = "ring";

#define TARGET_SIZE 64u
#define DRAWS (TARGET_SIZE * TARGET_SIZE)
#define RING_TEXTURES 32u

static uint32_t Word(uint32_t draw) {
    uint32_t x = (draw + 1u) * 0x9E3779B1u ^ 0x5EEDu;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    // Never nought, which is the clear.
    return x | 0x01000000u;
}

void RunRing(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_SIZE, TARGET_SIZE);
    spec.code_size = 32u * 1024u;
    spec.data_size = 2u * 1024u * 1024u;
    spec.image_size = 64u * 1024u;
    spec.command_size = 2u * 1024u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for 4096 uniform buffers", CHECK_NONE,
               opened, "%s", gpu.failure);
    if (!opened) {
        return;
    }
    DkShader vs;
    DkShader fs;
    const bool loaded =
        GpuLoadShader(&gpu, &vs, ShaderData(SHADER_RING_VERT), ShaderSize(SHADER_RING_VERT)) &&
        GpuLoadShader(&gpu, &fs, ShaderData(SHADER_RING_FRAG), ShaderSize(SHADER_RING_FRAG));
    CHECK_TRUE(SUITE, "the shaders that read 32 textures each load", CHECK_NONE, loaded, "%s",
               gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }

    // One texture of one texel, nought, behind all 64 bindings: the shaders add what they read.
    DkImage texture;
    DkImageLayout layout;
    static const uint8_t NOUGHT[4] = { 0, 0, 0, 0 };
    DkGpuAddr image_set = 0;
    DkGpuAddr sampler_set = 0;
    DkGpuAddr vertices_address = 0;
    DkGpuAddr indices_address = 0;
    DkGpuAddr buffers_address = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkSamplerDescriptor *const samplers =
        (DkSamplerDescriptor *)GpuAllocData(&gpu, (uint32_t)sizeof(DkSamplerDescriptor),
                                            DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    float *const vertices = (float *)GpuAllocData(&gpu, DRAWS * 4u * 16u, 256, &vertices_address);
    uint32_t *const indices =
        (uint32_t *)GpuAllocData(&gpu, DRAWS * 6u * 4u, 256, &indices_address);
    uint8_t *const buffers = (uint8_t *)GpuAllocData(&gpu, DRAWS * DK_UNIFORM_BUF_ALIGNMENT,
                                                     DK_UNIFORM_BUF_ALIGNMENT, &buffers_address);
    bool ready = images != NULL && samplers != NULL && vertices != NULL && indices != NULL &&
                 buffers != NULL &&
                 GpuCreateTexture(&gpu, &texture, &layout, DkImageFormat_RGBA8_Unorm, 1, 1) &&
                 GpuUploadTexture(&gpu, &texture, DkImageFormat_RGBA8_Unorm, 1, 1, NOUGHT);
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
        const float step = 2.0f / (float)TARGET_SIZE;
        for (uint32_t k = 0; k < DRAWS; ++k) {
            const float x0 = -1.0f + step * (float)(k % TARGET_SIZE);
            const float y0 = 1.0f - step * (float)(k / TARGET_SIZE + 1u);
            const float x1 = x0 + step;
            const float y1 = y0 + step;
            const float corners[4][4] = {
                { x0, y0, 0, 1 }, { x1, y0, 0, 1 }, { x1, y1, 0, 1 }, { x0, y1, 0, 1 }
            };
            memcpy(vertices + k * 16u, corners, sizeof(corners));
            static const uint32_t QUAD[6] = { 0, 1, 2, 0, 2, 3 };
            for (uint32_t i = 0; i < 6u; ++i) {
                indices[k * 6u + i] = k * 4u + QUAD[i];
            }
            uint32_t *const words = (uint32_t *)(buffers + (size_t)k * DK_UNIFORM_BUF_ALIGNMENT);
            memset(words, 0, DK_UNIFORM_BUF_ALIGNMENT);
            words[0] = Word(k);
        }
    }
    CHECK_TRUE(SUITE, "the texture, the vertices, the indices and the buffers are in place",
               CHECK_NONE, ready, "%s",
               gpu.failure[0] != '\0' ? gpu.failure : "the scratch block had no room");
    if (!ready) {
        GpuClose(&gpu);
        return;
    }

    static const char *const NAME = "4096 indexed draws that each take new descriptors all land";
    GpuClear(&gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&gpu);
    dkCmdBufBindImageDescriptorSet(gpu.cmdbuf, image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(gpu.cmdbuf, sampler_set, 1);
    DkResHandle handles[RING_TEXTURES];
    for (uint32_t i = 0; i < RING_TEXTURES; ++i) {
        handles[i] = dkMakeTextureHandle(0, 0);
    }
    dkCmdBufBindTextures(gpu.cmdbuf, DkStage_Vertex, 0, handles, RING_TEXTURES);
    dkCmdBufBindTextures(gpu.cmdbuf, DkStage_Fragment, 0, handles, RING_TEXTURES);
    const DkShader *const shaders[] = { &vs, &fs };
    dkCmdBufBindShaders(gpu.cmdbuf, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
    static const DkVtxAttribState attribute = { .bufferId = 0,
                                                .isFixed = 0,
                                                .offset = 0,
                                                .size = DkVtxAttribSize_4x32,
                                                .type = DkVtxAttribType_Float,
                                                .isBgra = 0 };
    static const DkVtxBufferState buffer_state = { 16, 0 };
    dkCmdBufBindVtxAttribState(gpu.cmdbuf, &attribute, 1);
    dkCmdBufBindVtxBufferState(gpu.cmdbuf, &buffer_state, 1);
    dkCmdBufBindVtxBuffer(gpu.cmdbuf, 0, vertices_address, DRAWS * 4u * 16u);
    dkCmdBufBindIdxBuffer(gpu.cmdbuf, DkIdxFormat_Uint32, indices_address);
    for (uint32_t k = 0; k < DRAWS; ++k) {
        dkCmdBufBindUniformBuffer(gpu.cmdbuf, DkStage_Fragment, 0,
                                  buffers_address + (DkGpuAddr)k * DK_UNIFORM_BUF_ALIGNMENT,
                                  DK_UNIFORM_BUF_ALIGNMENT);
        dkCmdBufDrawIndexed(gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, k * 6u, 0, 0);
    }
    GpuSubmit(&gpu);
    const uint8_t *const pixels = gpu.ready ? GpuReadBack(&gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false,
                   gpu.failure[0] != '\0' ? gpu.failure : "the list or the readback did not happen");
        GpuClose(&gpu);
        return;
    }
    Picture *const picture = PictureBegin(TARGET_SIZE, TARGET_SIZE, PICTURE_COLOUR, pixels);
    uint32_t missing = 0;
    uint32_t other = 0;
    int32_t first_missing = -1;
    int32_t first_other = -1;
    uint32_t seen_other = 0;
    for (uint32_t k = 0; k < DRAWS; ++k) {
        const uint32_t want = Word(k);
        const uint8_t expect[4] = { (uint8_t)want, (uint8_t)(want >> 8), (uint8_t)(want >> 16),
                                    (uint8_t)(want >> 24) };
        const uint8_t *const pixel = GpuPixel(pixels, TARGET_SIZE, k % TARGET_SIZE,
                                              k / TARGET_SIZE);
        const bool right = memcmp(pixel, expect, 4) == 0;
        PictureSet(picture, k % TARGET_SIZE, k / TARGET_SIZE, expect,
                   right ? PICTURE_MATCHED : PICTURE_WRONG);
        if (right) {
            continue;
        }
        const uint32_t got = (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 |
                             (uint32_t)pixel[2] << 16 | (uint32_t)pixel[3] << 24;
        if (got == 0u) {
            if (first_missing < 0) {
                first_missing = (int32_t)k;
            }
            ++missing;
        } else {
            if (first_other < 0) {
                first_other = (int32_t)k;
                seen_other = got;
            }
            ++other;
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, missing == 0 && other == 0,
               "%u of %u draws left their pixel cleared, the first draw %d; %u drew another "
               "word, the first draw %d (0x%08x)",
               (unsigned)missing, (unsigned)DRAWS, (int)first_missing, (unsigned)other,
               (int)first_other, (unsigned)seen_other);
    GpuClose(&gpu);
}
