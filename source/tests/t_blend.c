// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../blendref.h"
#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The fixed function blend unit, one equation and one pair of factors per case.
//
// Each case clears the target to the destination colour, draws a quad over all of it in the
// source colour under the state the case describes, and compares every pixel to what the
// reference in source/blendref.c works out. The descriptions live there so the reference suite
// can check them with no device, and what it measures about the halfway point between two bytes
// is what makes the exact tier legal on a normalised target.

static const char *const SUITE = "blend";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)

static DkBlendOp HardwareOp(BlendOp op) {
    switch (op) {
    case BLEND_OP_ADD:
        return DkBlendOp_Add;
    case BLEND_OP_SUBTRACT:
        return DkBlendOp_Sub;
    case BLEND_OP_REVERSE_SUBTRACT:
        return DkBlendOp_RevSub;
    case BLEND_OP_MIN:
        return DkBlendOp_Min;
    case BLEND_OP_MAX:
        return DkBlendOp_Max;
    }
    return DkBlendOp_Add;
}

static DkBlendFactor HardwareFactor(BlendFactor factor) {
    switch (factor) {
    case BLEND_FACTOR_ZERO:
        return DkBlendFactor_Zero;
    case BLEND_FACTOR_ONE:
        return DkBlendFactor_One;
    case BLEND_FACTOR_SOURCE_ALPHA:
        return DkBlendFactor_SrcAlpha;
    case BLEND_FACTOR_INVERSE_SOURCE_ALPHA:
        return DkBlendFactor_InvSrcAlpha;
    case BLEND_FACTOR_CONSTANT:
        // The colour constant, whose alpha component is what the alpha channel uses. There is a
        // separate factor for the constant's alpha broadcast to all four channels, and this is
        // not it.
        return DkBlendFactor_ConstColor;
    }
    return DkBlendFactor_Zero;
}

static GpuVertex Corner(float x, float y, const uint8_t colour[4]) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = 0.0f;
    vertex.u = 0.0f;
    vertex.v = 0.0f;
    vertex.r = BlendUnormToFloat(colour[0]);
    vertex.g = BlendUnormToFloat(colour[1]);
    vertex.b = BlendUnormToFloat(colour[2]);
    vertex.a = BlendUnormToFloat(colour[3]);
    return vertex;
}

static void FullScreenQuad(GpuVertex out[6], const uint8_t colour[4]) {
    out[0] = Corner(-1.0f, 1.0f, colour);
    out[1] = Corner(1.0f, 1.0f, colour);
    out[2] = Corner(1.0f, -1.0f, colour);
    out[3] = Corner(-1.0f, 1.0f, colour);
    out[4] = Corner(1.0f, -1.0f, colour);
    out[5] = Corner(-1.0f, -1.0f, colour);
}

/// Compares every pixel to @p expected and writes the sentence that explains a failure.
static bool EveryPixelIs(const uint8_t *pixels, const uint8_t expected[4], char *detail,
                         size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t first[4] = { 0, 0, 0, 0 };

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (CheckChannel(pixel[0], expected[0], 0) && CheckChannel(pixel[1], expected[1], 0) &&
                CheckChannel(pixel[2], expected[2], 0) && CheckChannel(pixel[3], expected[3], 0)) {
                continue;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                memcpy(first, pixel, sizeof(first));
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        detail[0] = '\0';
        return true;
    }
    snprintf(detail, detail_size,
             "%u of %u pixels wrong, first at (%u,%u) is %02x%02x%02x%02x, expected "
             "%02x%02x%02x%02x",
             (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)first_x, (unsigned)first_y,
             first[0], first[1], first[2], first[3], expected[0], expected[1], expected[2],
             expected[3]);
    return false;
}

static void BlendOneCase(GpuContext *gpu, const DkShader *vertex_shader,
                         const DkShader *fragment_shader, const BlendCase *test) {
    uint8_t expected[4];
    BlendReference(&test->spec, test->source, test->destination, expected);

    // The destination arrives by clearing, which the clear suite has already established writes
    // exactly the byte a normalised float asks for.
    GpuClear(gpu, BlendUnormToFloat(test->destination[0]),
             BlendUnormToFloat(test->destination[1]), BlendUnormToFloat(test->destination[2]),
             BlendUnormToFloat(test->destination[3]));

    GpuVertex vertices[6];
    FullScreenQuad(vertices, test->source);

    const uint32_t mark = GpuDataMark(gpu);
    GpuBeginPass(gpu);

    DkColorState color_state;
    dkColorStateDefaults(&color_state);
    dkColorStateSetBlendEnable(&color_state, 0, test->spec.enabled);
    dkCmdBufBindColorState(gpu->cmdbuf, &color_state);

    DkBlendState blend;
    dkBlendStateDefaults(&blend);
    dkBlendStateSetOps(&blend, HardwareOp(test->spec.color_op), HardwareOp(test->spec.alpha_op));
    dkBlendStateSetFactors(&blend, HardwareFactor(test->spec.color_source),
                           HardwareFactor(test->spec.color_destination),
                           HardwareFactor(test->spec.alpha_source),
                           HardwareFactor(test->spec.alpha_destination));
    dkCmdBufBindBlendState(gpu->cmdbuf, 0, &blend);
    dkCmdBufSetBlendConst(gpu->cmdbuf, test->spec.constant[0], test->spec.constant[1],
                          test->spec.constant[2], test->spec.constant[3]);

    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, 6);
    GpuSubmit(gpu);
    GpuDataRewind(gpu, mark);

    if (!drawn) {
        ReportCase(SUITE, test->name, CHECK_EXACT, false, gpu->failure);
        return;
    }

    const uint8_t *const pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, test->name, CHECK_EXACT, false,
                   gpu->ready ? "the readback handed back nothing" : gpu->failure);
        return;
    }

    char detail[320];
    const bool matched = EveryPixelIs(pixels, expected, detail, sizeof(detail));
    CHECK_TRUE(SUITE, test->name, CHECK_EXACT, matched, "%s", detail);
}

void RunBlend(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 16u * 1024u;
    spec.data_size = 64u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens", CHECK_NONE, opened, "%s", gpu.failure);
    if (!opened) {
        return;
    }

    DkShader vertex_shader;
    DkShader fragment_shader;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_FLAT_FRAG),
                                      ShaderSize(SHADER_FLAT_FRAG));
    CHECK_TRUE(SUITE, "the shaders load", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }

    ReportNote("blend target %ux%u RGBA8_Unorm, %u pixels per case, %u cases", TARGET_WIDTH,
               TARGET_HEIGHT, (unsigned)PIXEL_COUNT, (unsigned)BlendCaseCount());

    for (size_t index = 0; index < BlendCaseCount(); ++index) {
        BlendOneCase(&gpu, &vertex_shader, &fragment_shader, BlendCaseAt(index));
    }

    GpuClose(&gpu);
}
