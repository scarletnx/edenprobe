// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "../stencilref.h"
#include "tests.h"

// The stencil test, the stencil operations and the depth a fragment shader writes, on the two
// depth formats that carry stencil: Z24S8 and ZF32_X24S8, the second the one a 32-bit float
// depth with stencil is (D32F/S8).
//
// The target is 32 by 32 in three bands of rows, 0-9, 10-19 and 20-31, each cleared to a stencil
// and a depth of its own, so one draw over the whole target meets three situations at once. A
// stencil value is never read back as bytes: how the two parts of a Z24S8 word sit is a question
// the zeta suite leaves to a console. Instead each band is checked by a draw with the test set to
// EQUAL against the value the band should hold, which marks the pixels that hold it; the compare
// cases, which come first, are what that leans on.
//
// The last cases are the ones a translator gets wrong when it moves the depth and stencil tests:
// a depth the fragment shader writes is tested after the shader, so the shader runs, side effects
// and all, for every fragment the tests then reject; with early fragment tests it runs only for
// the ones that pass (ARB_shader_image_load_store, "Early Fragment Tests"). A counter the shader
// adds to says how many ran.

static const char *const SUITE = "stencil";

#define SIZE 32u
#define BANDS 3u
static const uint32_t BAND_TOP[BANDS + 1] = { 0, 10, 20, SIZE };

static const DkImageFormat FORMATS[2] = { DkImageFormat_Z24S8, DkImageFormat_ZF32_X24S8 };
static const char *const FORMAT_NAMES[2] = { "z24s8", "zf32_x24s8" };

static const uint8_t BLACK[4] = { 0, 0, 0, 0 };
static const uint8_t GREEN[4] = { 0x20, 0xE0, 0x40, 0xFF };
static const uint8_t MARK[4] = { 0x30, 0x50, 0xD0, 0xFF };
static const uint8_t OTHER_MARK[4] = { 0xD0, 0x50, 0x30, 0xFF };

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader flat;
    DkShader depth_out;
    DkShader count_late;
    DkShader count_early;
    DkImage ds[2];
    DkImageLayout ds_layout[2];
    DkGpuAddr counter_addr;
    volatile uint32_t *counter;
} StencilContext;

/// Two triangles over rows [y0, y1) of the whole width, in @p colour at clip depth @p z. With
/// @p clockwise the corners run the other way round, the same pixels with the other face.
static void BandQuad(GpuVertex out[6], uint32_t y0, uint32_t y1, const uint8_t colour[4], float z,
                     float alpha, bool clockwise) {
    const float top = 1.0f - 2.0f * (float)y0 / (float)SIZE;
    const float bottom = 1.0f - 2.0f * (float)y1 / (float)SIZE;
    const float ccw[6][2] = { { -1.0f, bottom }, { 1.0f, bottom }, { 1.0f, top },
                              { -1.0f, bottom }, { 1.0f, top },    { -1.0f, top } };
    for (uint32_t i = 0; i < 6u; ++i) {
        const uint32_t k = clockwise ? 5u - i : i;
        out[i] = (GpuVertex){ ccw[k][0],           ccw[k][1],           z,
                              0.0f,                0.0f,                colour[0] / 255.0f,
                              colour[1] / 255.0f,  colour[2] / 255.0f,  alpha };
    }
}

static void Scissor(StencilContext *c, uint32_t y0, uint32_t y1) {
    const DkScissor scissor = { 0, y0, SIZE, y1 - y0 };
    dkCmdBufSetScissors(c->gpu.cmdbuf, 0, &scissor, 1);
}

