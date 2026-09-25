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

// Fill rate, for time: 128 quads over a 512 by 512 target in one list, first with colour alone
// and then with a 32-bit float depth buffer tested and written by every quad, each nearer than
// the last. The pixels are judged too (the last quad's colour everywhere, and its depth), so a
// time is never a time for a wrong answer. The difference between the two is what depth costs
// the machine, which is where losing depth compression shows.

static const char *const SUITE = "fill";

#define TARGET_WIDTH 512u
#define TARGET_HEIGHT 512u
#define QUADS 128u

static GpuVertex Corner(float x, float y, float z, const uint8_t colour[4]) {
    GpuVertex v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.u = 0.0f;
    v.v = 0.0f;
    v.r = (float)colour[0] / 255.0f;
    v.g = (float)colour[1] / 255.0f;
    v.b = (float)colour[2] / 255.0f;
    v.a = (float)colour[3] / 255.0f;
    return v;
}

static void Colour(uint32_t k, uint8_t out[4]) {
    out[0] = (uint8_t)(k * 2u + 1u);
    out[1] = (uint8_t)(255u - k);
    out[2] = (uint8_t)(k * 7u);
    out[3] = 255u;
}

/// Quad k's depth: (255 - k) / 256, a binary fraction the transform carries exactly.
static float Depth(uint32_t k) {
    return (float)(255u - k) / 256.0f;
}

static void Case(GpuContext *gpu, const DkShader *vs, const DkShader *fs, bool depth,
                 uint32_t quads, const char *name) {
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuClearDepth(gpu, 1.0f);
    GpuBeginPass(gpu);
    DkDepthStencilState state;
    dkDepthStencilStateDefaults(&state);
    state.depthTestEnable = depth ? 1u : 0u;
    state.depthWriteEnable = depth ? 1u : 0u;
    state.stencilTestEnable = 0;
    state.depthCompareOp = DkCompareOp_Less;
    dkCmdBufBindDepthStencilState(gpu->cmdbuf, &state);
    bool ok = true;
    for (uint32_t k = 0; k < quads && ok; ++k) {
        uint8_t c[4];
        Colour(k, c);
        const float z = Depth(k);
        const GpuVertex quad[6] = { Corner(-1, 1, z, c), Corner(1, 1, z, c),  Corner(1, -1, z, c),
                                    Corner(-1, 1, z, c), Corner(1, -1, z, c), Corner(-1, -1, z, c) };
        ok = GpuDrawTriangles(gpu, vs, fs, quad, 6);
    }
    GpuSubmit(gpu);
    const uint8_t *const pixels = ok && gpu->ready ? GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the list or readback did not happen");
        return;
    }
    uint8_t want[4];
    Colour(quads - 1u, want);
    uint32_t wrong = 0;
    for (uint32_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        if (memcmp(pixels + (size_t)i * 4u, want, 4) != 0) {
            ++wrong;
        }
    }
    uint32_t depth_wrong = 0;
    if (depth) {
        const uint8_t *const d = GpuReadBackDepth(gpu);
        const float last = Depth(quads - 1u);
        for (uint32_t i = 0; d != NULL && i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
            float v;
            memcpy(&v, d + (size_t)i * 4u, 4);
            if (v != last) {
                ++depth_wrong;
            }
        }
        if (d == NULL) {
            depth_wrong = TARGET_WIDTH * TARGET_HEIGHT;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0 && depth_wrong == 0,
               "%u pixels not the last quad's colour, %u not its depth", (unsigned)wrong,
               (unsigned)depth_wrong);
}

void RunFill(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.depth_format = DkImageFormat_ZF32;
    spec.code_size = 32u * 1024u;
    spec.data_size = 256u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with a 512 by 512 depth target", CHECK_NONE, opened,
               "%s", gpu.failure);
    if (!opened) {
        return;
    }
    DkShader vs;
    DkShader fs;
    const bool loaded =
        GpuLoadShader(&gpu, &vs, ShaderData(SHADER_COMMON_VERT), ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&gpu, &fs, ShaderData(SHADER_FLAT_FRAG), ShaderSize(SHADER_FLAT_FRAG));
    CHECK_TRUE(SUITE, "the flat shaders load", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    // Whichever case comes first in a pass pays for the target's first use (about 11 ms against
    // 2 on RADV, either order), so a judged single quad goes first and the two timed ones compare.
    Case(&gpu, &vs, &fs, true, 1u, "one depth tested quad lands first, paying for the first use");
    Case(&gpu, &vs, &fs, false, QUADS, "128 full target quads with colour alone land in order");
    Case(&gpu, &vs, &fs, true, QUADS,
         "128 full target quads, each depth tested and written, land in order");
    GpuClose(&gpu);
}
