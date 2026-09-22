// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/// Compares all 4096 pixels against the reference under the case's own budget.
///
/// The budget is asked for per pixel, not once, because one of them is not a constant: the power
/// of two's allowance grows with its argument, so a case that took the middle of the sweep would
/// fail a correct machine at the ends and one that took the ends would wave the middle through.
static bool CheckEveryPixel(const uint8_t *pixels, FMathMode mode, FMathBudget budget,
                            char *detail, size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    float first_got = 0.0f;
    float first_want = 0.0f;
    float first_allowed = 0.0f;
    // The worst pixel is the one that used the largest share of its own allowance, and the two
    // numbers reported are that pixel's. A largest error and a largest share taken separately
    // would read as one pixel and be two.
    float worst_share = 0.0f;
    float worst_share_error = 0.0f;
    float worst_share_allowed = 0.0f;
    float tightest = 0.0f;
    float loosest = 0.0f;
    double worst_absolute = 0.0;

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const float got = PixelValue(pixels, x, y);
            const float want = FMathReference(mode, x, y, UNIFORM_PROBE);

            bool within;
            float allowed = 0.0f;
            if (budget.absolute) {
                within = CheckFloatAbs(got, want, budget.limit);
                const double difference = (double)got - (double)want;
                const double magnitude = difference < 0.0 ? -difference : difference;
                if (magnitude > worst_absolute) {
                    worst_absolute = magnitude;
                }
            } else {
                allowed = FMathAllowedUlps(mode, x, y);
                const float error = CheckFloatUlpError(got, want);
                within = error <= allowed;
                // How much of its own allowance this pixel used, which is the number that means
                // the same thing across a sweep whose allowance moves.
                const float share =
                    allowed > 0.0f ? error / allowed : (error > 0.0f ? 1.0e30f : 0.0f);
                if (share > worst_share) {
                    worst_share = share;
                    worst_share_error = error;
                    worst_share_allowed = allowed;
                }
                if (x == 0 && y == 0) {
                    tightest = allowed;
                    loosest = allowed;
                }
                if (allowed < tightest) {
                    tightest = allowed;
                }
                if (allowed > loosest) {
                    loosest = allowed;
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
                first_allowed = allowed;
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        if (budget.absolute) {
            snprintf(detail, detail_size, "worst was %.9g of an allowed %.9g", worst_absolute,
                     (double)budget.limit);
        } else if (tightest != loosest) {
            snprintf(detail, detail_size,
                     "the worst pixel used %.4f of its allowance, %.4f units in the last place "
                     "where %g were allowed; the allowance ran from %g to %g",
                     (double)worst_share, (double)worst_share_error,
                     (double)worst_share_allowed, (double)tightest, (double)loosest);
        } else {
            snprintf(detail, detail_size,
                     "worst was %.4f of an allowed %g units in the last place",
                     (double)worst_share_error, (double)tightest);
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
                 "%u of %u pixels outside their allowance, first at (%u,%u) is %.9g, expected "
                 "%.9g, where %g units in the last place were allowed; the worst pixel used "
                 "%.4f of its allowance",
                 (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)first_x, (unsigned)first_y,
                 (double)first_got, (double)first_want, (double)first_allowed,
                 (double)worst_share);
    }
    return false;
}

static void ModeCase(GpuContext *gpu, const DkShader *vertex_shader,
                     const DkShader *fragment_shader, DkGpuAddr uniform, FMathMode mode) {
    const char *const name = FMathName(mode);
    const FMathBudget budget = FMathBudgetFor(mode);

    // Every case reports the units in the last place tier, including the two whose budget is
    // nought: a tolerance of nought is still a tolerance, and a float shader never qualifies as
    // exact however tidy the numbers look on one machine. The guard case is the exception, since
    // it computes nothing and only moves a bit pattern through a uniform buffer.
    const CheckTier tier = mode == FMATH_UNIFORM ? CHECK_EXACT : CHECK_ULP;

    GpuClear(gpu, SENTINEL, SENTINEL, SENTINEL, SENTINEL);

    GpuVertex vertices[6];
    FullScreenQuad(vertices);

    GpuBeginPass(gpu);

    const FloatParams params = { (uint32_t)mode, UNIFORM_PROBE, { 0u, 0u } };
    dkCmdBufPushConstants(gpu->cmdbuf, uniform, UNIFORM_SIZE, 0, sizeof(params), &params);
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, uniform, UNIFORM_SIZE);

    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, 6);
    GpuSubmit(gpu);

    if (!drawn) {
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
        ReportNote(FMATH_BUDGET_NOTE_FORMAT, FMathName((FMathMode)mode), budget.reason);
    }

    for (uint32_t mode = 0; mode < FMATH_MODE_COUNT; ++mode) {
        ModeCase(&gpu, &vertex_shader, &fragment_shader, uniform, (FMathMode)mode);
    }

    GpuClose(&gpu);
}
