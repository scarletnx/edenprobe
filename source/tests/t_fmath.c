// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../fmathref.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Float arithmetic in a fragment shader, into a target that stores 32 bit floats, compared
// against the same arithmetic in double precision on the processor.
//
// The only suite here where two correct machines are allowed to disagree, which is why it runs
// last. The budget belongs to each case rather than to the suite, because the specification's
// numbers differ per operation, and each case reports the budget it allowed and the worst
// distance it saw. The operands are whole numbers over 64 out of the fragment's coordinate, so
// every pixel is a different input and all of them are exactly representable.

static const char *const SUITE = "fmath";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)

// What the target is cleared to. No case produces it, and it is nowhere near any of the ranges
// they do produce, so a pixel the draw never reached is obvious rather than plausible.
#define SENTINEL (-12345.678f)

// The float the guard case sends through the uniform buffer and expects back. Three point one
// four one five nine two seven, which has bits set across all four bytes.
#define UNIFORM_PROBE 3.14159274f

#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

typedef struct {
    uint32_t mode;
    float probe;
    uint32_t padding[2];
} FloatParams;

static GpuVertex Corner(float x, float y) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = 0.0f;
    vertex.u = 0.0f;
    vertex.v = 0.0f;
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;
    return vertex;
}

static void FullScreenQuad(GpuVertex out[6]) {
    out[0] = Corner(-1.0f, 1.0f);
    out[1] = Corner(1.0f, 1.0f);
    out[2] = Corner(1.0f, -1.0f);
    out[3] = Corner(-1.0f, 1.0f);
    out[4] = Corner(1.0f, -1.0f);
    out[5] = Corner(-1.0f, -1.0f);
}

static float PixelValue(const uint8_t *pixels, uint32_t x, uint32_t y) {
    float value;
    memcpy(&value, pixels + ((size_t)y * TARGET_WIDTH + x) * 4u, sizeof(value));
    return value;
}

/// How many representable values apart two floats are, for the report.
///
/// The same mapping CheckFloatUlp uses, repeated here because the comparator answers yes or no
/// and a case that passes should still be able to say how much room it had. Returns a very large
/// number for anything involving a NaN, which is never within any budget.
static uint32_t UlpDistance(float produced, float expected) {
    uint32_t a;
    uint32_t b;
    memcpy(&a, &produced, sizeof(a));
    memcpy(&b, &expected, sizeof(b));
    if (!CheckFloatUlp(produced, expected, UINT32_MAX - 1u)) {
        return UINT32_MAX;
    }
    const uint32_t ordered_a = (a & 0x80000000u) ? (0x80000000u - (a & 0x7FFFFFFFu))
                                                 : (a + 0x80000000u);
    const uint32_t ordered_b = (b & 0x80000000u) ? (0x80000000u - (b & 0x7FFFFFFFu))
                                                 : (b + 0x80000000u);
    return ordered_a > ordered_b ? ordered_a - ordered_b : ordered_b - ordered_a;
}

/// Compares all 4096 pixels against the reference under the case's own budget.
static bool CheckEveryPixel(const uint8_t *pixels, FMathMode mode, FMathBudget budget,
                            char *detail, size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    float first_got = 0.0f;
    float first_want = 0.0f;
    uint32_t worst_ulps = 0;
    double worst_absolute = 0.0;

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const float got = PixelValue(pixels, x, y);
            const float want = FMathReference(mode, x, y, UNIFORM_PROBE);

            const bool within = budget.absolute ? CheckFloatAbs(got, want, budget.limit)
                                                : CheckFloatUlp(got, want, budget.ulps);
            if (budget.absolute) {
                const double difference = (double)got - (double)want;
                const double magnitude = difference < 0.0 ? -difference : difference;
                if (magnitude > worst_absolute) {
                    worst_absolute = magnitude;
                }
            } else {
                const uint32_t distance = UlpDistance(got, want);
                if (distance > worst_ulps) {
                    worst_ulps = distance;
                }
            }

            if (within) {
                continue;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                first_got = got;
                first_want = want;
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        if (budget.absolute) {
            snprintf(detail, detail_size, "worst was %.9g of an allowed %.9g", worst_absolute,
                     (double)budget.limit);
        } else {
            snprintf(detail, detail_size, "worst was %u of an allowed %u units in the last place",
                     (unsigned)worst_ulps, (unsigned)budget.ulps);
        }
        return true;
    }

    if (budget.absolute) {
        snprintf(detail, detail_size,
                 "%u of %u pixels outside %.9g, first at (%u,%u) is %.9g, expected %.9g; worst "
                 "was %.9g",
                 (unsigned)wrong, (unsigned)PIXEL_COUNT, (double)budget.limit, (unsigned)first_x,
                 (unsigned)first_y, (double)first_got, (double)first_want, worst_absolute);
    } else {
        snprintf(detail, detail_size,
                 "%u of %u pixels outside %u units in the last place, first at (%u,%u) is %.9g, "
                 "expected %.9g; worst was %u",
                 (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)budget.ulps, (unsigned)first_x,
                 (unsigned)first_y, (double)first_got, (double)first_want, (unsigned)worst_ulps);
    }
    return false;
}

