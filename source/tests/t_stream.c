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

// One list of draws, each covering one pixel with a word from a uniform buffer of its own, read
// at an index the shader computes, so the whole buffer has to reach it (value_ubo_all.frag.glsl).
// Every buffer is different memory the processor filled before the list, so an emulator uploads
// each one: 4096 of 4 KiB (16 MiB) and 1024 of 64 KiB (64 MiB). Pixel k must hold buffer k's
// word, and the time of each case is what those uploads cost.
//
// With the mode word `stress` a third case asks for 4096 of 64 KiB, 256 MiB in one list: more
// than the ring an emulator stages uploads through, which then has to wrap inside the list
// without handing a later draw's bytes to an earlier one. Off by default because it asks for far
// more than a game does in one list, and an emulator that runs out of memory on it takes the
// rest of the run down with it.

static const char *const SUITE = "stream";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define MAX_DRAWS (TARGET_WIDTH * TARGET_HEIGHT)

static bool stress_cases;

void StreamSetStress(bool on) {
    stress_cases = on;
}

static uint32_t Word(uint32_t draw, uint32_t salt) {
    uint32_t x = (draw + 1u) * 0x9E3779B1u ^ salt;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    // Never nought, which is the clear.
    return x | 0x01000000u;
}

static void Case(GpuContext *gpu, const DkShader *vs, const DkShader *fs, const char *name,
                 uint32_t draws, uint32_t buffer_size, uint32_t salt) {
    DkGpuAddr vertices_address = 0;
    float *const vertices =
        (float *)GpuAllocData(gpu, MAX_DRAWS * 6u * 16u, 256, &vertices_address);
    DkGpuAddr buffers_address = 0;
    uint8_t *const buffers = (uint8_t *)GpuAllocData(gpu, draws * buffer_size,
                                                     DK_UNIFORM_BUF_ALIGNMENT, &buffers_address);
    if (vertices == NULL || buffers == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    const float step_x = 2.0f / (float)TARGET_WIDTH;
    const float step_y = 2.0f / (float)TARGET_HEIGHT;
    for (uint32_t k = 0; k < draws; ++k) {
        const float x0 = -1.0f + step_x * (float)(k % TARGET_WIDTH);
        const float y0 = 1.0f - step_y * (float)(k / TARGET_WIDTH + 1u);
        const float x1 = x0 + step_x;
        const float y1 = y0 + step_y;
        const float quad[6][4] = { { x0, y0, 0, 1 }, { x1, y0, 0, 1 }, { x1, y1, 0, 1 },
                                   { x0, y0, 0, 1 }, { x1, y1, 0, 1 }, { x0, y1, 0, 1 } };
        memcpy(vertices + k * 24u, quad, sizeof(quad));
        // Every 16 byte entry holds the buffer's word: the shader may read any of them.
        uint32_t *const words = (uint32_t *)(buffers + (size_t)k * buffer_size);
        for (uint32_t w = 0; w < buffer_size / 4u; w += 4u) {
            words[w] = Word(k, salt);
        }
    }

    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(gpu);
    const DkShader *const shaders[] = { vs, fs };
    dkCmdBufBindShaders(gpu->cmdbuf, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
    static const DkVtxAttribState attribute = { .bufferId = 0,
                                                .isFixed = 0,
                                                .offset = 0,
                                                .size = DkVtxAttribSize_4x32,
                                                .type = DkVtxAttribType_Float,
                                                .isBgra = 0 };
    static const DkVtxBufferState buffer_state = { 16, 0 };
    dkCmdBufBindVtxAttribState(gpu->cmdbuf, &attribute, 1);
    dkCmdBufBindVtxBufferState(gpu->cmdbuf, &buffer_state, 1);
    dkCmdBufBindVtxBuffer(gpu->cmdbuf, 0, vertices_address, MAX_DRAWS * 6u * 16u);
    DkGpuAddr count_address = 0;
    uint32_t *const count = (uint32_t *)GpuAllocData(gpu, DK_UNIFORM_BUF_ALIGNMENT,
                                                     DK_UNIFORM_BUF_ALIGNMENT, &count_address);
    if (count == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the entry count");
        return;
    }
    memset(count, 0, DK_UNIFORM_BUF_ALIGNMENT);
    count[0] = buffer_size / 16u;
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 1, count_address,
                              DK_UNIFORM_BUF_ALIGNMENT);
    for (uint32_t k = 0; k < draws; ++k) {
        dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0,
                                  buffers_address + (DkGpuAddr)k * buffer_size, buffer_size);
        dkCmdBufDraw(gpu->cmdbuf, DkPrimitive_Triangles, 6, 1, k * 6u, 0);
    }
    GpuSubmit(gpu);
    const uint8_t *const pixels = gpu->ready ? GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the list or readback did not happen");
        return;
    }
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t first = 0;
    int32_t from = -1;
    uint32_t got_first = 0;
    for (uint32_t k = 0; k < MAX_DRAWS; ++k) {
        const uint32_t x = k % TARGET_WIDTH;
        const uint32_t y = k / TARGET_WIDTH;
        const uint32_t want = k < draws ? Word(k, salt) : 0u;
        const uint8_t expect[4] = { (uint8_t)want, (uint8_t)(want >> 8), (uint8_t)(want >> 16),
                                    (uint8_t)(want >> 24) };
        const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
        const bool right = memcmp(pixel, expect, 4) == 0;
        PictureSet(picture, x, y, expect, right ? PICTURE_MATCHED : PICTURE_WRONG);
        if (right) {
            continue;
        }
        if (wrong == 0) {
            first = k;
            got_first = (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 | (uint32_t)pixel[2] << 16 |
                        (uint32_t)pixel[3] << 24;
            for (uint32_t j = 0; j < draws; ++j) {
                if (Word(j, salt) == got_first) {
                    from = (int32_t)j;
                    break;
                }
            }
        }
        ++wrong;
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u draws saw another buffer's word; draw %u saw 0x%08x, which is buffer "
               "%d's (-1: none)",
               (unsigned)wrong, (unsigned)MAX_DRAWS, (unsigned)first, (unsigned)got_first,
               (int)from);
}

void RunStream(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = (stress_cases ? 344u : 88u) * 1024u * 1024u;
    spec.command_size = 1024u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the uniform buffers", CHECK_NONE,
               opened, "%s", gpu.failure);
    if (!opened) {
        return;
    }
    DkShader vs;
    DkShader fs;
    const bool loaded = GpuLoadShader(&gpu, &vs, ShaderData(SHADER_POSITION_VERT),
                                      ShaderSize(SHADER_POSITION_VERT)) &&
                        GpuLoadShader(&gpu, &fs, ShaderData(SHADER_VALUE_UBO_ALL_FRAG),
                                      ShaderSize(SHADER_VALUE_UBO_ALL_FRAG));
    CHECK_TRUE(SUITE, "the drawing shaders load", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    Case(&gpu, &vs, &fs, "4096 draws each see their own 4 KiB uniform buffer", 4096u, 4096u,
         0x0000A11Cu);
    Case(&gpu, &vs, &fs, "1024 draws each see their own 64 KiB uniform buffer", 1024u, 65536u,
         0x0000B22Du);
    if (stress_cases) {
        Case(&gpu, &vs, &fs,
             "4096 draws each see their own 64 KiB uniform buffer, 256 MiB in one list", 4096u,
             65536u, 0x0000C33Eu);
    }
    GpuClose(&gpu);
}