/// Opens a list on the colour target and depth target @p f, both cleared: the colour to nought,
/// band b to stencil @p stencil[b] and depth @p depth[b].
static void Begin(StencilContext *c, uint32_t f, const uint8_t stencil[BANDS],
                  const float depth[BANDS]) {
    GpuBeginPass(&c->gpu);
    DkImageView colour;
    DkImageView ds;
    dkImageViewDefaults(&colour, &c->gpu.image);
    dkImageViewDefaults(&ds, &c->ds[f]);
    dkCmdBufBindRenderTarget(c->gpu.cmdbuf, &colour, &ds);
    dkCmdBufClearColorFloat(c->gpu.cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 0.0f);
    // The library writes the depth write enable only along with an enabled test (deko3d
    // source/maxwell/state.mme, BindDepthStencilState), so the test goes on, at ALWAYS, for the
    // clears to land.
    DkDepthStencilState writing;
    dkDepthStencilStateDefaults(&writing);
    writing.depthTestEnable = 1;
    writing.depthWriteEnable = 1;
    writing.depthCompareOp = DkCompareOp_Always;
    writing.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &writing);
    for (uint32_t b = 0; b < BANDS; ++b) {
        Scissor(c, BAND_TOP[b], BAND_TOP[b + 1]);
        dkCmdBufClearDepthStencil(c->gpu.cmdbuf, true, depth[b], 0xFF, stencil[b]);
    }
    Scissor(c, 0, SIZE);
}

/// Binds a depth and stencil state: the depth test with @p depth_op (off for DkCompareOp_Never
/// passed as 0), no depth write, and the stencil test with @p compare and the three ops on both
/// faces.
static void BindTest(StencilContext *c, uint32_t depth_op, StencilCompare compare, StencilOp fail,
                     StencilOp depth_fail, StencilOp pass) {
    DkDepthStencilState state;
    dkDepthStencilStateDefaults(&state);
    state.depthTestEnable = depth_op != 0u;
    state.depthWriteEnable = 0;
    state.depthCompareOp = depth_op != 0u ? (DkCompareOp)depth_op : DkCompareOp_Always;
    state.stencilTestEnable = 1;
    state.stencilFrontFailOp = (DkStencilOp)fail;
    state.stencilFrontDepthFailOp = (DkStencilOp)depth_fail;
    state.stencilFrontPassOp = (DkStencilOp)pass;
    state.stencilFrontCompareOp = (DkCompareOp)compare;
    state.stencilBackFailOp = (DkStencilOp)fail;
    state.stencilBackDepthFailOp = (DkStencilOp)depth_fail;
    state.stencilBackPassOp = (DkStencilOp)pass;
    state.stencilBackCompareOp = (DkCompareOp)compare;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &state);
}

static bool DrawBand(StencilContext *c, const DkShader *fragment, uint32_t y0, uint32_t y1,
                     const uint8_t colour[4], float z, float alpha, bool clockwise) {
    GpuVertex quad[6];
    BandQuad(quad, y0, y1, colour, z, alpha, clockwise);
    return GpuDrawTriangles(&c->gpu, &c->vertex, fragment, quad, 6);
}

/// Records the marking draws: band b's pixels that hold @p want[b] turn MARK. Writes nothing to
/// the stencil, and the depth test is off.
static bool Mark(StencilContext *c, const uint8_t want[BANDS], const uint8_t colour[4]) {
    bool ok = true;
    BindTest(c, 0, STENCIL_EQUAL, STENCIL_KEEP, STENCIL_KEEP, STENCIL_KEEP);
    for (uint32_t b = 0; b < BANDS; ++b) {
        dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0x00, want[b], 0xFF);
        Scissor(c, BAND_TOP[b], BAND_TOP[b + 1]);
        ok = ok && DrawBand(c, &c->flat, BAND_TOP[b], BAND_TOP[b + 1], colour, 0.5f, 1.0f, false);
    }
    Scissor(c, 0, SIZE);
    return ok;
}

static uint32_t BandOf(uint32_t y) {
    uint32_t b = 0;
    while (b + 1u < BANDS && y >= BAND_TOP[b + 1]) {
        ++b;
    }
    return b;
}

