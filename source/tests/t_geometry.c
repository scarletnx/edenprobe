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
//
// square.geom's early return compiles to PRET and RET, and quad.tese reads the levels back, and a
// translator can lack either and still have the stages. So each gets a twin that does neither,
// square_branch.geom and quad_color.tese, and the twins' cases tell "no such stage" apart from
// those two.
//
// The last four draw the patch with no control stage, which the console allows: the levels come
// from the list (dkCmdBufSetTessOuterLevels), and the evaluation stage reads the vertex stage's
// outputs straight. A host that needs a control stage has to make one up that passes everything
// on, the clip distances too: clip.vert.glsl writes all eight, the eighth minus one, and
// clip_last.tese passes the eighth on as its own first, which clips the whole patch, where
// clip_first.tese passes the first, one, and the patch is drawn.
//
// Then storage images in the stages before the fragment one: store.tese writes 0xa0 to 0xa2 into
// texels 0 to 2 of an R32_Uint image while the fragment stage samples texel 3 of the same image,
// which nothing writes; a draw after a barrier must fetch the three stored words; and store.vert
// writes 0xb0 plus the vertex index into texel index mod 4. A host that binds images per stage
// only for the fragment stage, or holds the image in a layout the second use cannot see, fails.
//
// And one storage buffer both stages before the fragment one bind: buffer_store.geom writes words
// 0 to 15 while buffer_read.vert, in the same draw, reads word 32 for the colour. A host that
// gives the geometry stage's global memory nothing of the draw's, or takes the two stages' views
// of the one buffer for two buffers, loses the stores or the colour (ScarletNX's draw storage
// test holds its renderer to the geometry stage's writes aliasing the vertex stage's reads).

static const char *const SUITE = "geometry";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define POINTS 4u

typedef struct {
    GpuContext gpu;
    DkShader vs;
    DkShader fs;
    DkShader gs;
    DkShader gs_branch;
    DkShader tcs;
    DkShader tes;
    DkShader tes_color;
    DkShader vs_clip;
    DkShader tes_clip_first;
    DkShader tes_clip_last;
    DkShader tes_store;
    DkShader vs_buffer;
    DkShader gs_buffer;
    DkShader vs_store;
    DkShader fs_texel;
    DkImage stage_image;
    DkImageLayout stage_layout;
    DkGpuAddr image_set;
    DkGpuAddr sampler_set;
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
static void SquaresCase(GeometryContext *g, const char *name, const DkShader *gs_shader,
                        const bool shown[POINTS]) {
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
        const DkShader *const gs[] = { gs_shader };
        dkCmdBufBindShaders(g->gpu.cmdbuf, DkStageFlag_Geometry, gs, 1);
        dkCmdBufDraw(g->gpu.cmdbuf, DkPrimitive_Points, POINTS, 1, 0, 0);
        Unbind(g);
    }
    GpuSubmit(&g->gpu);
    Judge(g, name, ok, rects, count);
}

/// The colour word buffer_read.vert reads, its bytes red to alpha.
#define SHARED_COLOUR 0xFF2A6B19u

