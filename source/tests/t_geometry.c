// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The two optional stages between the vertex and the fragment: a geometry shader and a
// tessellation pair. Every other suite draws with the vertex and fragment stages alone, so this
// is the one a host without geometry or tessellation shaders has to emulate to pass, and the
// one where an emulator that assumes the host has them shows what it does when it does not.
//
// square.geom turns each point into an 8 by 8 square, or into nothing when the point's alpha is
// nought. quad.tesc and quad.tese spread a four point patch over its corners' rectangle at a
// level the patch carries, and hand the level they saw to the fragment in the alpha. Every edge
// lands on a pixel edge, so every pixel is exact: inside, the colour; outside, the clear.

static const char *const SUITE = "geometry";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define POINTS 4u

typedef struct {
    GpuContext gpu;
    DkShader vs;
    DkShader fs;
    DkShader gs;
    DkShader tcs;
    DkShader tes;
} GeometryContext;

typedef struct {
    uint32_t x0, y0, x1, y1;
    uint8_t colour[4];
} Rect;

static const uint8_t COLOURS[POINTS][4] = {
    { 0xE0, 0x30, 0x10, 0xFF },
    { 0x20, 0xD0, 0x40, 0xFF },
    { 0x10, 0x50, 0xF0, 0xFF },
    { 0xC0, 0xB0, 0x20, 0xFF },
};

/// A vertex at pixel @p x, @p y, rows from the top as the readback's are.
static GpuVertex At(uint32_t x, uint32_t y, const uint8_t colour[4]) {
    GpuVertex v;
    v.x = 2.0f * (float)x / (float)TARGET_WIDTH - 1.0f;
    v.y = 1.0f - 2.0f * (float)y / (float)TARGET_HEIGHT;
    v.z = 0.5f;
    v.u = 0.0f;
    v.v = 0.0f;
    v.r = (float)colour[0] / 255.0f;
    v.g = (float)colour[1] / 255.0f;
    v.b = (float)colour[2] / 255.0f;
    v.a = (float)colour[3] / 255.0f;
    return v;
}

/// Judges the whole target: each pixel inside one of @p rects holds its colour, every other
/// one the clear, nought.
static void Judge(GeometryContext *g, const char *name, bool recorded, const Rect *rects,
                  uint32_t count) {
    const uint8_t *const pixels = recorded && g->gpu.ready ? GpuReadBack(&g->gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   g->gpu.failure[0] != '\0' ? g->gpu.failure
                                                : "the list or readback did not happen");
        return;
    }
    static const uint8_t cleared[4] = { 0, 0, 0, 0 };
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong_inside = 0;
    uint32_t wrong_outside = 0;
    uint32_t first = TARGET_WIDTH * TARGET_HEIGHT;
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *want = cleared;
            for (uint32_t r = 0; r < count; ++r) {
                if (x >= rects[r].x0 && x < rects[r].x1 && y >= rects[r].y0 && y < rects[r].y1) {
                    want = rects[r].colour;
                }
            }
            const bool right = memcmp(GpuPixel(pixels, TARGET_WIDTH, x, y), want, 4) == 0;
            PictureSet(picture, x, y, want, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                if (want == cleared) {
                    ++wrong_outside;
                } else {
                    ++wrong_inside;
                }
                if (first == TARGET_WIDTH * TARGET_HEIGHT) {
                    first = y * TARGET_WIDTH + x;
                }
            }
        }
    }
    const uint8_t *const p =
        first < TARGET_WIDTH * TARGET_HEIGHT ? pixels + (size_t)first * 4u : cleared;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong_inside == 0 && wrong_outside == 0,
               "%u pixels wrong where something is drawn, %u where nothing is; first at (%u,%u) "
               "is %02x%02x%02x%02x",
               (unsigned)wrong_inside, (unsigned)wrong_outside,
               (unsigned)(first % TARGET_WIDTH), (unsigned)(first / TARGET_WIDTH), p[0], p[1],
               p[2], p[3]);
}

static void Unbind(GeometryContext *g) {
    dkCmdBufBindShaders(g->gpu.cmdbuf,
                        DkStageFlag_Geometry | DkStageFlag_TessCtrl | DkStageFlag_TessEval, NULL,
                        0);
}