/// Judges the rows of the bands in @p bands (bit b for band b) against @p expect, the colour each
/// band should show.
static void Judge(const uint8_t *pixels, const char *name, uint32_t bands,
                  const uint8_t *const expect[BANDS], const char *what) {
    Picture *const picture = PictureBegin(SIZE, SIZE, PICTURE_COLOUR, pixels);
    uint32_t judged = 0;
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < SIZE; ++y) {
        const uint32_t b = BandOf(y);
        for (uint32_t x = 0; x < SIZE; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, SIZE, x, y);
            const bool mine = (bands >> b & 1u) != 0u;
            const bool right = memcmp(pixel, expect[b], 4) == 0;
            PictureSet(picture, x, y, expect[b],
                       !mine ? PICTURE_UNJUDGED : right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!mine) {
                continue;
            }
            ++judged;
            if (!right && wrong++ == 0u) {
                first_x = x;
                first_y = y;
                memcpy(seen, pixel, 4);
            }
        }
    }
    const uint32_t band = BandOf(first_y);
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u,
               "%u of %u pixels wrong; (%u, %u) in band %u holds %02x%02x%02x%02x, wanted "
               "%02x%02x%02x%02x (%s)",
               (unsigned)wrong, (unsigned)judged, (unsigned)first_x, (unsigned)first_y,
               (unsigned)band, seen[0], seen[1], seen[2], seen[3], expect[band][0],
               expect[band][1], expect[band][2], expect[band][3], what);
}

/// Submits the open list and reads the colour back; NULL with the case failed as @p name when
/// either did not happen.
static const uint8_t *Finish(StencilContext *c, bool recorded, const char *name) {
    GpuSubmit(&c->gpu);
    const uint8_t *const pixels = recorded && c->gpu.ready ? GpuReadBack(&c->gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure
                                              : "the list or the readback did not happen");
    }
    return pixels;
}

static void CompareCase(StencilContext *c, uint32_t f, StencilCompare op) {
    char name[96];
    snprintf(name, sizeof(name), "%s: %s against 0x80 passes the bands it should",
             FORMAT_NAMES[f], StencilCompareName(op));
    if (!ReportStart(SUITE, name)) {
        return;
    }
    static const float far[BANDS] = { 1.0f, 1.0f, 1.0f };
    Begin(c, f, STENCIL_COMPARE_BANDS, far);
    BindTest(c, 0, op, STENCIL_KEEP, STENCIL_KEEP, STENCIL_KEEP);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0x00, STENCIL_COMPARE_REF, 0xFF);
    const bool ok = DrawBand(c, &c->flat, 0, SIZE, GREEN, 0.5f, 1.0f, false);
    const uint8_t *const pixels = Finish(c, ok, name);
    if (pixels == NULL) {
        return;
    }
    const uint8_t *expect[BANDS];
    for (uint32_t b = 0; b < BANDS; ++b) {
        expect[b] = StencilPasses(op, STENCIL_COMPARE_REF, STENCIL_COMPARE_BANDS[b], 0xFF) ? GREEN
                                                                                       : BLACK;
    }
    Judge(pixels, name, 7u, expect, "green where the reference passes, nought where it fails");
}

/// One op case: GREATER against 0x80 over the bands 0xa5 (fails), 0x5a with a nearer depth
/// (passes the stencil, fails LESS) and 0x5a (passes both), with @p fail, @p depth_fail and
/// @p pass, each band then marked where it holds what its op leaves.
static void OpCase(StencilContext *c, uint32_t f, StencilOp fail, StencilOp depth_fail,
                   StencilOp pass) {
    char name[128];
    snprintf(name, sizeof(name), "%s: a failing stencil takes %s, a failing depth %s, a pass %s",
             FORMAT_NAMES[f], StencilOpName(fail), StencilOpName(depth_fail), StencilOpName(pass));
    if (!ReportStart(SUITE, name)) {
        return;
    }
    static const uint8_t stored[BANDS] = { STENCIL_OP_FAILING, STENCIL_OP_PASSING,
                                           STENCIL_OP_PASSING };
    static const float depth[BANDS] = { 1.0f, 0.25f, 1.0f };
    Begin(c, f, stored, depth);
    BindTest(c, DkCompareOp_Less, STENCIL_GREATER, fail, depth_fail, pass);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0xFF, STENCIL_OP_REF, 0xFF);
    bool ok = DrawBand(c, &c->flat, 0, SIZE, GREEN, 0.5f, 1.0f, false);
    const uint8_t want[BANDS] = {
        StencilApply(fail, stored[0], STENCIL_OP_REF, 0xFF),
        StencilApply(depth_fail, stored[1], STENCIL_OP_REF, 0xFF),
        StencilApply(pass, stored[2], STENCIL_OP_REF, 0xFF),
    };
    ok = ok && Mark(c, want, MARK);
    const uint8_t *const pixels = Finish(c, ok, name);
    if (pixels == NULL) {
        return;
    }
    const uint8_t *const expect[BANDS] = { MARK, MARK, MARK };
    char what[64];
    snprintf(what, sizeof(what), "bands should hold %02x %02x %02x", want[0], want[1], want[2]);
    Judge(pixels, name, 7u, expect, what);
}

