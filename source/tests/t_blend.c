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
//
// Two facts about the alpha channel are measured here rather than assumed, because the graphics
// library settles neither: whether the alpha channel runs an equation of its own, and whether an
// alpha destination factor of its own reaches the unit.

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

/// Clears to @p destination, draws @p source over the whole target under @p spec, and hands the
/// pixels back.
///
/// NULL when nothing was drawn or nothing came back, with the sentence explaining it in
/// @p stopped_by, which is only the fallback: the context's own failure text is better whenever
/// it has one.
static const uint8_t *BlendDraw(GpuContext *gpu, const DkShader *vertex_shader,
                                const DkShader *fragment_shader, const BlendSpec *spec,
                                const uint8_t source[4], const uint8_t destination[4],
                                const char **stopped_by) {
    // The destination arrives by clearing, which the clear suite has already established writes
    // exactly the byte a normalised float asks for.
    GpuClear(gpu, BlendUnormToFloat(destination[0]), BlendUnormToFloat(destination[1]),
             BlendUnormToFloat(destination[2]), BlendUnormToFloat(destination[3]));

    GpuVertex vertices[6];
    FullScreenQuad(vertices, source);

    GpuBeginPass(gpu);

    DkColorState color_state;
    dkColorStateDefaults(&color_state);
    dkColorStateSetBlendEnable(&color_state, 0, spec->enabled);
    dkCmdBufBindColorState(gpu->cmdbuf, &color_state);

    DkBlendState blend;
    dkBlendStateDefaults(&blend);
    dkBlendStateSetOps(&blend, HardwareOp(spec->color_op), HardwareOp(spec->alpha_op));
    dkBlendStateSetFactors(&blend, HardwareFactor(spec->color_source),
                           HardwareFactor(spec->color_destination),
                           HardwareFactor(spec->alpha_source),
                           HardwareFactor(spec->alpha_destination));
    dkCmdBufBindBlendState(gpu->cmdbuf, 0, &blend);
    dkCmdBufSetBlendConst(gpu->cmdbuf, spec->constant[0], spec->constant[1], spec->constant[2],
                          spec->constant[3]);

    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, 6);
    GpuSubmit(gpu);

    if (!drawn) {
        *stopped_by = "the draw was not recorded";
        return NULL;
    }

    const uint8_t *const pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        *stopped_by = "the readback handed back nothing";
    }
    return pixels;
}

// The operands both probes below draw with. A whole 32 rides on the destination alpha, so a
// factor or an equation behaving differently moves the answer by 32 and nothing rounds.
static const uint8_t PROBE_SOURCE[4] = { 0x20, 0x20, 0x20, 0x40 };
static const uint8_t PROBE_DESTINATION[4] = { 0x40, 0x60, 0x80, 0x20 };

/// The alpha byte one probe draw leaves at the corner, or -1 with the reason already reported.
static int ProbeAlpha(GpuContext *gpu, const DkShader *vertex_shader,
                      const DkShader *fragment_shader, const BlendSpec *spec,
                      const char *measuring) {
    const char *stopped_by = "";
    const uint8_t *const pixels = BlendDraw(gpu, vertex_shader, fragment_shader, spec,
                                            PROBE_SOURCE, PROBE_DESTINATION, &stopped_by);
    if (pixels == NULL) {
        ReportNote("blend could not measure %s: %s", measuring,
                   gpu->failure[0] != '\0' ? gpu->failure : stopped_by);
        return -1;
    }
    return (int)GpuPixel(pixels, TARGET_WIDTH, 0, 0)[3];
}

/// Whether the alpha channel runs an equation of its own, which every case that tells the alpha
/// state apart from the colour one rests on.
///
/// Measured because nothing decides it here: no call in the library writes the register that
/// turns separate alpha blending on, so the answer is whatever the machine left there. Two draws
/// that differ in nothing but the alpha equation, one of which answers the same either way and
/// is the anchor.
static bool NoteAlphaEquationIsItsOwn(GpuContext *gpu, const DkShader *vertex_shader,
                                      const DkShader *fragment_shader) {
    static const char *const MEASURING = "whether the alpha channel has an equation of its own";
    BlendSpec spec = { true,
                       BLEND_OP_ADD,
                       BLEND_FACTOR_ONE,
                       BLEND_FACTOR_ONE,
                       BLEND_OP_ADD,
                       BLEND_FACTOR_ONE,
                       BLEND_FACTOR_ONE,
                       { 0.0f, 0.0f, 0.0f, 0.0f } };

    const int adding = ProbeAlpha(gpu, vertex_shader, fragment_shader, &spec, MEASURING);
    if (adding < 0) {
        return false;
    }
    spec.alpha_op = BLEND_OP_MAX;
    const int largest = ProbeAlpha(gpu, vertex_shader, fragment_shader, &spec, MEASURING);
    if (largest < 0) {
        return false;
    }

    const int sum = (int)PROBE_SOURCE[3] + (int)PROBE_DESTINATION[3];
    if (adding != sum) {
        ReportNote("blend cannot say %s: adding both sides of an alpha of %u over %u left %u "
                   "where %d was the only right answer either way",
                   MEASURING, (unsigned)PROBE_SOURCE[3], (unsigned)PROBE_DESTINATION[3],
                   (unsigned)adding, sum);
        return false;
    }
    if (largest == adding) {
        ReportNote("blend: the alpha channel has no equation of its own, so it is running the "
                   "colour one. Adding and the maximum both left %u where the maximum alone "
                   "gives %u. Nothing in the graphics library turns separate alpha blending on, "
                   "and any case below that tells the alpha state apart from the colour state "
                   "fails for that reason rather than for anything the blend unit got wrong",
                   (unsigned)adding, (unsigned)PROBE_SOURCE[3]);
        return false;
    }
    ReportNote("blend: the alpha channel has an equation of its own, adding leaving %u and the "
               "maximum %u, so the cases that tell the alpha state apart from the colour state "
               "mean what they say",
               (unsigned)adding, (unsigned)largest);
    return true;
}

