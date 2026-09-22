// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../raster.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The first cases that put geometry through the pipeline. A triangle or a quad is drawn into a
// 64 by 64 target in one flat colour, every pixel is read back, and every pixel is compared to
// what the coverage rule in source/raster.c produces.
//
// The fragment stage does nothing here, so a failure points at vertex fetch, the transform into
// target space, or the rasteriser's coverage. Two tiers, and each case says which it judged at:
// exact where a centre is at least half a pixel from every edge, and position for the band an
// edge runs through, where sub pixel precision is a reported limit rather than a constant.
//
// The two cases that take no margin are the sharpest here: 32 pixel centres land exactly on a
// shared edge, and the fill rule alone decides them.

static const char *const SUITE = "raster";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u

// The colour nothing draws and the colour everything draws, differing in all four channels so a
// channel quietly not being written has nowhere to hide. Both are ones and noughts, the two ends
// of the normalised range, so there is nothing to round.
static const float CLEAR_COLOR[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
static const uint8_t CLEAR_BYTES[4] = { 0x00, 0x00, 0xFF, 0x00 };
static const float DRAW_COLOR[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
static const uint8_t DRAW_BYTES[4] = { 0xFF, 0xFF, 0x00, 0xFF };

/// What the case expects, in the terms source/raster.c speaks.
typedef struct {
    const RasterTriangle *triangles;
    uint32_t triangle_count;
    /// How close to an edge a centre may be before the case stops claiming to know the answer.
    double margin;
    /// x, y, width, height in target space. Pixels outside must be the clear colour.
    uint32_t scissor[4];
} Expectation;

static GpuVertex Corner(float x, float y) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = 0.0f;
    vertex.u = 0.0f;
    vertex.v = 0.0f;
    vertex.r = DRAW_COLOR[0];
    vertex.g = DRAW_COLOR[1];
    vertex.b = DRAW_COLOR[2];
    vertex.a = DRAW_COLOR[3];
    return vertex;
}

/// Six vertices making the rectangle from (@p x0, @p y0) to (@p x1, @p y1) in clip space.
///
/// The corner order is the contract between this function and QuadTriangles below: the two have
/// to hand the same two triangles to the hardware and to the reference, in the same winding, or
/// the fill rule would resolve the quad's own diagonal one way on one side and the other way on
/// the other, and 64 pixels down that diagonal would read as a failure.
static void QuadVertices(GpuVertex out[6], float x0, float y0, float x1, float y1) {
    const GpuVertex top_left = Corner(x0, y1);
    const GpuVertex top_right = Corner(x1, y1);
    const GpuVertex bottom_right = Corner(x1, y0);
    const GpuVertex bottom_left = Corner(x0, y0);
    out[0] = top_left;
    out[1] = top_right;
    out[2] = bottom_right;
    out[3] = top_left;
    out[4] = bottom_right;
    out[5] = bottom_left;
}

static void QuadTriangles(RasterTriangle out[2], const RasterViewport *viewport, float x0,
                          float y0, float x1, float y1) {
    const float first[6] = { x0, y1, x1, y1, x1, y0 };
    const float second[6] = { x0, y1, x1, y0, x0, y0 };
    RasterTriangleFromClip(&out[0], viewport, first);
    RasterTriangleFromClip(&out[1], viewport, second);
}

static bool PixelIs(const uint8_t *pixel, const uint8_t expected[4]) {
    // Threshold zero, which is what the exact tier means: the same comparison the selfcheck
    // suite pins down, asked for no tolerance at all.
    return CheckChannel(pixel[0], expected[0], 0) && CheckChannel(pixel[1], expected[1], 0) &&
           CheckChannel(pixel[2], expected[2], 0) && CheckChannel(pixel[3], expected[3], 0);
}

/// Walks every pixel and compares it to the coverage rule's answer.
///
/// A pixel outside the scissor must be the clear colour, whatever the triangles say. A pixel
/// inside it is judged by whether any triangle covers its centre, unless that centre is within
/// the margin of an edge, in which case either answer is accepted and the pixel is counted.
///
/// Returns false and writes a sentence naming the first wrong pixel, what it held, what it
/// should have held and how many were wrong in total.
static bool CheckCoverage(const uint8_t *pixels, const Expectation *want, char *detail,
                          size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t judged = 0;
    uint32_t allowed_either = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t first[4] = { 0, 0, 0, 0 };
    const uint8_t *first_expected = CLEAR_BYTES;

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const bool in_scissor =
                x >= want->scissor[0] && x < want->scissor[0] + want->scissor[2] &&
                y >= want->scissor[1] && y < want->scissor[1] + want->scissor[3];

            bool near_edge = false;
            bool covered = false;
            if (in_scissor) {
                for (uint32_t t = 0; t < want->triangle_count; ++t) {
                    const RasterVerdict verdict =
                        RasterClassifyPixel(&want->triangles[t], x, y, want->margin);
                    if (verdict == RASTER_INSIDE) {
                        covered = true;
                    } else if (verdict == RASTER_NEAR_EDGE) {
                        near_edge = true;
                    }
                }
            }

            // A pixel a triangle definitely covers is settled even if another triangle's edge
            // happens to run through it, which is what keeps the quad cases from throwing away
            // the pixels along their own internal diagonal.
            if (near_edge && !covered) {
                ++allowed_either;
                continue;
            }

            ++judged;
            const uint8_t *const expected = covered ? DRAW_BYTES : CLEAR_BYTES;
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (PixelIs(pixel, expected)) {
                continue;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                memcpy(first, pixel, sizeof(first));
                first_expected = expected;
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        snprintf(detail, detail_size, "%u pixels judged, %u left to the implementation",
                 (unsigned)judged, (unsigned)allowed_either);
        return true;
    }
    snprintf(detail, detail_size,
             "%u of %u judged pixels wrong (%u left to the implementation), first at (%u,%u) is "
             "%02x%02x%02x%02x, expected %02x%02x%02x%02x",
             (unsigned)wrong, (unsigned)judged, (unsigned)allowed_either, (unsigned)first_x,
             (unsigned)first_y, first[0], first[1], first[2], first[3], first_expected[0],
             first_expected[1], first_expected[2], first_expected[3]);
    return false;
}

/// What every case does: clear, draw, read back, compare, report one line.
///
/// @p viewport and @p scissor are the state the case wants on top of what GpuBeginPass sets,
/// and NULL means leave that at the whole target.
static void DrawAndCheck(GpuContext *gpu, const DkShader *vertex_shader,
                         const DkShader *fragment_shader, const char *name,
                         const GpuVertex *vertices, uint32_t vertex_count,
                         const DkViewport *viewport, const DkScissor *scissor,
                         const Expectation *want) {
    GpuClear(gpu, CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2], CLEAR_COLOR[3]);

    const uint32_t mark = GpuDataMark(gpu);
    GpuBeginPass(gpu);
    if (viewport != NULL) {
        dkCmdBufSetViewports(gpu->cmdbuf, 0, viewport, 1);
    }
    if (scissor != NULL) {
        dkCmdBufSetScissors(gpu->cmdbuf, 0, scissor, 1);
    }
    const bool drawn =
        GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, vertex_count);
    GpuSubmit(gpu);
    GpuDataRewind(gpu, mark);

    // The margin is the tier: a case that held nothing back judged every pixel against one
    // right answer, and one that allowed either answer near an edge has claimed the position
    // tier.
    const CheckTier tier = want->margin > 0.0 ? CHECK_POSITION : CHECK_EXACT;

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
    const bool matched = CheckCoverage(pixels, want, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, tier, matched, "%s", detail);
}