/// The ends of the range: band 0 holds @p start and takes @p first, band 1 holds it and takes
/// @p second, each drawn on its own with ALWAYS.
static void BoundaryCase(StencilContext *c, uint32_t f, uint8_t start, StencilOp first,
                         StencilOp second) {
    char name[96];
    snprintf(name, sizeof(name), "%s: %s and %s at 0x%02x", FORMAT_NAMES[f], StencilOpName(first),
             StencilOpName(second), start);
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const uint8_t stored[BANDS] = { start, start, 0x33 };
    static const float far[BANDS] = { 1.0f, 1.0f, 1.0f };
    Begin(c, f, stored, far);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0xFF, 0x00, 0xFF);
    bool ok = true;
    const StencilOp ops[2] = { first, second };
    for (uint32_t b = 0; b < 2u; ++b) {
        BindTest(c, 0, STENCIL_ALWAYS, STENCIL_KEEP, STENCIL_KEEP, ops[b]);
        Scissor(c, BAND_TOP[b], BAND_TOP[b + 1]);
        ok = ok && DrawBand(c, &c->flat, BAND_TOP[b], BAND_TOP[b + 1], GREEN, 0.5f, 1.0f, false);
    }
    Scissor(c, 0, SIZE);
    const uint8_t want[BANDS] = { StencilApply(first, start, 0, 0xFF),
                                  StencilApply(second, start, 0, 0xFF), 0x33 };
    ok = ok && Mark(c, want, MARK);
    const uint8_t *const pixels = Finish(c, ok, name);
    if (pixels == NULL) {
        return;
    }
    const uint8_t *const expect[BANDS] = { MARK, MARK, MARK };
    char what[64];
    snprintf(what, sizeof(what), "bands should hold %02x %02x %02x", want[0], want[1], want[2]);
    Judge(pixels, name, 3u, expect, what);
}

/// The two masks, one list: band 0 holds 0x0f and is inverted through the write mask 0xf0; band
/// 1 holds 0x5a and passes EQUAL against 0x3a under the compare mask 0x1f, then takes REPLACE;
/// band 2 holds 0x5a and fails the same compare without the mask, keeping it.
static void MaskCases(StencilContext *c, uint32_t f) {
    char write_name[96];
    char compare_name[96];
    snprintf(write_name, sizeof(write_name), "%s: the write mask keeps the bits it closes",
             FORMAT_NAMES[f]);
    snprintf(compare_name, sizeof(compare_name),
             "%s: the compare mask applies to the reference and the stored value", FORMAT_NAMES[f]);
    if (!ReportStart(SUITE, write_name)) {
        ReportSkip(SUITE, compare_name, REPORT_REASON_DEPENDS,
                   "judged from the list of the case before it, which did not run");
        return;
    }
    static const uint8_t stored[BANDS] = { 0x0F, 0x5A, 0x5A };
    static const float far[BANDS] = { 1.0f, 1.0f, 1.0f };
    Begin(c, f, stored, far);
    BindTest(c, 0, STENCIL_ALWAYS, STENCIL_KEEP, STENCIL_KEEP, STENCIL_INVERT);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0xF0, 0x00, 0xFF);
    Scissor(c, BAND_TOP[0], BAND_TOP[1]);
    bool ok = DrawBand(c, &c->flat, BAND_TOP[0], BAND_TOP[1], GREEN, 0.5f, 1.0f, false);
    BindTest(c, 0, STENCIL_EQUAL, STENCIL_KEEP, STENCIL_KEEP, STENCIL_REPLACE);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0xFF, 0x3A, 0x1F);
    Scissor(c, BAND_TOP[1], BAND_TOP[2]);
    ok = ok && DrawBand(c, &c->flat, BAND_TOP[1], BAND_TOP[2], GREEN, 0.5f, 1.0f, false);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0xFF, 0x3A, 0xFF);
    Scissor(c, BAND_TOP[2], BAND_TOP[3]);
    ok = ok && DrawBand(c, &c->flat, BAND_TOP[2], BAND_TOP[3], GREEN, 0.5f, 1.0f, false);
    Scissor(c, 0, SIZE);
    const uint8_t want[BANDS] = { StencilApply(STENCIL_INVERT, 0x0F, 0, 0xF0), 0x3A, 0x5A };
    ok = ok && Mark(c, want, MARK);
    const uint8_t *const pixels = Finish(c, ok, write_name);
    if (pixels == NULL) {
        if (ReportStart(SUITE, compare_name)) {
            ReportCase(SUITE, compare_name, CHECK_NONE, false, "the list did not run");
        }
        return;
    }
    const uint8_t *const expect[BANDS] = { MARK, MARK, MARK };
    Judge(pixels, write_name, 1u, expect, "0x0f inverted under 0xf0 should be 0xff");
    if (ReportStart(SUITE, compare_name)) {
        Judge(pixels, compare_name, 6u, expect,
              "0x5a replaced by 0x3a where the masked compare passes, kept where the unmasked "
              "fails");
    }
}