/// Whether an alpha destination factor of its own reaches the blend unit.
///
/// A note and not a case: the answer is a property of the library on the way to the hardware,
/// and a pass would claim something about a machine nobody asked. Two draws differing in nothing
/// but that factor, with the colour destination factor held at nought, so a library that
/// substitutes it makes them identical. The one place allowed to build the state the reference
/// suite forbids a case.
///
/// @p alpha_is_separate is what the probe above found: without it the two draws agreeing has two
/// explanations rather than one.
static void NoteAlphaDestinationReach(GpuContext *gpu, const DkShader *vertex_shader,
                                      const DkShader *fragment_shader, bool alpha_is_separate) {
    static const char *const MEASURING = "whether the alpha destination factor arrives";
    BlendSpec spec = { true,
                       BLEND_OP_ADD,
                       BLEND_FACTOR_ONE,
                       BLEND_FACTOR_ZERO,
                       BLEND_OP_ADD,
                       BLEND_FACTOR_ONE,
                       BLEND_FACTOR_ZERO,
                       { 0.0f, 0.0f, 0.0f, 0.0f } };

    const int weighted_to_nothing = ProbeAlpha(gpu, vertex_shader, fragment_shader, &spec,
                                               MEASURING);
    if (weighted_to_nothing < 0) {
        return;
    }
    spec.alpha_destination = BLEND_FACTOR_ONE;
    const int weighted_whole = ProbeAlpha(gpu, vertex_shader, fragment_shader, &spec, MEASURING);
    if (weighted_whole < 0) {
        return;
    }

    if (weighted_to_nothing != (int)PROBE_SOURCE[3]) {
        ReportNote("blend cannot say %s: a factor of nought over a destination alpha of %u left "
                   "%u where the source alpha %u was the only right answer either way",
                   MEASURING, (unsigned)PROBE_DESTINATION[3], (unsigned)weighted_to_nothing,
                   (unsigned)PROBE_SOURCE[3]);
        return;
    }
    if (weighted_whole != weighted_to_nothing) {
        ReportNote("blend: the alpha destination factor now reaches the blend unit, nought "
                   "leaving %u and one leaving %u. The graphics library used to send the colour "
                   "destination factor in its place; the cases in blendref.c and the rule in the "
                   "reference suite are shaped around that and can be lifted",
                   (unsigned)weighted_to_nothing, (unsigned)weighted_whole);
        return;
    }
    if (!alpha_is_separate) {
        ReportNote("blend: the alpha destination factor made no difference, nought and one both "
                   "leaving %u, but with no alpha equation of its own that is what an alpha "
                   "channel running the colour state looks like too, so this run cannot say "
                   "which of the two it is",
                   (unsigned)weighted_whole);
        return;
    }
    ReportNote("blend: the alpha destination factor does not reach the blend unit, so no case "
               "gives it one of its own. Nought and one both left %u, and with the colour "
               "destination factor at nought in both draws, one that arrived would have added "
               "the destination alpha %u",
               (unsigned)weighted_whole, (unsigned)PROBE_DESTINATION[3]);
}

static void BlendOneCase(GpuContext *gpu, const DkShader *vertex_shader,
                         const DkShader *fragment_shader, const BlendCase *test) {
    uint8_t expected[4];
    BlendReference(&test->spec, test->source, test->destination, expected);

    const char *stopped_by = "";
    const uint8_t *const pixels = BlendDraw(gpu, vertex_shader, fragment_shader, &test->spec,
                                            test->source, test->destination, &stopped_by);
    if (pixels == NULL) {
        ReportCase(SUITE, test->name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : stopped_by);
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

    const bool alpha_is_separate =
        NoteAlphaEquationIsItsOwn(&gpu, &vertex_shader, &fragment_shader);
    NoteAlphaDestinationReach(&gpu, &vertex_shader, &fragment_shader, alpha_is_separate);

    for (size_t index = 0; index < BlendCaseCount(); ++index) {
        BlendOneCase(&gpu, &vertex_shader, &fragment_shader, BlendCaseAt(index));
    }

    GpuClose(&gpu);
}