static void ModeCase(GpuContext *gpu, const DkShader *vertex_shader,
                     const DkShader *fragment_shader, DkGpuAddr uniform, FMathMode mode) {
    const char *const name = FMathName(mode);
    const FMathBudget budget = FMathBudgetFor(mode);
    // A budget of nothing at all is the exact tier by another name: the case is claiming there
    // is one right bit pattern. Anything else is the units in the last place tier, whichever of
    // the two shapes the budget came in.
    const CheckTier tier =
        (!budget.absolute && budget.ulps == 0u) ? CHECK_EXACT : CHECK_ULP;

    GpuClear(gpu, SENTINEL, SENTINEL, SENTINEL, SENTINEL);

    GpuVertex vertices[6];
    FullScreenQuad(vertices);

    const uint32_t mark = GpuDataMark(gpu);
    GpuBeginPass(gpu);

    const FloatParams params = { (uint32_t)mode, UNIFORM_PROBE, { 0u, 0u } };
    dkCmdBufPushConstants(gpu->cmdbuf, uniform, UNIFORM_SIZE, 0, sizeof(params), &params);
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, uniform, UNIFORM_SIZE);

    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, 6);
    GpuSubmit(gpu);
    GpuDataRewind(gpu, mark);

    if (!drawn) {
        ReportCase(SUITE, name, tier, false, gpu->failure);
        return;
    }

    const uint8_t *const pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, tier, false,
                   gpu->ready ? "the readback handed back nothing" : gpu->failure);
        return;
    }

    char detail[384];
    const bool matched = CheckEveryPixel(pixels, mode, budget, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, tier, matched, "%s", detail);
}

void RunFmath(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    // R32_Float, so the shader's answer reaches memory with nothing normalising or clamping it.
    // A comparison in units in the last place is meaningless against a target that has already
    // thrown most of them away.
    spec.color_format = DkImageFormat_R32_Float;
    spec.code_size = 16u * 1024u;
    spec.data_size = 64u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with a float target", CHECK_NONE, opened, "%s",
               gpu.failure);
    if (!opened) {
        return;
    }

    DkShader vertex_shader;
    DkShader fragment_shader;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_FMATH_FRAG),
                                      ShaderSize(SHADER_FMATH_FRAG));
    CHECK_TRUE(SUITE, "the float shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }

    DkGpuAddr uniform = 0;
    const bool reserved =
        GpuAllocData(&gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &uniform) != NULL;
    CHECK_TRUE(SUITE, "the uniform buffer has somewhere to live", CHECK_NONE, reserved,
               "the scratch block had no room for %u aligned bytes", (unsigned)UNIFORM_SIZE);
    if (!reserved) {
        GpuClose(&gpu);
        return;
    }

    ReportNote("fmath target %ux%u R32_Float, operands %g to %g", TARGET_WIDTH, TARGET_HEIGHT,
               (double)FMathInputA(0), (double)FMathInputA(TARGET_WIDTH - 1u));
    for (uint32_t mode = 0; mode < FMATH_MODE_COUNT; ++mode) {
        const FMathBudget budget = FMathBudgetFor((FMathMode)mode);
        ReportNote("fmath budget for \"%s\": %s", FMathName((FMathMode)mode), budget.reason);
    }

    for (uint32_t mode = 0; mode < FMATH_MODE_COUNT; ++mode) {
        ModeCase(&gpu, &vertex_shader, &fragment_shader, uniform, (FMathMode)mode);
    }

    GpuClose(&gpu);
}