/// The two faces: rows 0-15 wound one way and 16-31 the other, each REPLACE with ALWAYS, the
/// front reference 0x11 and the back 0x22. Which winding is front depends on the window origin
/// the library sets up, so the case asks only that each half takes one face's whole state and
/// the two halves differ; a note says which was front.
static void FaceCase(StencilContext *c, uint32_t f) {
    char name[96];
    snprintf(name, sizeof(name), "%s: each winding takes its own face's reference",
             FORMAT_NAMES[f]);
    if (!ReportStart(SUITE, name)) {
        return;
    }
    static const uint8_t stored[BANDS] = { 0, 0, 0 };
    static const float far[BANDS] = { 1.0f, 1.0f, 1.0f };
    Begin(c, f, stored, far);
    BindTest(c, 0, STENCIL_ALWAYS, STENCIL_KEEP, STENCIL_KEEP, STENCIL_REPLACE);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_Front, 0xFF, 0x11, 0xFF);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_Back, 0xFF, 0x22, 0xFF);
    bool ok = DrawBand(c, &c->flat, 0, SIZE / 2u, GREEN, 0.5f, 1.0f, false) &&
              DrawBand(c, &c->flat, SIZE / 2u, SIZE, GREEN, 0.5f, 1.0f, true);
    // Mark 0x11 with one colour and 0x22 with the other over the whole target.
    BindTest(c, 0, STENCIL_EQUAL, STENCIL_KEEP, STENCIL_KEEP, STENCIL_KEEP);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0x00, 0x11, 0xFF);
    ok = ok && DrawBand(c, &c->flat, 0, SIZE, MARK, 0.5f, 1.0f, false);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0x00, 0x22, 0xFF);
    ok = ok && DrawBand(c, &c->flat, 0, SIZE, OTHER_MARK, 0.5f, 1.0f, false);
    const uint8_t *const pixels = Finish(c, ok, name);
    if (pixels == NULL) {
        return;
    }
    const uint8_t *const top = GpuPixel(pixels, SIZE, 0, 0);
    const bool top_front = memcmp(top, MARK, 4) == 0;
    const uint8_t *const upper = top_front ? MARK : OTHER_MARK;
    const uint8_t *const lower = top_front ? OTHER_MARK : MARK;
    uint32_t wrong = 0;
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            wrong += memcmp(GpuPixel(pixels, SIZE, x, y), y < SIZE / 2u ? upper : lower, 4) != 0;
        }
    }
    const bool top_marked = top_front || memcmp(top, OTHER_MARK, 4) == 0;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, top_marked && wrong == 0u,
               "%u of %u pixels do not hold their half's reference; the top left holds "
               "%02x%02x%02x%02x, where 0x11 marks %02x%02x%02x%02x and 0x22 %02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)(SIZE * SIZE), top[0], top[1], top[2], top[3], MARK[0],
               MARK[1], MARK[2], MARK[3], OTHER_MARK[0], OTHER_MARK[1], OTHER_MARK[2],
               OTHER_MARK[3]);
    if (top_marked && wrong == 0u) {
        ReportNote("stencil %s: the quad wound counter-clockwise in clip space (rows 0-15) took "
                   "the %s face's state",
                   FORMAT_NAMES[f], top_front ? "front" : "back");
    }
}

