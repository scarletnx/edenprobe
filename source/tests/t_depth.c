// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The depth test, and the depth buffer behind it.
//
// Every case here lays down one quad over the whole target at a known depth and then a second
// one at another known depth with one compare operation, and asks what colour came out. The
// answer is one of two colours for every pixel, so it is exact: nothing in the path from the
// vertex to the target rounds.
//
// All eight compare operations the graphics library offers are bound, over eleven cases: four
// of the eight differ from another only on an equal depth and are worth asking about on both
// sides of it.
//
// The depths are exact binary fractions and stay exact through the transform into depth space,
// so a case can say the buffer holds exactly 0.5 and mean the bit pattern. The last three cases
// read that buffer back and compare the float bit for bit.

static const char *const SUITE = "depth";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)

// The depth the first quad is laid down at, the depth the second one usually challenges it from,
// and one behind it. All three are exact binary fractions.
#define NEAR_DEPTH 0.25f
#define FIRST_DEPTH 0.5f
#define FAR_DEPTH 0.75f
// What the buffer is cleared to before anything is drawn, and what the clear case checks. Chosen
// to be none of the three above, so a clear that quietly did nothing cannot be mistaken for one
// that worked.
#define CLEARED_DEPTH 0.375f

static const uint8_t BACKGROUND[4] = { 0x00, 0x00, 0x00, 0xFF };
static const uint8_t FIRST_COLOR[4] = { 0xFF, 0x00, 0x00, 0xFF };
static const uint8_t SECOND_COLOR[4] = { 0x00, 0xFF, 0x00, 0xFF };

static GpuVertex Corner(float x, float y, float z, const uint8_t colour[4]) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.u = 0.0f;
    vertex.v = 0.0f;
    vertex.r = (float)colour[0] / 255.0f;
    vertex.g = (float)colour[1] / 255.0f;
    vertex.b = (float)colour[2] / 255.0f;
    vertex.a = (float)colour[3] / 255.0f;
    return vertex;
}

static void FullScreenQuad(GpuVertex out[6], float z, const uint8_t colour[4]) {
    out[0] = Corner(-1.0f, 1.0f, z, colour);
    out[1] = Corner(1.0f, 1.0f, z, colour);
    out[2] = Corner(1.0f, -1.0f, z, colour);
    out[3] = Corner(-1.0f, 1.0f, z, colour);
    out[4] = Corner(1.0f, -1.0f, z, colour);
    out[5] = Corner(-1.0f, -1.0f, z, colour);
}

/// Draws one quad over everything at @p z with the depth state the case asks for.
static bool DepthPass(GpuContext *gpu, const DkShader *vertex_shader,
                      const DkShader *fragment_shader, float z, const uint8_t colour[4],
                      DkCompareOp compare, bool write_depth) {
    GpuVertex vertices[6];
    FullScreenQuad(vertices, z, colour);

    GpuBeginPass(gpu);

    DkDepthStencilState state;
    dkDepthStencilStateDefaults(&state);
    state.depthTestEnable = 1;
    state.depthWriteEnable = write_depth ? 1u : 0u;
    state.stencilTestEnable = 0;
    state.depthCompareOp = compare;
    dkCmdBufBindDepthStencilState(gpu->cmdbuf, &state);

    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, 6);
    GpuSubmit(gpu);
    return drawn;
}

/// Clears colour and depth, then lays down the first quad so there is something to test against.
static bool Prepare(GpuContext *gpu, const DkShader *vertex_shader,
                    const DkShader *fragment_shader) {
    GpuClear(gpu, (float)BACKGROUND[0] / 255.0f, (float)BACKGROUND[1] / 255.0f,
             (float)BACKGROUND[2] / 255.0f, (float)BACKGROUND[3] / 255.0f);
    GpuClearDepth(gpu, 1.0f);
    // Always, so this one lands whatever the clear left behind, and writing, so the depth it
    // lands at is what the case below argues with.
    return DepthPass(gpu, vertex_shader, fragment_shader, FIRST_DEPTH, FIRST_COLOR,
                     DkCompareOp_Always, true);
}

