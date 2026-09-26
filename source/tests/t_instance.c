// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../rawref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The 3D class's instanced vertex array draws, VERTEX_ARRAY_INSTANCE_FIRST and _SUBSEQUENT,
// which draw the moment they arrive: FIRST draws instance nought, and each SUBSEQUENT draws the
// same range again as the next instance. The library never sends them, so they go in as raw
// methods between the library's own commands.
//
// The target is four regions side by side; instance.vert moves each instance one region to the
// right, and the fragment writes the first word of a uniform buffer the library rewrites between
// draws with dkCmdBufPushConstants. So a region says which draw reached it, with which uniform
// value, as which instance.

static const char *const SUITE = "instance";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 16u
#define REGION_WIDTH 16u
#define REGIONS 4u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

#define VALUE_BEFORE 0x3A5C11E7u
#define VALUE_AFTER 0x94B62D0Fu

// The topology field's value for a triangle list, as in the library's DkPrimitive.
#define TOPOLOGY_TRIANGLES 4u

typedef struct {
    GpuContext gpu;
    DkShader instance;
    DkShader value_ubo;
    DkGpuAddr vertices;
    DkGpuAddr uniform;
} InstanceContext;

/// What each region should hold after a case: a word, or nothing (the clear, nought).
typedef struct {
    bool drawn;
    uint32_t word;
} RegionWant;

static void Bind(InstanceContext *c) {
    const DkShader *const shaders[] = { &c->instance, &c->value_ubo };
    dkCmdBufBindShaders(c->gpu.cmdbuf, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
    static const DkVtxAttribState attribute = { .bufferId = 0,
                                                .isFixed = 0,
                                                .offset = 0,
                                                .size = DkVtxAttribSize_3x32,
                                                .type = DkVtxAttribType_Float,
                                                .isBgra = 0 };
    static const DkVtxBufferState buffer_state = { 12, 0 };
    dkCmdBufBindVtxAttribState(c->gpu.cmdbuf, &attribute, 1);
    dkCmdBufBindVtxBufferState(c->gpu.cmdbuf, &buffer_state, 1);
    dkCmdBufBindVtxBuffer(c->gpu.cmdbuf, 0, c->vertices, 12u * 12u);
    dkCmdBufBindUniformBuffer(c->gpu.cmdbuf, DkStage_Fragment, 0, c->uniform, UNIFORM_SIZE);
}

static void SetValue(InstanceContext *c, uint32_t word) {
    const uint32_t data[4] = { word, 0u, 0u, 0u };
    dkCmdBufPushConstants(c->gpu.cmdbuf, c->uniform, UNIFORM_SIZE, 0, sizeof(data), data);
}

/// Records the raw instanced draw of vertices [@p first, @p first + 6).
static bool RawInstanced(InstanceContext *c, uint32_t method, uint32_t first) {
    const uint32_t words[2] = { RawIncreasingHeader(RAW_SUBCHANNEL_3D, method, 1),
                                RawInstanceArrayWord(TOPOLOGY_TRIANGLES, first, 6) };
    return GpuRawMethods(&c->gpu, words, 2);
}

/// Judges the four regions against @p want; @p only limits the verdict to the regions set.
static void JudgeRegions(InstanceContext *c, const char *name, const RegionWant want[REGIONS],
                         uint32_t only, bool recorded) {
    if (!recorded || !c->gpu.ready) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "the list was not recorded");
        return;
    }
    const uint8_t *const pixels = GpuReadBack(&c->gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "the readback handed back nothing");
        return;
    }
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong[REGIONS] = { 0, 0, 0, 0 };
    uint32_t seen[REGIONS] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint32_t r = x / REGION_WIDTH;
            const uint32_t word = want[r].drawn ? want[r].word : 0u;
            const uint8_t expect[4] = { (uint8_t)word, (uint8_t)(word >> 8),
                                        (uint8_t)(word >> 16), (uint8_t)(word >> 24) };
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            const bool right = memcmp(pixel, expect, 4) == 0;
            const bool judged = (only & (1u << r)) != 0u;
            PictureSet(picture, x, y, expect,
                       !judged ? PICTURE_UNJUDGED : right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (judged && !right) {
                if (wrong[r] == 0) {
                    seen[r] = (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) |
                              ((uint32_t)pixel[2] << 16) | ((uint32_t)pixel[3] << 24);
                }
                ++wrong[r];
            }
        }
    }
    const uint32_t total = wrong[0] + wrong[1] + wrong[2] + wrong[3];
    CHECK_TRUE(SUITE, name, CHECK_EXACT, total == 0,
               "wrong pixels per region %u/%u/%u/%u; first word seen per region "
               "%08x/%08x/%08x/%08x; wanted %08x/%08x/%08x/%08x (0 = not drawn)",
               (unsigned)wrong[0], (unsigned)wrong[1], (unsigned)wrong[2], (unsigned)wrong[3],
               (unsigned)seen[0], (unsigned)seen[1], (unsigned)seen[2], (unsigned)seen[3],
               (unsigned)(want[0].drawn ? want[0].word : 0u),
               (unsigned)(want[1].drawn ? want[1].word : 0u),
               (unsigned)(want[2].drawn ? want[2].word : 0u),
               (unsigned)(want[3].drawn ? want[3].word : 0u));
}