/// A depth written by the fragment shader: rows 0-15 drawn at vertex depth 0.25 with the shader
/// writing 0.75, rows 16-31 the other way round, over a depth of 0.5 under LESS, with ALWAYS,
/// depth fail INCR and pass REPLACE 0x40 over a stencil of 0x10.
static void ShaderDepthCases(StencilContext *c, uint32_t f) {
    char colour_name[96];
    char stencil_name[112];
    snprintf(colour_name, sizeof(colour_name),
             "%s: the depth the fragment shader writes is the one tested", FORMAT_NAMES[f]);
    snprintf(stencil_name, sizeof(stencil_name),
             "%s: the depth fail op follows the depth the fragment shader wrote", FORMAT_NAMES[f]);
    if (!ReportStart(SUITE, colour_name)) {
        ReportSkip(SUITE, stencil_name, REPORT_REASON_DEPENDS,
                   "judged from the list of the case before it, which did not run");
        return;
    }
    static const uint8_t stored[BANDS] = { 0x10, 0x10, 0x10 };
    static const float middle[BANDS] = { 0.5f, 0.5f, 0.5f };
    Begin(c, f, stored, middle);
    BindTest(c, DkCompareOp_Less, STENCIL_ALWAYS, STENCIL_KEEP, STENCIL_INCR, STENCIL_REPLACE);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0xFF, 0x40, 0xFF);
    // The shader takes its depth from the vertex colour's alpha.
    bool ok = DrawBand(c, &c->depth_out, 0, SIZE / 2u, GREEN, 0.25f, 0.75f, false) &&
              DrawBand(c, &c->depth_out, SIZE / 2u, SIZE, GREEN, 0.75f, 0.25f, false);
    GpuSubmit(&c->gpu);
    const uint8_t *pixels = ok && c->gpu.ready ? GpuReadBack(&c->gpu) : NULL;
    if (pixels == NULL) {
        const char *why = c->gpu.failure[0] != '\0' ? c->gpu.failure : "the list did not run";
        ReportCase(SUITE, colour_name, CHECK_NONE, false, why);
        if (ReportStart(SUITE, stencil_name)) {
            ReportCase(SUITE, stencil_name, CHECK_NONE, false, why);
        }
        return;
    }
    // Judged by halves, which do not follow the bands.
    uint32_t wrong = 0;
    uint32_t first_y = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, SIZE, x, y);
            const uint8_t *const want = y < SIZE / 2u ? BLACK : GREEN;
            if (memcmp(pixel, want, 4) != 0 && wrong++ == 0u) {
                first_y = y;
                memcpy(seen, pixel, 4);
            }
        }
    }
    CHECK_TRUE(SUITE, colour_name, CHECK_EXACT, wrong == 0u,
               "%u of %u pixels wrong; row %u holds %02x%02x%02x%02x: the top half should be "
               "rejected (shader depth 0.75) and the bottom drawn (0.25)",
               (unsigned)wrong, (unsigned)(SIZE * SIZE), (unsigned)first_y, seen[0], seen[1],
               seen[2], seen[3]);
    if (!ReportStart(SUITE, stencil_name)) {
        return;
    }
    // A second list over what the first left: the top half should hold 0x11, the bottom 0x40.
    GpuBeginPass(&c->gpu);
    DkImageView colour;
    DkImageView ds;
    dkImageViewDefaults(&colour, &c->gpu.image);
    dkImageViewDefaults(&ds, &c->ds[f]);
    dkCmdBufBindRenderTarget(c->gpu.cmdbuf, &colour, &ds);
    dkCmdBufClearColorFloat(c->gpu.cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 0.0f);
    BindTest(c, 0, STENCIL_EQUAL, STENCIL_KEEP, STENCIL_KEEP, STENCIL_KEEP);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0x00, 0x11, 0xFF);
    ok = DrawBand(c, &c->flat, 0, SIZE / 2u, MARK, 0.5f, 1.0f, false);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0x00, 0x40, 0xFF);
    ok = ok && DrawBand(c, &c->flat, SIZE / 2u, SIZE, MARK, 0.5f, 1.0f, false);
    pixels = Finish(c, ok, stencil_name);
    if (pixels == NULL) {
        return;
    }
    const uint8_t *const expect[BANDS] = { MARK, MARK, MARK };
    Judge(pixels, stencil_name, 7u, expect,
          "rows 0-15 should hold 0x11 (depth fail INCR), rows 16-31 0x40 (pass REPLACE)");
}