/// A quad case: the rectangle in clip space, judged with no margin.
///
/// No margin is honest here because every one of these rectangles has its edges on exact pixel
/// boundaries, so no centre is closer than half a pixel to any of them, except the ones on the
/// quad's own internal diagonal, which the fill rule settles and CheckCoverage keeps.
static void QuadCase(GpuContext *gpu, const DkShader *vertex_shader,
                     const DkShader *fragment_shader, const char *name, float x0, float y0,
                     float x1, float y1) {
    GpuVertex vertices[6];
    QuadVertices(vertices, x0, y0, x1, y1);
    RasterTriangle triangles[2];
    QuadTriangles(triangles, &RASTER_FULL_VIEWPORT, x0, y0, x1, y1);

    Expectation want;
    want.triangles = triangles;
    want.triangle_count = 2;
    want.margin = 0.0;
    want.scissor[0] = 0;
    want.scissor[1] = 0;
    want.scissor[2] = TARGET_WIDTH;
    want.scissor[3] = TARGET_HEIGHT;
    DrawAndCheck(gpu, vertex_shader, fragment_shader, name, vertices, 6, NULL, NULL, &want);
}

/// A triangle case stated in clip space, with a margin and an optional viewport and scissor.
static void TriangleCase(GpuContext *gpu, const DkShader *vertex_shader,
                         const DkShader *fragment_shader, const char *name, const float clip[6],
                         double margin, const RasterViewport *viewport, const DkScissor *scissor) {
    const GpuVertex vertices[3] = {
        Corner(clip[0], clip[1]),
        Corner(clip[2], clip[3]),
        Corner(clip[4], clip[5]),
    };
    RasterTriangle triangle;
    RasterTriangleFromClip(&triangle, viewport, clip);

    Expectation want;
    want.triangles = &triangle;
    want.triangle_count = 1;
    want.margin = margin;
    if (scissor != NULL) {
        want.scissor[0] = scissor->x;
        want.scissor[1] = scissor->y;
        want.scissor[2] = scissor->width;
        want.scissor[3] = scissor->height;
    } else {
        want.scissor[0] = 0;
        want.scissor[1] = 0;
        want.scissor[2] = TARGET_WIDTH;
        want.scissor[3] = TARGET_HEIGHT;
    }

    const DkViewport hardware_viewport = { viewport->x, viewport->y, viewport->width,
                                           viewport->height, 0.0f, 1.0f };
    DrawAndCheck(gpu, vertex_shader, fragment_shader, name, vertices, 3, &hardware_viewport,
                 scissor, &want);
}

