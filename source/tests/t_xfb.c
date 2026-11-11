// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../rawref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Transform feedback, which a game's driver turns on through the 3D class's registers and deko3d
// has no call for: the vertex stage's outputs written to a buffer as the draw runs, the rasterizer
// off. One triangle's three positions are captured into buffer 0, four words a vertex in the
// order the stream layout names them (the position's attributes, 0x1c to 0x1f, a word each), and
// the primitives written counter counts the one triangle. Every captured word is the float the
// vertex stage wrote, so exact; the bytes past the 48 captured keep what they held.
//
// The registers are the 3D class's (byte offsets as reden's maxwell_3d.h asserts them): buffer
// n's enable, address, size and start offset at 0x380 + 0x20 n, its stream, varying count and
// stride at 0x700 + 0x10 n, the enable at 0x744, and the stream layout, a byte per varying, at
// 0x2800 + 0x80 n. ScarletNX's transform feedback test captures through the same registers.

static const char *const SUITE = "xfb";

#define TFB_BUFFER 0x0E0u   ///< buffer 0: enable, address high and low, size, start offset
#define TFB_CONTROL 0x1C0u  ///< buffer 0: stream, varying count, stride
#define TFB_ENABLE 0x1D1u
#define TFB_LAYOUT 0xA00u   ///< buffer 0's varyings, four a word, first in the low byte
#define CAPTURE_BYTES 256u
#define VERTICES 3u
#define POISON 0xCDu

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader flat;
} XfbContext;

static XfbContext context;