/// Four points, one per quadrant, each at the centre of an 8 by 8 square; @p shown says which
/// get a square, and the others go in with an alpha of nought.
static void SquaresCase(GeometryContext *g, const char *name, const bool shown[POINTS]) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    static const uint32_t centres[POINTS][2] = { { 16, 16 }, { 48, 16 }, { 16, 48 }, { 48, 48 } };
    GpuVertex points[POINTS];
    Rect rects[POINTS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < POINTS; ++i) {
        uint8_t colour[4];
        memcpy(colour, COLOURS[i], 4);
        colour[3] = shown[i] ? 0xFF : 0x00;
        points[i] = At(centres[i][0], centres[i][1], colour);
        if (shown[i]) {
            rects[count++] = (Rect){ centres[i][0] - 4, centres[i][1] - 4, centres[i][0] + 4,
                                     centres[i][1] + 4, { colour[0], colour[1], colour[2], 0xFF } };
        }
    }
    GpuClear(&g->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&g->gpu);
    bool ok = GpuBindTriangleState(&g->gpu, &g->vs, &g->fs, points, POINTS);
    if (ok) {
        const DkShader *const gs[] = { &g->gs };
        dkCmdBufBindShaders(g->gpu.cmdbuf, DkStageFlag_Geometry, gs, 1);
        dkCmdBufDraw(g->gpu.cmdbuf, DkPrimitive_Points, POINTS, 1, 0, 0);
        Unbind(g);
    }
    GpuSubmit(&g->gpu);
    Judge(g, name, ok, rects, count);
}

/// A patch over the rectangle from (8,8) to (56,56), tessellated at @p level.
static void PatchCase(GeometryContext *g, const char *name, uint32_t level) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const uint8_t colour[4] = { 0x5C, 0x2A, 0xB4, (uint8_t)level };
    // Bottom left, bottom right, top right, top left: quad.tese spreads the first two along the
    // bottom and the last two along the top.
    const GpuVertex corners[4] = { At(8, 56, colour), At(56, 56, colour), At(56, 8, colour),
                                   At(8, 8, colour) };
    const Rect rect = { 8, 8, 56, 56, { colour[0], colour[1], colour[2], (uint8_t)level } };
    GpuClear(&g->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&g->gpu);
    bool ok = GpuBindTriangleState(&g->gpu, &g->vs, &g->fs, corners, 4);
    if (ok) {
        const DkShader *const stages[] = { &g->tcs, &g->tes };
        dkCmdBufBindShaders(g->gpu.cmdbuf, DkStageFlag_TessCtrl | DkStageFlag_TessEval, stages,
                            2);
        dkCmdBufSetPatchSize(g->gpu.cmdbuf, 4);
        dkCmdBufDraw(g->gpu.cmdbuf, DkPrimitive_Patches, 4, 1, 0, 0);
        Unbind(g);
    }
    GpuSubmit(&g->gpu);
    Judge(g, name, ok, &rect, 1);
}

void RunGeometry(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 64u * 1024u;
    spec.data_size = 64u * 1024u;

    if (!ReportStart(SUITE, "graphics device opens")) {
        return;
    }
    GeometryContext g;
    const bool opened = GpuOpenSpec(&g.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens", CHECK_NONE, opened, "%s", g.gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the geometry and tessellation shaders load")) {
        GpuClose(&g.gpu);
        return;
    }
    const bool loaded =
        GpuLoadShader(&g.gpu, &g.vs, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&g.gpu, &g.fs, ShaderData(SHADER_FLAT_FRAG), ShaderSize(SHADER_FLAT_FRAG)) &&
        GpuLoadShader(&g.gpu, &g.gs, ShaderData(SHADER_SQUARE_GEOM),
                      ShaderSize(SHADER_SQUARE_GEOM)) &&
        GpuLoadShader(&g.gpu, &g.tcs, ShaderData(SHADER_QUAD_TESC), ShaderSize(SHADER_QUAD_TESC)) &&
        GpuLoadShader(&g.gpu, &g.tes, ShaderData(SHADER_QUAD_TESE), ShaderSize(SHADER_QUAD_TESE));
    CHECK_TRUE(SUITE, "the geometry and tessellation shaders load", CHECK_NONE, loaded, "%s",
               g.gpu.failure);
    if (!loaded) {
        GpuClose(&g.gpu);
        return;
    }

    static const bool all[POINTS] = { true, true, true, true };
    static const bool some[POINTS] = { true, false, false, true };
    SquaresCase(&g, "a geometry shader turns each of four points into its 8 by 8 square", all);
    SquaresCase(&g, "a point the geometry shader emits nothing for draws nothing", some);
    PatchCase(&g, "a patch tessellated at level 4 covers its rectangle, and says 4", 4u);
    PatchCase(&g, "the same patch at level 7 covers the same rectangle, and says 7", 7u);
    GpuClose(&g.gpu);
}
