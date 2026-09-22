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
// Every case lays one quad over the whole target at a known depth, then a second at another
// depth under one compare operation, and asks what colour came out. Two colours per pixel and
// nothing that rounds, so it is exact.
//
// All eight compare operations are asked at all three depths, which is the smallest number that
// works: three depths give eight patterns of pass and fail, one per operation, so leaving a
// depth out lets a pair agree everywhere the suite looks. The case after the shaders load works
// all of it out on the processor and checks the eight really do differ.
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

/// The eight compare operations, each with the word that goes in a case's name.
static const struct {
    DkCompareOp op;
    const char *name;
} COMPARISONS[] = {
    { DkCompareOp_Never, "never" },
    { DkCompareOp_Less, "less than" },
    { DkCompareOp_Equal, "equal" },
    { DkCompareOp_Lequal, "less than or equal" },
    { DkCompareOp_Greater, "greater than" },
    { DkCompareOp_NotEqual, "not equal" },
    { DkCompareOp_Gequal, "greater than or equal" },
    { DkCompareOp_Always, "always" },
};

/// The three depths the second quad is drawn at, against a first quad at FIRST_DEPTH, and how a
/// case names where it was.
static const struct {
    float depth;
    const char *where;
} CHALLENGES[] = {
    { NEAR_DEPTH, "nearer than the buffer" },
    { FIRST_DEPTH, "at the buffer's own depth" },
    { FAR_DEPTH, "farther than the buffer" },
};

/// What the operation means, on the processor. The only statement of it in this file, so the
/// expected colour of every case and the argument for how many cases there are both come from
/// here.
static bool ComparePasses(DkCompareOp op, float fragment, float buffer) {
    switch (op) {
    case DkCompareOp_Never:
        return false;
    case DkCompareOp_Less:
        return fragment < buffer;
    case DkCompareOp_Equal:
        return fragment == buffer;
    case DkCompareOp_Lequal:
        return fragment <= buffer;
    case DkCompareOp_Greater:
        return fragment > buffer;
    case DkCompareOp_NotEqual:
        return fragment != buffer;
    case DkCompareOp_Gequal:
        return fragment >= buffer;
    case DkCompareOp_Always:
        return true;
    }
    return false;
}

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

    // The argument for the number of cases below, checked rather than asserted: at these three
    // depths the eight operations give eight different patterns, so each is told apart from
    // every other.
    uint32_t indistinguishable = 0;
    const char *first_pair = "";
    const char *second_pair = "";
    for (size_t a = 0; a < sizeof(COMPARISONS) / sizeof(COMPARISONS[0]); ++a) {
        for (size_t b = a + 1; b < sizeof(COMPARISONS) / sizeof(COMPARISONS[0]); ++b) {
            bool same = true;
            for (size_t at = 0; at < sizeof(CHALLENGES) / sizeof(CHALLENGES[0]); ++at) {
                if (ComparePasses(COMPARISONS[a].op, CHALLENGES[at].depth, FIRST_DEPTH) !=
                    ComparePasses(COMPARISONS[b].op, CHALLENGES[at].depth, FIRST_DEPTH)) {
                    same = false;
                    break;
                }
            }
            if (same) {
                if (indistinguishable == 0) {
                    first_pair = COMPARISONS[a].name;
                    second_pair = COMPARISONS[b].name;
                }
                ++indistinguishable;
            }
        }
    }
    CHECK_TRUE(SUITE, "these three depths tell all eight comparisons apart", CHECK_NONE,
               indistinguishable == 0, "%u pairs agree everywhere, first was \"%s\" and \"%s\"",
               (unsigned)indistinguishable, first_pair, second_pair);

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

    // Every operation at every depth. The names come out as sentences a reader can check
    // against the table above without running anything: "a fragment nearer than the buffer
    // passes a less than test".
    for (size_t op = 0; op < sizeof(COMPARISONS) / sizeof(COMPARISONS[0]); ++op) {
        for (size_t at = 0; at < sizeof(CHALLENGES) / sizeof(CHALLENGES[0]); ++at) {
            const bool passes =
                ComparePasses(COMPARISONS[op].op, CHALLENGES[at].depth, FIRST_DEPTH);
            // "an equal test", "a less than test": the article follows the operation's name.
            const char *const article =
                strchr("aeiou", COMPARISONS[op].name[0]) != NULL ? "an" : "a";
            char name[128];
            snprintf(name, sizeof(name), "a fragment %s %s %s %s test", CHALLENGES[at].where,
                     passes ? "passes" : "fails", article, COMPARISONS[op].name);
            CompareCase(&gpu, &vertex_shader, &fragment_shader, name, CHALLENGES[at].depth,
                        COMPARISONS[op].op, passes ? SECOND_COLOR : FIRST_COLOR);
        }
    }

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