/// A quad over the target, its vertex stage reading word 32 of a buffer its geometry stage writes
/// words 0 to 15 of: every pixel the colour, and the buffer holding the sixteen stores, the colour
/// and the other words as they were.
static void SharedBufferCase(GeometryContext *g) {
    static const char *const NAME =
        "a geometry stage's stores land beside the word the vertex stage reads in one buffer";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    DkGpuAddr address = 0;
    uint32_t *const words = (uint32_t *)GpuAllocData(&g->gpu, 64u * 4u, 256u, &address);
    if (words == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the buffer");
        return;
    }
    uint32_t want[64];
    for (uint32_t i = 0; i < 64u; ++i) {
        words[i] = 0x5A000000u + i;
        want[i] = i < 16u ? 0xC0DE0000u + i : words[i];
    }
    words[32] = SHARED_COLOUR;
    want[32] = SHARED_COLOUR;
    const uint8_t white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    const GpuVertex quad[6] = { At(0, 64, white), At(64, 64, white), At(64, 0, white),
                                At(0, 64, white), At(64, 0, white),  At(0, 0, white) };
    GpuClear(&g->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&g->gpu);
    bool ok = GpuBindTriangleState(&g->gpu, &g->vs_buffer, &g->fs, quad, 6);
    if (ok) {
        dkCmdBufBindStorageBuffer(g->gpu.cmdbuf, DkStage_Vertex, 0, address, 64u * 4u);
        dkCmdBufBindStorageBuffer(g->gpu.cmdbuf, DkStage_Geometry, 0, address, 64u * 4u);
        const DkShader *const gs[] = { &g->gs_buffer };
        dkCmdBufBindShaders(g->gpu.cmdbuf, DkStageFlag_Geometry, gs, 1);
        dkCmdBufDraw(g->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
        Unbind(g);
        dkCmdBufBarrier(g->gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    }
    GpuSubmit(&g->gpu);
    const uint8_t *const pixels = ok && g->gpu.ready ? GpuReadBack(&g->gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false,
                   g->gpu.failure[0] != '\0' ? g->gpu.failure : "the draw was not recorded");
        return;
    }
    uint32_t wrong_pixels = 0, wrong_words = 0, first_word = 0, seen_pixel = 0;
    for (uint32_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        uint32_t pixel = 0;
        memcpy(&pixel, pixels + i * 4u, 4);
        if (pixel != SHARED_COLOUR && wrong_pixels++ == 0u) {
            seen_pixel = pixel;
        }
    }
    for (uint32_t i = 0; i < 64u; ++i) {
        if (words[i] != want[i] && wrong_words++ == 0u) {
            first_word = i;
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, wrong_pixels == 0 && wrong_words == 0,
               "%u of %u pixels wrong, the first 0x%08x where the colour is 0x%08x; %u of 64 words "
               "wrong, the first, word %u, 0x%08x where 0x%08x belongs",
               (unsigned)wrong_pixels, (unsigned)(TARGET_WIDTH * TARGET_HEIGHT),
               (unsigned)seen_pixel, (unsigned)SHARED_COLOUR, (unsigned)wrong_words,
               (unsigned)first_word, (unsigned)words[first_word], (unsigned)want[first_word]);
}

/// A patch over the rectangle from (8,8) to (56,56), tessellated at @p level: by @p tcs, or
/// with no control stage (NULL) at the level the list sets. @p drawn false wants nothing drawn.
static void PatchCase(GeometryContext *g, const char *name, const DkShader *vs,
                      const DkShader *tcs, const DkShader *tes_shader, uint32_t level,
                      bool drawn) {
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
    bool ok = GpuBindTriangleState(&g->gpu, vs, &g->fs, corners, 4);
    if (ok) {
        const DkShader *const stages[] = { tcs != NULL ? tcs : tes_shader, tes_shader };
        dkCmdBufBindShaders(g->gpu.cmdbuf, DkStageFlag_TessCtrl | DkStageFlag_TessEval, stages,
                            tcs != NULL ? 2 : 1);
        if (tcs == NULL) {
            const float l = (float)level;
            dkCmdBufSetTessOuterLevels(g->gpu.cmdbuf, l, l, l, l);
            dkCmdBufSetTessInnerLevels(g->gpu.cmdbuf, l, l);
        }
        dkCmdBufSetPatchSize(g->gpu.cmdbuf, 4);
        dkCmdBufDraw(g->gpu.cmdbuf, DkPrimitive_Patches, 4, 1, 0, 0);
        Unbind(g);
    }
    GpuSubmit(&g->gpu);
    Judge(g, name, ok, &rect, drawn ? 1u : 0u);
}

/// The stage image's four texels before each list: 0x11, 0x22, 0x33 and 0x44.
static bool ResetStageImage(GeometryContext *g) {
    static const uint32_t start[4] = { 0x11, 0x22, 0x33, 0x44 };
    return GpuUploadTexture(&g->gpu, &g->stage_image, DkImageFormat_R32_Uint, 4, 1, start);
}

/// Binds the stage image's descriptors: texture 0 for the fragment stage, the image for @p stage.
static void BindStageImage(GeometryContext *g, DkStage stage) {
    dkCmdBufBindImageDescriptorSet(g->gpu.cmdbuf, g->image_set, 2);
    dkCmdBufBindSamplerDescriptorSet(g->gpu.cmdbuf, g->sampler_set, 1);
    dkCmdBufBindTexture(g->gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    dkCmdBufBindImage(g->gpu.cmdbuf, stage, 0, dkMakeImageHandle(1));
}

/// Judges the stage image's four texels against @p want.
static void JudgeStageImage(GeometryContext *g, const char *name, bool recorded,
                            const uint32_t want[4]) {
    const uint32_t *const got =
        recorded && g->gpu.ready
            ? (const uint32_t *)GpuReadBackImageBytes(&g->gpu, &g->stage_image, 4, 1, 4)
            : NULL;
    if (got == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   g->gpu.failure[0] != '\0' ? g->gpu.failure
                                              : "the list or readback did not happen");
        return;
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, memcmp(got, want, 16) == 0,
               "the texels hold %x %x %x %x, wanted %x %x %x %x", (unsigned)got[0],
               (unsigned)got[1], (unsigned)got[2], (unsigned)got[3], (unsigned)want[0],
               (unsigned)want[1], (unsigned)want[2], (unsigned)want[3]);
}

/// The patch from PatchCase at level 4 with store.tese, reading texel 3 in the fragment stage;
/// with @p after, then a barrier and three 8 by 6 quads along the bottom reading texels 0 to 2.
static bool StorePatch(GeometryContext *g, bool after) {
    const uint8_t colour[4] = { 3, 0, 0, 4 };
    const GpuVertex corners[4] = { At(8, 56, colour), At(56, 56, colour), At(56, 8, colour),
                                   At(8, 8, colour) };
    GpuClear(&g->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&g->gpu);
    BindStageImage(g, DkStage_TessEval);
    bool ok = GpuBindTriangleState(&g->gpu, &g->vs, &g->fs_texel, corners, 4);
    if (ok) {
        const DkShader *const stages[] = { &g->tcs, &g->tes_store };
        dkCmdBufBindShaders(g->gpu.cmdbuf, DkStageFlag_TessCtrl | DkStageFlag_TessEval, stages,
                            2);
        dkCmdBufSetPatchSize(g->gpu.cmdbuf, 4);
        dkCmdBufDraw(g->gpu.cmdbuf, DkPrimitive_Patches, 4, 1, 0, 0);
        Unbind(g);
    }
    if (ok && after) {
        dkCmdBufBarrier(g->gpu.cmdbuf, DkBarrier_Primitives, DkInvalidateFlags_Image);
        for (uint32_t i = 0; i < 3u && ok; ++i) {
            const uint8_t index[4] = { (uint8_t)i, 0, 0, 0xFF };
            const GpuVertex quad[6] = { At(8 * i, 64, index),     At(8 * i + 8, 64, index),
                                        At(8 * i + 8, 58, index), At(8 * i, 64, index),
                                        At(8 * i + 8, 58, index), At(8 * i, 58, index) };
            ok = GpuDrawTriangles(&g->gpu, &g->vs, &g->fs_texel, quad, 6);
        }
    }
    GpuSubmit(&g->gpu);
    return ok;
}

static void StageImageCases(GeometryContext *g) {
    static const char *const SAMPLED = "the fragment stage samples the texel an evaluation stage "
                                       "storing into its image left alone";
    static const char *const STORED = "an evaluation stage's stores land in its image";
    if (!ReportStart(SUITE, SAMPLED)) {
        ReportSkip(SUITE, STORED, REPORT_REASON_DEPENDS,
                   "judged from the list of the case before it, which did not run");
    } else {
        const bool ok = ResetStageImage(g) && StorePatch(g, false);
        const Rect rect = { 8, 8, 56, 56, { 0x44, 0, 0, 0xFF } };
        Judge(g, SAMPLED, ok, &rect, 1);
        if (ReportStart(SUITE, STORED)) {
            static const uint32_t want[4] = { 0xA0, 0xA1, 0xA2, 0x44 };
            JudgeStageImage(g, STORED, ok, want);
        }
    }
    static const char *const AFTER =
        "a draw after a barrier fetches what the evaluation stage stored";
    if (ReportStart(SUITE, AFTER)) {
        const bool ok = ResetStageImage(g) && StorePatch(g, true);
        const Rect rects[4] = { { 8, 8, 56, 56, { 0x44, 0, 0, 0xFF } },
                                { 0, 58, 8, 64, { 0xA0, 0, 0, 0xFF } },
                                { 8, 58, 16, 64, { 0xA1, 0, 0, 0xFF } },
                                { 16, 58, 24, 64, { 0xA2, 0, 0, 0xFF } } };
        Judge(g, AFTER, ok, rects, 4);
    }
    static const char *const VERTEX = "the vertex stage's stores land in its image";
    if (ReportStart(SUITE, VERTEX)) {
        bool ok = ResetStageImage(g);
        if (ok) {
            static const uint8_t colour[4] = { 0x40, 0x40, 0x40, 0xFF };
            const GpuVertex quad[6] = { At(0, 4, colour), At(4, 4, colour), At(4, 0, colour),
                                        At(0, 4, colour), At(4, 0, colour), At(0, 0, colour) };
            GpuBeginPass(&g->gpu);
            BindStageImage(g, DkStage_Vertex);
            ok = GpuDrawTriangles(&g->gpu, &g->vs_store, &g->fs, quad, 6);
            GpuSubmit(&g->gpu);
        }
        static const uint32_t want[4] = { 0xB0, 0xB1, 0xB2, 0xB3 };
        JudgeStageImage(g, VERTEX, ok, want);
    }
}

void RunGeometry(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 64u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.image_size = 256u * 1024u;

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
        GpuLoadShader(&g.gpu, &g.tes, ShaderData(SHADER_QUAD_TESE), ShaderSize(SHADER_QUAD_TESE)) &&
        GpuLoadShader(&g.gpu, &g.gs_branch, ShaderData(SHADER_SQUARE_BRANCH_GEOM),
                      ShaderSize(SHADER_SQUARE_BRANCH_GEOM)) &&
        GpuLoadShader(&g.gpu, &g.tes_color, ShaderData(SHADER_QUAD_COLOR_TESE),
                      ShaderSize(SHADER_QUAD_COLOR_TESE)) &&
        GpuLoadShader(&g.gpu, &g.vs_clip, ShaderData(SHADER_CLIP_VERT),
                      ShaderSize(SHADER_CLIP_VERT)) &&
        GpuLoadShader(&g.gpu, &g.tes_clip_first, ShaderData(SHADER_CLIP_FIRST_TESE),
                      ShaderSize(SHADER_CLIP_FIRST_TESE)) &&
        GpuLoadShader(&g.gpu, &g.tes_clip_last, ShaderData(SHADER_CLIP_LAST_TESE),
                      ShaderSize(SHADER_CLIP_LAST_TESE)) &&
        GpuLoadShader(&g.gpu, &g.tes_store, ShaderData(SHADER_STORE_TESE),
                      ShaderSize(SHADER_STORE_TESE)) &&
        GpuLoadShader(&g.gpu, &g.vs_store, ShaderData(SHADER_STORE_VERT),
                      ShaderSize(SHADER_STORE_VERT)) &&
        GpuLoadShader(&g.gpu, &g.fs_texel, ShaderData(SHADER_FETCH_TEXEL_FRAG),
                      ShaderSize(SHADER_FETCH_TEXEL_FRAG)) &&
        GpuLoadShader(&g.gpu, &g.vs_buffer, ShaderData(SHADER_BUFFER_READ_VERT),
                      ShaderSize(SHADER_BUFFER_READ_VERT)) &&
        GpuLoadShader(&g.gpu, &g.gs_buffer, ShaderData(SHADER_BUFFER_STORE_GEOM),
                      ShaderSize(SHADER_BUFFER_STORE_GEOM));
    CHECK_TRUE(SUITE, "the geometry and tessellation shaders load", CHECK_NONE, loaded, "%s",
               g.gpu.failure);
    if (!loaded) {
        GpuClose(&g.gpu);
        return;
    }

    static const bool all[POINTS] = { true, true, true, true };
    static const bool some[POINTS] = { true, false, false, true };
    SquaresCase(&g, "a geometry shader turns each of four points into its 8 by 8 square", &g.gs,
                all);
    SquaresCase(&g, "a point the geometry shader emits nothing for draws nothing", &g.gs, some);
    PatchCase(&g, "a patch tessellated at level 4 covers its rectangle, and says 4", &g.vs, &g.tcs,
              &g.tes, 4u, true);
    PatchCase(&g, "the same patch at level 7 covers the same rectangle, and says 7", &g.vs, &g.tcs,
              &g.tes, 7u, true);
    SquaresCase(&g, "a geometry shader with no early return squares the shown points only",
                &g.gs_branch, some);
    PatchCase(&g, "a patch whose evaluation stage reads no level covers its rectangle at 4",
              &g.vs, &g.tcs, &g.tes_color, 4u, true);
    PatchCase(&g, "a patch with no control stage covers its rectangle at the list's level 5",
              &g.vs, NULL, &g.tes_color, 5u, true);
    PatchCase(&g, "an evaluation stage with no control stage reads the list's level 5", &g.vs,
              NULL, &g.tes, 5u, true);
    PatchCase(&g, "a positive first clip distance passed on by the evaluation stage clips nothing",
              &g.vs_clip, NULL, &g.tes_clip_first, 4u, true);
    PatchCase(&g, "a negative eighth clip distance passed on by the evaluation stage clips the "
                  "patch",
              &g.vs_clip, NULL, &g.tes_clip_last, 4u, false);
    SharedBufferCase(&g);

    if (!ReportStart(SUITE, "the stage image and its descriptors are in place")) {
        GpuClose(&g.gpu);
        return;
    }
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        &g.gpu, 2u * (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT,
        &g.image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        &g.gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
        &g.sampler_set);
    const bool made = images != NULL && samplers != NULL &&
                      GpuCreateImage(&g.gpu, &g.stage_image, &g.stage_layout,
                                     DkImageFormat_R32_Uint, 4, 1, DkImageFlags_UsageLoadStore, 0,
                                     NULL);
    if (made) {
        DkImageView view;
        dkImageViewDefaults(&view, &g.stage_image);
        dkImageDescriptorInitialize(&images[0], &view, false, false);
        dkImageDescriptorInitialize(&images[1], &view, true, false);
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    }
    CHECK_TRUE(SUITE, "the stage image and its descriptors are in place", CHECK_NONE, made, "%s",
               g.gpu.failure[0] != '\0' ? g.gpu.failure : "the scratch block had no room");
    if (made) {
        StageImageCases(&g);
    }
    GpuClose(&g.gpu);
}