void RunRaster(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    // Room for both shaders several times over, and for a dozen draws' worth of vertices. Sized
    // generously because running out is a budget, not a finding, and a budget that shows up as
    // a failed drawing case would be read as one.
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
    const bool loaded =
        GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_FLAT_FRAG),
                      ShaderSize(SHADER_FLAT_FRAG));
    CHECK_TRUE(SUITE, "the shaders load", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }

    ReportNote("raster target %ux%u RGBA8_Unorm, clear %02x%02x%02x%02x, drawn %02x%02x%02x%02x",
               TARGET_WIDTH, TARGET_HEIGHT, CLEAR_BYTES[0], CLEAR_BYTES[1], CLEAR_BYTES[2],
               CLEAR_BYTES[3], DRAW_BYTES[0], DRAW_BYTES[1], DRAW_BYTES[2], DRAW_BYTES[3]);

    // Whether anything comes out at all, before any case asks where it came out. A quad over the
    // whole of clip space covers every pixel, so this is also the case that separates "the
    // pipeline does nothing" from "the pipeline puts things in the wrong place".
    QuadCase(&gpu, &vertex_shader, &fragment_shader, "a full screen quad covers every pixel",
             -1.0f, -1.0f, 1.0f, 1.0f);

    // The direction of each axis, one case each, deliberately before anything with a slope. If
    // the vertical axis is upside down, every case below this one fails with a mirror image and
    // this is the line that says so in one word.
    QuadCase(&gpu, &vertex_shader, &fragment_shader, "clip space plus y is the top half", -1.0f,
             0.0f, 1.0f, 1.0f);
    QuadCase(&gpu, &vertex_shader, &fragment_shader, "clip space minus x is the left half", -1.0f,
             -1.0f, 0.0f, 1.0f);

    // The slanted triangle, whose corners and the reasons for them are in source/raster.c next
    // to the rule that judges it. Judged with half a pixel of margin: the interior and the
    // exterior are exact, the band the edges run through is counted and left alone.
    TriangleCase(&gpu, &vertex_shader, &fragment_shader,
                 "a triangle's interior is exact and its edges land within a pixel",
                 RASTER_SLANTED, 0.5, &RASTER_FULL_VIEWPORT, NULL);

    // The fill rule. The two halves of a square split along a diagonal that passes through 32
    // pixel centres exactly, both stated in source/raster.c. Judged with no margin at all,
    // because there is nothing here for sub pixel precision to decide: the corners sit on whole
    // pixel boundaries and the edge function at those centres is exactly zero however many
    // fraction bits the hardware keeps.
    //
    // Read the two together. Exactly 32 wrong pixels in both, and nothing wrong anywhere else,
    // does not mean coverage is broken: it means the shared edge went to the other triangle,
    // which is a consistent rule and the wrong one. Any other count is a coverage fault. The
    // graphics API this suite is written against requires the top and left rule, so either
    // answer is a finding, and the two are worth telling apart on sight.
    TriangleCase(&gpu, &vertex_shader, &fragment_shader,
                 "a shared edge belongs to the triangle it is the left edge of",
                 RASTER_UPPER_RIGHT_HALF, 0.0, &RASTER_FULL_VIEWPORT, NULL);
    TriangleCase(&gpu, &vertex_shader, &fragment_shader,
                 "the same edge does not also belong to the other triangle",
                 RASTER_LOWER_LEFT_HALF, 0.0, &RASTER_FULL_VIEWPORT, NULL);

    // The same slanted triangle through a viewport that is half the size and offset, so every
    // corner moves and the rest of the target must stay the colour it was cleared to. A viewport
    // does not clip, it only transforms; this triangle stays inside its rectangle, so what the
    // case asserts about the pixels outside is a consequence of where the triangle went, not of
    // any clipping.
    TriangleCase(&gpu, &vertex_shader, &fragment_shader, "a viewport offset moves the triangle",
                 RASTER_SLANTED, 0.5, &RASTER_HALF_VIEWPORT, NULL);

    // And the same triangle again with a scissor that shares neither its horizontal nor its
    // vertical centre line with the target, so a scissor read upside down or with its axes
    // swapped lands somewhere else entirely rather than on itself.
    static const DkScissor SCISSOR = { 12, 8, 24, 20 };
    TriangleCase(&gpu, &vertex_shader, &fragment_shader,
                 "a scissor leaves everything outside it untouched", RASTER_SLANTED, 0.5,
                 &RASTER_FULL_VIEWPORT, &SCISSOR);

    GpuClose(&gpu);
}