static bool EveryPixelIs(const uint8_t *pixels, const uint8_t expected[4], char *detail,
                         size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t first[4] = { 0, 0, 0, 0 };

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (memcmp(pixel, expected, 4) == 0) {
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

static uint32_t FloatBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool EveryDepthIs(const uint8_t *pixels, float expected, char *detail,
                         size_t detail_size) {
    const uint32_t want = FloatBits(expected);
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint32_t first_got = 0;

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            uint32_t got;
            memcpy(&got, pixels + ((size_t)y * TARGET_WIDTH + x) * 4u, sizeof(got));
            if (got == want) {
                continue;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                first_got = got;
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        detail[0] = '\0';
        return true;
    }
    float as_float;
    memcpy(&as_float, &first_got, sizeof(as_float));
    snprintf(detail, detail_size,
             "%u of %u depths wrong, first at (%u,%u) is 0x%08x (%.9f), expected 0x%08x (%.9f)",
             (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)first_x, (unsigned)first_y,
             (unsigned)first_got, (double)as_float, (unsigned)want, (double)expected);
    return false;
}

/// One compare operation: prepare, challenge it, look at the colour.
static void CompareCase(GpuContext *gpu, const DkShader *vertex_shader,
                        const DkShader *fragment_shader, const char *name, float z,
                        DkCompareOp compare, const uint8_t expected[4]) {
    if (!Prepare(gpu, vertex_shader, fragment_shader) ||
        !DepthPass(gpu, vertex_shader, fragment_shader, z, SECOND_COLOR, compare, true)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw was not recorded");
        return;
    }

    const uint8_t *const pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure
                                            : "the readback handed back nothing");
        return;
    }

    char detail[320];
    const bool matched = EveryPixelIs(pixels, expected, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, CHECK_EXACT, matched, "%s", detail);
}

/// Lays the first quad down, draws a nearer one with the depth write open or closed, and looks
/// at what the depth buffer holds afterwards.
///
/// Reports whatever happens, including when the draw never went in: a case that quietly produced
/// no line would leave a summary whose counts do not add up to the cases above it.
static void WriteCase(GpuContext *gpu, const DkShader *vertex_shader,
                      const DkShader *fragment_shader, const char *name, bool write_depth,
                      float expected_depth) {
    if (!Prepare(gpu, vertex_shader, fragment_shader) ||
        !DepthPass(gpu, vertex_shader, fragment_shader, NEAR_DEPTH, SECOND_COLOR,
                   DkCompareOp_Less, write_depth)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw was not recorded");
        return;
    }

    const uint8_t *const depths = GpuReadBackDepth(gpu);
    if (depths == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure
                                           : "the depth readback handed back nothing");
        return;
    }

    char detail[320];
    const bool matched = EveryDepthIs(depths, expected_depth, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, CHECK_EXACT, matched, "%s", detail);
}

/// The other half of that claim: closing the depth write stops the depth, not the colour.
static void ColorCase(GpuContext *gpu, const DkShader *vertex_shader,
                      const DkShader *fragment_shader) {
    static const char *const NAME = "closing the depth write does not stop the colour";
    if (!Prepare(gpu, vertex_shader, fragment_shader) ||
        !DepthPass(gpu, vertex_shader, fragment_shader, NEAR_DEPTH, SECOND_COLOR,
                   DkCompareOp_Less, false)) {
        ReportCase(SUITE, NAME, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw was not recorded");
        return;
    }

    const uint8_t *const pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure
                                           : "the readback handed back nothing");
        return;
    }

    char detail[320];
    const bool matched = EveryPixelIs(pixels, SECOND_COLOR, detail, sizeof(detail));
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, matched, "%s", detail);
}