/// The side effect cases: a counter the fragment shader adds one to, over the whole target, with
/// the top half (rows 0-15) rejected by the depth test (@p by_stencil false: a depth of nought
/// under LESS) or by the stencil test (EQUAL 0x5a against 0x00). @p early picks the shader with
/// early fragment tests, which writes no depth; the late one writes 0.5.
static void CountCase(StencilContext *c, uint32_t f, bool early, bool by_stencil) {
    char name[128];
    snprintf(name, sizeof(name), "%s: %s",
             FORMAT_NAMES[f],
             early ? (by_stencil ? "with early fragment tests the shader runs only where the "
                                   "stencil test passes"
                                 : "with early fragment tests the shader runs only where the depth "
                                   "test passes")
                   : (by_stencil ? "a shader that writes depth runs where the stencil test then "
                                   "fails"
                                 : "a shader that writes depth runs where the depth test then "
                                   "fails"));
    if (!ReportStart(SUITE, name)) {
        return;
    }
    c->counter[0] = 0;
    c->counter[1] = 0;
    // Begin's bands are cleared over again by halves below, which is what this case's rows are.
    static const uint8_t none[BANDS] = { 0, 0, 0 };
    static const float far[BANDS] = { 1.0f, 1.0f, 1.0f };
    Begin(c, f, none, far);
    DkDepthStencilState writing;
    dkDepthStencilStateDefaults(&writing);
    writing.depthTestEnable = 1;
    writing.depthWriteEnable = 1;
    writing.depthCompareOp = DkCompareOp_Always;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &writing);
    Scissor(c, 0, SIZE / 2u);
    dkCmdBufClearDepthStencil(c->gpu.cmdbuf, true, by_stencil ? 1.0f : 0.0f, 0xFF, 0x00);
    Scissor(c, SIZE / 2u, SIZE);
    dkCmdBufClearDepthStencil(c->gpu.cmdbuf, true, 1.0f, 0xFF, 0x5A);
    Scissor(c, 0, SIZE);
    BindTest(c, DkCompareOp_Less, by_stencil ? STENCIL_EQUAL : STENCIL_ALWAYS, STENCIL_KEEP,
             STENCIL_KEEP, STENCIL_KEEP);
    dkCmdBufSetStencil(c->gpu.cmdbuf, DkFace_FrontAndBack, 0x00, 0x5A, 0xFF);
    dkCmdBufBindStorageBuffer(c->gpu.cmdbuf, DkStage_Fragment, 0, c->counter_addr, 16);
    bool ok = DrawBand(c, early ? &c->count_early : &c->count_late, 0, SIZE, GREEN, 0.5f, 0.5f,
                       false);
    // The counter's lines out of the graphics unit's cache before the processor reads them.
    dkCmdBufBarrier(c->gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    const uint8_t *const pixels = Finish(c, ok, name);
    if (pixels == NULL) {
        return;
    }
    const uint32_t count = c->counter[early ? 1 : 0];
    const uint32_t want = early ? SIZE * SIZE / 2u : SIZE * SIZE;
    uint32_t wrong = 0;
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            wrong += memcmp(GpuPixel(pixels, SIZE, x, y), y < SIZE / 2u ? BLACK : GREEN, 4) != 0;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, count == want && wrong == 0u,
               "the shader ran %u times, wanted %u (%u pixels, the top half rejected); %u pixels "
               "of colour wrong",
               (unsigned)count, (unsigned)want, (unsigned)(SIZE * SIZE), (unsigned)wrong);
}