/// The three vertices, every coordinate a different float, w one as common.vert.glsl writes it.
static const GpuVertex TRIANGLE[VERTICES] = {
    { 0.25f, -0.5f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    { -0.125f, 0.375f, 0.0625f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.875f, 0.625f, -0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
};

/// Records the triangle drawn with the rasterizer off and buffer 0 capturing the position into
/// @p capture; @p counter, when not nought, gets the primitives written report after the draw.
static bool Capture(DkGpuAddr capture, DkGpuAddr counter) {
    GpuBeginList(&context.gpu);
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.rasterizerEnable = 0;
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(context.gpu.cmdbuf, &rasterizer);
    if (counter != 0) {
        dkCmdBufResetCounter(context.gpu.cmdbuf, DkCounter_TransformFeedbackPrimitivesWritten);
    }
    const uint32_t on[] = {
        RawIncreasingHeader(RAW_SUBCHANNEL_3D, TFB_BUFFER, 5),
        1u,
        (uint32_t)(capture >> 32),
        (uint32_t)capture,
        CAPTURE_BYTES,
        0u,
        RawIncreasingHeader(RAW_SUBCHANNEL_3D, TFB_CONTROL, 3),
        0u,
        4u,
        16u,
        RawIncreasingHeader(RAW_SUBCHANNEL_3D, TFB_LAYOUT, 1),
        0x1F1E1D1Cu,
        RawInlineHeader(RAW_SUBCHANNEL_3D, TFB_ENABLE, 1),
    };
    const uint32_t off[] = {
        RawInlineHeader(RAW_SUBCHANNEL_3D, TFB_ENABLE, 0),
        RawInlineHeader(RAW_SUBCHANNEL_3D, TFB_BUFFER, 0),
    };
    bool ok = GpuBindTriangleState(&context.gpu, &context.vertex, &context.flat, TRIANGLE,
                                   VERTICES) &&
              GpuRawMethods(&context.gpu, on, (uint32_t)(sizeof(on) / sizeof(on[0])));
    if (ok) {
        dkCmdBufDraw(context.gpu.cmdbuf, DkPrimitive_Triangles, VERTICES, 1, 0, 0);
        if (counter != 0) {
            dkCmdBufReportCounter(context.gpu.cmdbuf, DkCounter_TransformFeedbackPrimitivesWritten,
                                  counter);
        }
    }
    ok = ok && GpuRawMethods(&context.gpu, off, (uint32_t)(sizeof(off) / sizeof(off[0])));
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    GpuSubmit(&context.gpu);
    return ok && context.gpu.ready;
}

static void PositionsCase(void) {
    static const char *const NAME = "a capture of one triangle writes its three positions, 48 "
                                    "bytes";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    DkGpuAddr address = 0;
    uint8_t *const bytes = (uint8_t *)GpuAllocData(&context.gpu, CAPTURE_BYTES, 256u, &address);
    if (bytes == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the buffer");
        return;
    }
    memset(bytes, POISON, CAPTURE_BYTES);
    if (!Capture(address, 0)) {
        ReportCase(SUITE, NAME, CHECK_NONE, false,
                   context.gpu.failure[0] != '\0' ? context.gpu.failure
                                                  : "the capture was not recorded");
        return;
    }
    uint8_t want[CAPTURE_BYTES];
    memset(want, POISON, sizeof(want));
    for (uint32_t v = 0; v < VERTICES; ++v) {
        const float position[4] = { TRIANGLE[v].x, TRIANGLE[v].y, TRIANGLE[v].z, 1.0f };
        memcpy(want + v * 16u, position, 16);
    }
    uint32_t wrong = 0, first = 0;
    for (uint32_t i = 0; i < CAPTURE_BYTES; i += 4u) {
        if (memcmp(bytes + i, want + i, 4) != 0 && wrong++ == 0u) {
            first = i;
        }
    }
    uint32_t got = 0, expected = 0;
    memcpy(&got, bytes + first, 4);
    memcpy(&expected, want + first, 4);
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, wrong == 0,
               "%u of %u words wrong; the first, at byte %u, holds 0x%08x, expected 0x%08x (0xcdcdcdcd"
               " is a word nothing wrote)",
               (unsigned)wrong, (unsigned)(CAPTURE_BYTES / 4u), (unsigned)first, (unsigned)got,
               (unsigned)expected);
}

static void CounterCase(void) {
    static const char *const NAME = "the primitives written counter counts the captured triangle";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    DkGpuAddr address = 0, counter = 0;
    uint8_t *const bytes = (uint8_t *)GpuAllocData(&context.gpu, CAPTURE_BYTES, 256u, &address);
    uint64_t *const report = (uint64_t *)GpuAllocData(&context.gpu, 16u, 256u, &counter);
    if (bytes == NULL || report == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    memset(bytes, POISON, CAPTURE_BYTES);
    memset(report, 0xA5, 16);
    if (!Capture(address, counter)) {
        ReportCase(SUITE, NAME, CHECK_NONE, false,
                   context.gpu.failure[0] != '\0' ? context.gpu.failure
                                                  : "the capture was not recorded");
        return;
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, report[0] == 1u,
               "the counter reads %llu (0x%016llx), expected 1", (unsigned long long)report[0],
               (unsigned long long)report[0]);
}

void RunXfb(void) {
    static const char *const OPEN = "graphics device opens";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    memset(&context, 0, sizeof(context));
    GpuSpec spec;
    GpuSpecDefaults(&spec, 16, 16);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    const bool opened = GpuOpenSpec(&context.gpu, &spec);
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, opened, "%s", context.gpu.failure);
    if (!opened) {
        return;
    }
    static const char *const LOAD = "the shaders load";
    if (!ReportStart(SUITE, LOAD)) {
        GpuClose(&context.gpu);
        return;
    }
    const bool loaded = GpuLoadShader(&context.gpu, &context.vertex, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&context.gpu, &context.flat, ShaderData(SHADER_FLAT_FRAG),
                                      ShaderSize(SHADER_FLAT_FRAG));
    CHECK_TRUE(SUITE, LOAD, CHECK_NONE, loaded, "%s", context.gpu.failure);
    if (!loaded) {
        GpuClose(&context.gpu);
        return;
    }
    PositionsCase();
    CounterCase();
    GpuClose(&context.gpu);
}