void RunDepth(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    // A 32 bit float depth buffer and no stencil, so the readback cases compare one number per
    // pixel with nothing packed alongside it.
    spec.depth_format = DkImageFormat_ZF32;
    spec.code_size = 16u * 1024u;
    spec.data_size = 64u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with a depth target", CHECK_NONE, opened, "%s",
               gpu.failure);
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

    ReportNote("depth target %ux%u ZF32, first quad at %.3f, near %.3f, far %.3f", TARGET_WIDTH,
               TARGET_HEIGHT, (double)FIRST_DEPTH, (double)NEAR_DEPTH, (double)FAR_DEPTH);

    // A depth buffer that holds what it was told to hold, before any test is asked about it.
    GpuClearDepth(&gpu, CLEARED_DEPTH);
    {
        const uint8_t *const depths = GpuReadBackDepth(&gpu);
        if (depths == NULL) {
            ReportCase(SUITE, "a depth clear writes the value it was given", CHECK_NONE, false,
                       gpu.failure[0] != '\0'
                           ? gpu.failure
                           : "the depth readback handed back nothing");
        } else {
            char detail[320];
            const bool matched = EveryDepthIs(depths, CLEARED_DEPTH, detail, sizeof(detail));
            CHECK_TRUE(SUITE, "a depth clear writes the value it was given", CHECK_EXACT, matched,
                       "%s", detail);
        }
    }

    // All eight compare operations the graphics library offers, because five of them would leave
    // three that an emulator could have wired to anything. The four that differ from another
    // only on an equal depth are challenged at exactly 0.5, which is where they differ: the
    // strict ones reject it and the inclusive ones take it, and 0.5 is exact on both sides of
    // the comparison so that really is an equality and not two numbers that came out close.
    // Each case lays the first quad down at 0.5 and then challenges it.
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "a nearer fragment passes a less than test", NEAR_DEPTH, DkCompareOp_Less,
                SECOND_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "a nearer fragment fails a greater than test", NEAR_DEPTH, DkCompareOp_Greater,
                FIRST_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "a nearer fragment fails an equality test", NEAR_DEPTH, DkCompareOp_Equal,
                FIRST_COLOR);
    // The equality that does pass. 0.5 is exact on both sides of the comparison, so this is a
    // real test of equality rather than of how close two numbers came out.
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "an equal depth passes an equality test", FIRST_DEPTH, DkCompareOp_Equal,
                SECOND_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "a farther fragment fails a less than test", FAR_DEPTH, DkCompareOp_Less,
                FIRST_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "a farther fragment passes a greater than test", FAR_DEPTH, DkCompareOp_Greater,
                SECOND_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader, "always lets the fragment through",
                FAR_DEPTH, DkCompareOp_Always, SECOND_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader, "never lets the fragment through",
                NEAR_DEPTH, DkCompareOp_Never, FIRST_COLOR);
    // The three that only an equal depth tells apart from the three above them. A machine that
    // had wired less than or equal to less than would pass every case before this one.
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "an equal depth passes a less than or equal test", FIRST_DEPTH,
                DkCompareOp_Lequal, SECOND_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "an equal depth passes a greater than or equal test", FIRST_DEPTH,
                DkCompareOp_Gequal, SECOND_COLOR);
    CompareCase(&gpu, &vertex_shader, &fragment_shader,
                "an equal depth fails a not equal test", FIRST_DEPTH, DkCompareOp_NotEqual,
                FIRST_COLOR);

    // What the buffer holds afterwards, which the colour cases above cannot see. A fragment that
    // passed and had the write open replaces the depth; one that passed with the write closed
    // leaves it alone, and its colour still lands, which is what separates the two.
    WriteCase(&gpu, &vertex_shader, &fragment_shader,
              "a fragment that passes with the write open replaces the depth", true, NEAR_DEPTH);
    WriteCase(&gpu, &vertex_shader, &fragment_shader,
              "a fragment that passes with the write closed leaves the depth", false,
              FIRST_DEPTH);
    ColorCase(&gpu, &vertex_shader, &fragment_shader);

    GpuClose(&gpu);
}