static void RunFormatCases(StencilContext *c, uint32_t f) {
    for (uint32_t op = STENCIL_NEVER; op <= STENCIL_ALWAYS; ++op) {
        CompareCase(c, f, (StencilCompare)op);
    }
    // Each op once as the pass op, once as the fail op and once as the depth fail op.
    for (uint32_t k = 0; k < 8u; ++k) {
        OpCase(c, f, (StencilOp)(1u + (k + 3u) % 8u), (StencilOp)(1u + (k + 5u) % 8u),
               (StencilOp)(1u + k));
    }
    BoundaryCase(c, f, 0xFF, STENCIL_INCR, STENCIL_INCR_WRAP);
    BoundaryCase(c, f, 0x00, STENCIL_DECR, STENCIL_DECR_WRAP);
    MaskCases(c, f);
    FaceCase(c, f);
    ShaderDepthCases(c, f);
    CountCase(c, f, false, false);
    CountCase(c, f, true, false);
    CountCase(c, f, false, true);
    CountCase(c, f, true, true);
}

void RunStencil(void) {
    if (!ReportStart(SUITE, "graphics device opens with two depth and stencil targets")) {
        return;
    }
    StencilContext c;
    memset(&c, 0, sizeof(c));
    GpuSpec spec;
    GpuSpecDefaults(&spec, SIZE, SIZE);
    spec.code_size = 64u * 1024u;
    spec.data_size = 256u * 1024u;
    spec.image_size = 1024u * 1024u;
    bool opened = GpuOpenSpec(&c.gpu, &spec);
    for (uint32_t f = 0; opened && f < 2u; ++f) {
        opened = GpuCreateRenderImage(&c.gpu, &c.ds[f], &c.ds_layout[f], FORMATS[f], SIZE, SIZE,
                                      DkMsMode_1x);
    }
    void *const counter =
        opened ? GpuAllocData(&c.gpu, 256, 256, &c.counter_addr) : NULL;
    opened = opened && counter != NULL;
    CHECK_TRUE(SUITE, "graphics device opens with two depth and stencil targets", CHECK_NONE,
               opened, "%s", c.gpu.failure[0] != '\0' ? c.gpu.failure : "no room for the counter");
    if (!opened) {
        GpuClose(&c.gpu);
        return;
    }
    c.counter = (volatile uint32_t *)counter;
    if (!ReportStart(SUITE, "the drawing shaders load")) {
        GpuClose(&c.gpu);
        return;
    }
    const bool loaded =
        GpuLoadShader(&c.gpu, &c.vertex, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&c.gpu, &c.flat, ShaderData(SHADER_FLAT_FRAG),
                      ShaderSize(SHADER_FLAT_FRAG)) &&
        GpuLoadShader(&c.gpu, &c.depth_out, ShaderData(SHADER_DEPTH_OUT_FRAG),
                      ShaderSize(SHADER_DEPTH_OUT_FRAG)) &&
        GpuLoadShader(&c.gpu, &c.count_late, ShaderData(SHADER_COUNT_LATE_FRAG),
                      ShaderSize(SHADER_COUNT_LATE_FRAG)) &&
        GpuLoadShader(&c.gpu, &c.count_early, ShaderData(SHADER_COUNT_EARLY_FRAG),
                      ShaderSize(SHADER_COUNT_EARLY_FRAG));
    CHECK_TRUE(SUITE, "the drawing shaders load", CHECK_NONE, loaded, "%s", c.gpu.failure);
    if (!loaded) {
        GpuClose(&c.gpu);
        return;
    }
    for (uint32_t f = 0; f < 2u; ++f) {
        RunFormatCases(&c, f);
    }
    GpuClose(&c.gpu);
}