/// FIRST over region 0 with the value before, the uniform rewritten, then a library draw of the
/// vertices over region 2 with the value after.
static bool RecordFirstThenDraw(InstanceContext *c) {
    GpuClear(&c->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginList(&c->gpu);
    Bind(c);
    SetValue(c, VALUE_BEFORE);
    bool ok = RawInstanced(c, RAW_3D_VERTEX_ARRAY_INSTANCE_FIRST, 0);
    SetValue(c, VALUE_AFTER);
    dkCmdBufDraw(c->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 6, 0);
    GpuSubmit(&c->gpu);
    return ok;
}

void RunInstance(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;

    if (!ReportStart(SUITE, "graphics device opens")) {
        return;
    }
    InstanceContext c;
    const bool opened = GpuOpenSpec(&c.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens", CHECK_NONE, opened, "%s", c.gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the drawing shaders load")) {
        GpuClose(&c.gpu);
        return;
    }
    const bool loaded = GpuLoadShader(&c.gpu, &c.instance, ShaderData(SHADER_INSTANCE_VERT),
                                      ShaderSize(SHADER_INSTANCE_VERT)) &&
                        GpuLoadShader(&c.gpu, &c.value_ubo, ShaderData(SHADER_VALUE_UBO_FRAG),
                                      ShaderSize(SHADER_VALUE_UBO_FRAG));
    CHECK_TRUE(SUITE, "the drawing shaders load", CHECK_NONE, loaded, "%s", c.gpu.failure);
    if (!loaded) {
        GpuClose(&c.gpu);
        return;
    }
    float *const vertices = (float *)GpuAllocData(&c.gpu, 12u * 12u, 256, &c.vertices);
    uint32_t *const uniform =
        (uint32_t *)GpuAllocData(&c.gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &c.uniform);
    if (vertices == NULL || uniform == NULL) {
        if (ReportStart(SUITE, "the vertices and the uniform buffer have somewhere to live")) {
            ReportCase(SUITE, "the vertices and the uniform buffer have somewhere to live",
                       CHECK_NONE, false, "no scratch left");
        }
        GpuClose(&c.gpu);
        return;
    }
    memset(uniform, 0, UNIFORM_SIZE);
    // Vertices 0 to 5 cover region 0 (clip x from -1 to -0.5), 6 to 11 region 2 (0 to 0.5).
    const float lefts[2] = { -1.0f, 0.0f };
    for (uint32_t q = 0; q < 2u; ++q) {
        const float x0 = lefts[q];
        const float x1 = lefts[q] + 0.5f;
        const float quad[6][3] = { { x0, -1.0f, 0.0f }, { x1, -1.0f, 0.0f }, { x1, 1.0f, 0.0f },
                                   { x0, -1.0f, 0.0f }, { x1, 1.0f, 0.0f },  { x0, 1.0f, 0.0f } };
        memcpy(vertices + q * 18u, quad, sizeof(quad));
    }

    if (ReportStart(SUITE, "an instanced array draw uses the uniform value set before it")) {
        const bool ok = RecordFirstThenDraw(&c);
        const RegionWant want[REGIONS] = {
            { true, VALUE_BEFORE }, { false, 0 }, { true, VALUE_AFTER }, { false, 0 }
        };
        JudgeRegions(&c, "an instanced array draw uses the uniform value set before it", want,
                     1u << 0, ok);
    }
    if (ReportStart(SUITE, "a draw after an instanced array draw adds no instances of its own")) {
        const bool ok = RecordFirstThenDraw(&c);
        const RegionWant want[REGIONS] = {
            { true, VALUE_BEFORE }, { false, 0 }, { true, VALUE_AFTER }, { false, 0 }
        };
        JudgeRegions(&c, "a draw after an instanced array draw adds no instances of its own",
                     want, (1u << 1) | (1u << 2) | (1u << 3), ok);
    }
    if (ReportStart(SUITE, "each subsequent instanced array draw lands as the next instance")) {
        GpuClear(&c.gpu, 0.0f, 0.0f, 0.0f, 0.0f);
        GpuBeginList(&c.gpu);
        Bind(&c);
        SetValue(&c, VALUE_BEFORE);
        bool ok = RawInstanced(&c, RAW_3D_VERTEX_ARRAY_INSTANCE_FIRST, 0);
        for (uint32_t i = 0; i < 3u && ok; ++i) {
            ok = RawInstanced(&c, RAW_3D_VERTEX_ARRAY_INSTANCE_SUBSEQUENT, 0);
        }
        GpuSubmit(&c.gpu);
        const RegionWant want[REGIONS] = { { true, VALUE_BEFORE },
                                           { true, VALUE_BEFORE },
                                           { true, VALUE_BEFORE },
                                           { true, VALUE_BEFORE } };
        JudgeRegions(&c, "each subsequent instanced array draw lands as the next instance", want,
                     0xFu, ok);
    }

    GpuClose(&c.gpu);
}
