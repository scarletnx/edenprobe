// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../fixedref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../rawref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The fixed function state a game's driver sets and Vulkan lacks or keeps elsewhere, so an
// emulator has to carry it over itself: the point size, the alpha test, the depth bounds, the
// rasteriser switched off, the viewport's sign and swizzle and the face they leave a triangle,
// the window origin and the depth mode a device is made with.
//
// Every case draws flat colours over whole pixels and reads all 4096 back: exact. Where a rule
// is more than a comparison (which pixels a point covers, where a swizzled viewport puts a
// rectangle, which face a triangle shows), it is fixedref.c's, pinned by the reference suite.
// Two things only a console settles are notes, not cases: which way up a point's coordinate
// runs, and whether a depth outside the range is clipped or clamped.

static const char *const SUITE = "fixed";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)

static const uint8_t CLEAR_COLOUR[4] = { 0x00, 0x00, 0xFF, 0xFF };
static const uint8_t DRAWN_COLOUR[4] = { 0xFF, 0x00, 0x00, 0xFF };
static const uint8_t OTHER_COLOUR[4] = { 0x00, 0xFF, 0x00, 0xFF };

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader flat;
    DkShader point_coord;
    /// What the case expects, built pixel by pixel before it is compared.
    uint8_t expected[PIXEL_COUNT * 4u];
} FixedContext;

/// One context at a time, so the expected image is not on the stack.
static FixedContext context;

static DkCompareOp CompareOp(FixedCompare op) {
    static const DkCompareOp OPS[FIXED_COMPARE_COUNT] = {
        DkCompareOp_Never,   DkCompareOp_Less,     DkCompareOp_Equal,  DkCompareOp_Lequal,
        DkCompareOp_Greater, DkCompareOp_NotEqual, DkCompareOp_Gequal, DkCompareOp_Always,
    };
    return OPS[op];
}

static GpuVertex Vertex(float x, float y, float z, const uint8_t colour[4], float alpha) {
    GpuVertex v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.u = 0.0f;
    v.v = 0.0f;
    v.r = (float)colour[0] / 255.0f;
    v.g = (float)colour[1] / 255.0f;
    v.b = (float)colour[2] / 255.0f;
    v.a = alpha;
    return v;
}

/// A clip space rectangle as two triangles, both counter clockwise with y upwards when @p ccw,
/// both clockwise otherwise.
static void Rect(GpuVertex out[6], float x0, float y0, float x1, float y1, float z,
                 const uint8_t colour[4], float alpha, bool ccw) {
    const GpuVertex a = Vertex(x0, y0, z, colour, alpha);
    const GpuVertex b = Vertex(x1, y0, z, colour, alpha);
    const GpuVertex c = Vertex(x1, y1, z, colour, alpha);
    const GpuVertex d = Vertex(x0, y1, z, colour, alpha);
    if (ccw) {
        out[0] = a, out[1] = b, out[2] = c, out[3] = a, out[4] = c, out[5] = d;
    } else {
        out[0] = a, out[1] = c, out[2] = b, out[3] = a, out[4] = d, out[5] = c;
    }
}

/// Clip space x and y of target space (@p x, @p y) through an upper left device's whole target
/// viewport: the inverse of FixedProject with nothing swizzled.
static float ClipX(double x) {
    return (float)(x / (TARGET_WIDTH * 0.5) - 1.0);
}
static float ClipY(double y) {
    return (float)(1.0 - y / (TARGET_HEIGHT * 0.5));
}

static void ExpectFill(FixedContext *x, const uint8_t colour[4]) {
    for (uint32_t i = 0; i < PIXEL_COUNT; ++i) {
        memcpy(x->expected + i * 4u, colour, 4);
    }
}

static void ExpectRect(FixedContext *x, FixedRect rect, const uint8_t colour[4]) {
    for (int32_t y = rect.y0; y < rect.y1; ++y) {
        for (int32_t xx = rect.x0; xx < rect.x1; ++xx) {
            if (xx >= 0 && y >= 0 && xx < (int32_t)TARGET_WIDTH && y < (int32_t)TARGET_HEIGHT) {
                memcpy(x->expected + ((size_t)y * TARGET_WIDTH + (size_t)xx) * 4u, colour, 4);
            }
        }
    }
}

static bool Unready(FixedContext *x, const char *name, bool recorded) {
    if (recorded && x->gpu.ready) {
        return false;
    }
    ReportCase(SUITE, name, CHECK_NONE, false,
               x->gpu.failure[0] != '\0' ? x->gpu.failure : "the list was not recorded");
    return true;
}

/// Reads the target back and holds every pixel inside @p region to the expected image; the
/// detail names the first wrong pixel and @p what, a sentence about what the case drew.
static void JudgeRegion(FixedContext *x, const char *name, FixedRect region, bool recorded,
                        const char *what) {
    if (Unready(x, name, recorded)) {
        return;
    }
    const uint8_t *const pixels = GpuReadBack(&x->gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   x->gpu.failure[0] != '\0' ? x->gpu.failure : "the readback handed back nothing");
        return;
    }
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0, drawn_where_clear = 0, clear_where_drawn = 0;
    uint32_t first_x = 0, first_y = 0;
    for (int32_t y = region.y0; y < region.y1; ++y) {
        for (int32_t xx = region.x0; xx < region.x1; ++xx) {
            const size_t at = ((size_t)y * TARGET_WIDTH + (size_t)xx) * 4u;
            const bool right = memcmp(pixels + at, x->expected + at, 4) == 0;
            PictureSet(picture, (uint32_t)xx, (uint32_t)y, x->expected + at,
                       right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (right) {
                continue;
            }
            if (wrong == 0) {
                first_x = (uint32_t)xx;
                first_y = (uint32_t)y;
            }
            ++wrong;
            if (memcmp(x->expected + at, CLEAR_COLOUR, 4) == 0) {
                ++drawn_where_clear;
            } else if (memcmp(pixels + at, CLEAR_COLOUR, 4) == 0) {
                ++clear_where_drawn;
            }
        }
    }
    const size_t first = ((size_t)first_y * TARGET_WIDTH + first_x) * 4u;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%s: %u wrong pixels, %u drawn where the target should keep its clear, %u left "
               "clear where something should be drawn; the first at (%u, %u) is %02x %02x %02x "
               "%02x, expected %02x %02x %02x %02x",
               what, (unsigned)wrong, (unsigned)drawn_where_clear, (unsigned)clear_where_drawn,
               (unsigned)first_x, (unsigned)first_y, pixels[first], pixels[first + 1],
               pixels[first + 2], pixels[first + 3], x->expected[first], x->expected[first + 1],
               x->expected[first + 2], x->expected[first + 3]);
}

static const FixedRect WHOLE = { 0, 0, (int32_t)TARGET_WIDTH, (int32_t)TARGET_HEIGHT };

static void Judge(FixedContext *x, const char *name, bool recorded, const char *what) {
    JudgeRegion(x, name, WHOLE, recorded, what);
}

static void Clear(FixedContext *x) {
    GpuClear(&x->gpu, (float)CLEAR_COLOUR[0] / 255.0f, (float)CLEAR_COLOUR[1] / 255.0f,
             (float)CLEAR_COLOUR[2] / 255.0f, (float)CLEAR_COLOUR[3] / 255.0f);
}

// ---- points

/// The four points every point case draws in one list, the size set before each, in target
/// space: odd sizes on a pixel centre, even ones on a pixel corner, so no pixel centre sits on an
/// edge of any of them (the reference suite checks it).
static const struct {
    double x, y;
    float size;
} POINTS[] = {
    { 10.5, 10.5, 1.0f },
    { 42.5, 10.5, 3.0f },
    { 16.0, 44.0, 4.0f },
    { 44.0, 44.0, 8.0f },
};
#define POINT_COUNT (sizeof(POINTS) / sizeof(POINTS[0]))

/// The quarter of the target point @p i sits in, which its case judges.
static FixedRect PointCell(size_t i) {
    const int32_t half = (int32_t)TARGET_WIDTH / 2;
    const int32_t col = POINTS[i].x < half ? 0 : half;
    const int32_t row = POINTS[i].y < half ? 0 : half;
    const FixedRect cell = { col, row, col + half, row + half };
    return cell;
}

static bool DrawPoints(FixedContext *x, const DkShader *fragment) {
    GpuVertex vertices[POINT_COUNT];
    for (size_t i = 0; i < POINT_COUNT; ++i) {
        vertices[i] = Vertex(ClipX(POINTS[i].x), ClipY(POINTS[i].y), 0.0f, DRAWN_COLOUR, 1.0f);
    }
    if (!GpuBindTriangleState(&x->gpu, &x->vertex, fragment, vertices, POINT_COUNT)) {
        return false;
    }
    for (size_t i = 0; i < POINT_COUNT; ++i) {
        dkCmdBufSetPointSize(x->gpu.cmdbuf, POINTS[i].size);
        dkCmdBufDraw(x->gpu.cmdbuf, DkPrimitive_Points, 1, 1, (uint32_t)i, 0);
    }
    dkCmdBufSetPointSize(x->gpu.cmdbuf, 1.0f);
    return true;
}

static void PointCase(FixedContext *x, size_t i) {
    char name[128];
    const unsigned size = (unsigned)POINTS[i].size;
    snprintf(name, sizeof(name), "a point of size %u covers the %u by %u pixels around its centre",
             size, size, size);
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Clear(x);
    GpuBeginPass(&x->gpu);
    const bool recorded = DrawPoints(x, &x->flat);
    GpuSubmit(&x->gpu);
    ExpectFill(x, CLEAR_COLOUR);
    for (size_t p = 0; p < POINT_COUNT; ++p) {
        ExpectRect(x, FixedPointRect(POINTS[p].x, POINTS[p].y, POINTS[p].size), DRAWN_COLOUR);
    }
    char what[96];
    snprintf(what, sizeof(what), "a point of size %u at (%.1f, %.1f), its size set between draws",
             size, POINTS[i].x, POINTS[i].y);
    JudgeRegion(x, name, PointCell(i), recorded, what);
}

/// The coordinate a point's fragments read, whose origin the console's PointCoordReplace
/// register sets and deko3d leaves at lower left: a note, since nothing but a console run says
/// which row reads nought.
static void PointCoordNote(FixedContext *x) {
    Clear(x);
    GpuBeginPass(&x->gpu);
    const bool recorded = DrawPoints(x, &x->point_coord);
    GpuSubmit(&x->gpu);
    const uint8_t *const pixels = recorded && x->gpu.ready ? GpuReadBack(&x->gpu) : NULL;
    if (pixels == NULL) {
        ReportNote("fixed point coordinate: no readback (%s)", x->gpu.failure);
        return;
    }
    const FixedRect r = FixedPointRect(POINTS[3].x, POINTS[3].y, POINTS[3].size);
    // The shader writes no blue, which the clear holds: a pixel without it was drawn. A machine
    // that drew the point smaller says so here, since its corners then hold the clear.
    uint32_t covered = 0;
    for (int32_t y = r.y0; y < r.y1; ++y) {
        for (int32_t xx = r.x0; xx < r.x1; ++xx) {
            covered += GpuPixel(pixels, TARGET_WIDTH, (uint32_t)xx, (uint32_t)y)[2] == 0u;
        }
    }
    const uint8_t *const top = GpuPixel(pixels, TARGET_WIDTH, (uint32_t)r.x0, (uint32_t)r.y0);
    const uint8_t *const bottom =
        GpuPixel(pixels, TARGET_WIDTH, (uint32_t)r.x1 - 1u, (uint32_t)r.y1 - 1u);
    // In eighths: 1/16 at the first texel row and 15/16 at the last, 16 and 239 as bytes.
    ReportNote("fixed point coordinate: a point of size 8 drew %u of its 64 pixels and reads "
               "gl_PointCoord (%u, %u) at its first row and column and (%u, %u) at its last, of "
               "255; 16 at the first row is the coordinate's origin at the top of memory, 239 at "
               "the bottom",
               (unsigned)covered, top[0], top[1], bottom[0], bottom[1]);
}

// ---- the alpha test

static const float ALPHA = 0.5f;
static const float ALPHA_REFERENCES[3] = { 0.25f, 0.5f, 0.75f };

static void AlphaCase(FixedContext *x, FixedCompare op) {
    char name[128];
    char passes[48] = "";
    for (size_t i = 0; i < 3; ++i) {
        if (FixedAlphaPasses(op, ALPHA, ALPHA_REFERENCES[i])) {
            char one[12];
            snprintf(one, sizeof(one), "%s%.2f", passes[0] != '\0' ? ", " : "",
                     (double)ALPHA_REFERENCES[i]);
            strncat(passes, one, sizeof(passes) - strlen(passes) - 1u);
        }
    }
    snprintf(name, sizeof(name), "an alpha test %s passes an alpha of 0.5 against %s",
             FixedCompareName(op), passes[0] != '\0' ? passes : "no reference");
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Clear(x);
    GpuBeginPass(&x->gpu);
    DkColorState colour;
    dkColorStateDefaults(&colour);
    colour.alphaCompareOp = CompareOp(op);
    dkCmdBufBindColorState(x->gpu.cmdbuf, &colour);
    // Red, green and blue only, so the half alpha the target would store, whose rounding is the
    // format suite's business, never reaches a byte this case reads.
    DkColorWriteState mask;
    dkColorWriteStateDefaults(&mask);
    dkColorWriteStateSetMask(&mask, 0, DkColorMask_R | DkColorMask_G | DkColorMask_B);
    dkCmdBufBindColorWriteState(x->gpu.cmdbuf, &mask);
    bool recorded = true;
    ExpectFill(x, CLEAR_COLOUR);
    for (size_t i = 0; i < 3 && recorded; ++i) {
        // A column each, the reference set between draws of one list.
        const float x0 = -1.0f + (float)i * (2.0f / 3.0f);
        GpuVertex rect[6];
        Rect(rect, x0, -1.0f, x0 + 2.0f / 3.0f, 1.0f, 0.0f, DRAWN_COLOUR, ALPHA, true);
        dkCmdBufSetAlphaRef(x->gpu.cmdbuf, ALPHA_REFERENCES[i]);
        recorded = GpuDrawTriangles(&x->gpu, &x->vertex, &x->flat, rect, 6);
        if (FixedAlphaPasses(op, ALPHA, ALPHA_REFERENCES[i])) {
            // The columns' edges: 0, 21.33, 42.67, 64; whole pixels whose centres are inside.
            const FixedRect column = { (int32_t)(i == 0 ? 0 : i == 1 ? 21 : 43), 0,
                                       (int32_t)(i == 0 ? 21 : i == 1 ? 43 : 64),
                                       (int32_t)TARGET_HEIGHT };
            uint8_t want[4];
            memcpy(want, DRAWN_COLOUR, 4);
            want[3] = CLEAR_COLOUR[3];
            ExpectRect(x, column, want);
        }
    }
    GpuSubmit(&x->gpu);
    Judge(x, name, recorded, "three columns of alpha 0.5, the references 0.25, 0.5 and 0.75");
}

// ---- depth bounds

static void BoundsCase(FixedContext *x, bool enabled, float min_bound, float max_bound) {
    static const float STORED = 0.5f;
    static const float FRAGMENT = 0.25f;
    const bool drawn = !enabled || FixedBoundsPass(STORED, min_bound, max_bound);
    char name[160];
    if (enabled) {
        snprintf(name, sizeof(name),
                 "depth bounds %.3f to %.3f %s a pixel whose buffer holds 0.5, its fragment at "
                 "0.25",
                 (double)min_bound, (double)max_bound, drawn ? "keep" : "reject");
    } else {
        snprintf(name, sizeof(name), "depth bounds switched off keep every pixel");
    }
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Clear(x);
    GpuClearDepth(&x->gpu, STORED);
    GpuBeginPass(&x->gpu);
    // The depth unit on, with a test every fragment passes and no write, so the bounds alone
    // decide and the buffer keeps 0.5.
    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = 1;
    depth.depthWriteEnable = 0;
    depth.depthCompareOp = DkCompareOp_Always;
    depth.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(x->gpu.cmdbuf, &depth);
    dkCmdBufSetDepthBounds(x->gpu.cmdbuf, enabled, min_bound, max_bound);
    GpuVertex rect[6];
    Rect(rect, -1.0f, -1.0f, 1.0f, 1.0f, FRAGMENT, DRAWN_COLOUR, 1.0f, true);
    const bool recorded = GpuDrawTriangles(&x->gpu, &x->vertex, &x->flat, rect, 6);
    GpuSubmit(&x->gpu);
    ExpectFill(x, drawn ? DRAWN_COLOUR : CLEAR_COLOUR);
    Judge(x, name, recorded,
          "a quad over the target at 0.25 over a buffer of 0.5, the bounds test against the "
          "buffer");
}

// ---- the rasteriser switched off

static void RasterizerOffCase(FixedContext *x) {
    static const char *const NAME = "with the rasteriser off a draw writes nothing";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Clear(x);
    GpuBeginPass(&x->gpu);
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.rasterizerEnable = 0;
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(x->gpu.cmdbuf, &rasterizer);
    GpuVertex rect[6];
    Rect(rect, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, DRAWN_COLOUR, 1.0f, true);
    const bool recorded = GpuDrawTriangles(&x->gpu, &x->vertex, &x->flat, rect, 6);
    GpuSubmit(&x->gpu);
    ExpectFill(x, CLEAR_COLOUR);
    Judge(x, NAME, recorded, "a quad over the target with RasterizerEnable off");
}

// ---- viewports and faces

/// The two rectangles every face case draws, in clip space: one counter clockwise, one
/// clockwise, at places a mirror or a transposition moves apart.
static const struct {
    float x0, y0, x1, y1;
    bool ccw;
    const uint8_t *colour;
} FACE_RECTS[2] = {
    { -0.75f, 0.25f, -0.25f, 0.75f, true, DRAWN_COLOUR },
    { 0.25f, -0.5f, 0.75f, 0.0f, false, OTHER_COLOUR },
};

typedef enum {
    /// The viewport as the device writes it.
    FACE_PLAIN,
    /// Its y scale written positive by a raw method, the rest as the device wrote it.
    FACE_SCALE_POSITIVE,
    /// The swizzle NegativeY.
    FACE_NEGATE_Y,
    /// Both, which cancel.
    FACE_SCALE_AND_NEGATE,
    /// The swizzle PositiveY into x and PositiveX into y.
    FACE_SWAP,
} FaceMode;

static FixedTransform FaceTransform(FaceMode mode, bool lower_left) {
    FixedTransform t;
    memset(&t, 0, sizeof(t));
    t.width = TARGET_WIDTH;
    t.height = TARGET_HEIGHT;
    t.scale_y_positive = lower_left;
    t.flip_winding = lower_left;
    if (mode == FACE_SCALE_POSITIVE || mode == FACE_SCALE_AND_NEGATE) {
        t.scale_y_positive = !t.scale_y_positive;
    }
    t.negate_y = mode == FACE_NEGATE_Y || mode == FACE_SCALE_AND_NEGATE;
    t.swap_xy = mode == FACE_SWAP;
    return t;
}

static void SetSwizzle(FixedContext *x, FaceMode mode) {
    DkViewportSwizzle swizzle = { DkSwizzle_PositiveX, DkSwizzle_PositiveY, DkSwizzle_PositiveZ,
                                  DkSwizzle_PositiveW };
    if (mode == FACE_NEGATE_Y || mode == FACE_SCALE_AND_NEGATE) {
        swizzle.y = DkSwizzle_NegativeY;
    } else if (mode == FACE_SWAP) {
        swizzle.x = DkSwizzle_PositiveY;
        swizzle.y = DkSwizzle_PositiveX;
    }
    dkCmdBufSetViewportSwizzles(x->gpu.cmdbuf, 0, &swizzle, 1);
}

/// Draws the two rectangles with back faces culled (none when @p cull is false) and the front
/// face counter clockwise when @p front_ccw, under @p mode, and judges the target.
static void FaceCase(FixedContext *x, const char *name, FaceMode mode, bool lower_left,
                     bool cull, bool front_ccw, const char *what) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Clear(x);
    GpuBeginPass(&x->gpu);
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = cull ? DkFace_Back : DkFace_None;
    rasterizer.frontFace = front_ccw ? DkFrontFace_CCW : DkFrontFace_CW;
    dkCmdBufBindRasterizerState(x->gpu.cmdbuf, &rasterizer);
    SetSwizzle(x, mode);
    bool recorded = true;
    if (mode == FACE_SCALE_POSITIVE || mode == FACE_SCALE_AND_NEGATE) {
        // The device wrote minus half the height (an upper left origin) or plus it (lower left):
        // the other sign, the offset left at the centre.
        uint32_t words[2];
        RawViewportScaleY(words, (lower_left ? -0.5f : 0.5f) * (float)TARGET_HEIGHT);
        recorded = GpuRawMethods(&x->gpu, words, 2);
    }
    for (size_t i = 0; i < 2 && recorded; ++i) {
        GpuVertex rect[6];
        Rect(rect, FACE_RECTS[i].x0, FACE_RECTS[i].y0, FACE_RECTS[i].x1, FACE_RECTS[i].y1, 0.0f,
             FACE_RECTS[i].colour, 1.0f, FACE_RECTS[i].ccw);
        recorded = GpuDrawTriangles(&x->gpu, &x->vertex, &x->flat, rect, 6);
    }
    // The next list starts from the swizzle the device began with.
    SetSwizzle(x, FACE_PLAIN);
    GpuSubmit(&x->gpu);

    const FixedTransform t = FaceTransform(mode, lower_left);
    const bool ccw_front = FixedFrontFacing(&t, front_ccw);
    ExpectFill(x, CLEAR_COLOUR);
    for (size_t i = 0; i < 2; ++i) {
        const bool front = FACE_RECTS[i].ccw ? ccw_front : !ccw_front;
        if (!cull || front) {
            ExpectRect(x,
                       FixedProjectRect(&t, FACE_RECTS[i].x0, FACE_RECTS[i].y0, FACE_RECTS[i].x1,
                                        FACE_RECTS[i].y1),
                       FACE_RECTS[i].colour);
        }
    }
    Judge(x, name, recorded, what);
}

// ---- depth outside the range

/// What a quad at a depth outside the range leaves, on the device's own depth mode: a note,
/// since whether the console clips it or clamps it with the clamp off is not settled.
static void OutsideNote(FixedContext *x, bool clamp) {
    Clear(x);
    GpuClearDepth(&x->gpu, 0.75f);
    GpuBeginPass(&x->gpu);
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = DkFace_None;
    rasterizer.depthClampEnable = clamp ? 1u : 0u;
    dkCmdBufBindRasterizerState(x->gpu.cmdbuf, &rasterizer);
    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = 1;
    depth.depthWriteEnable = 1;
    depth.depthCompareOp = DkCompareOp_Always;
    depth.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(x->gpu.cmdbuf, &depth);
    GpuVertex left[6], right[6];
    Rect(left, -1.0f, -1.0f, 0.0f, 1.0f, -0.5f, DRAWN_COLOUR, 1.0f, true);
    Rect(right, 0.0f, -1.0f, 1.0f, 1.0f, 1.5f, OTHER_COLOUR, 1.0f, true);
    const bool recorded = GpuDrawTriangles(&x->gpu, &x->vertex, &x->flat, left, 6) &&
                          GpuDrawTriangles(&x->gpu, &x->vertex, &x->flat, right, 6);
    GpuSubmit(&x->gpu);
    const uint8_t *const pixels = recorded && x->gpu.ready ? GpuReadBack(&x->gpu) : NULL;
    uint8_t colour_left[4] = { 0 }, colour_right[4] = { 0 };
    if (pixels != NULL) {
        memcpy(colour_left, GpuPixel(pixels, TARGET_WIDTH, 16, 32), 4);
        memcpy(colour_right, GpuPixel(pixels, TARGET_WIDTH, 48, 32), 4);
    }
    const uint8_t *const depths = pixels != NULL ? GpuReadBackDepth(&x->gpu) : NULL;
    if (depths == NULL) {
        ReportNote("fixed depth outside the range, clamp %s: no readback (%s)",
                   clamp ? "on" : "off", x->gpu.failure);
        return;
    }
    float depth_left, depth_right;
    memcpy(&depth_left, GpuPixel(depths, TARGET_WIDTH, 16, 32), 4);
    memcpy(&depth_right, GpuPixel(depths, TARGET_WIDTH, 48, 32), 4);
    ReportNote("fixed depth outside the range, clamp %s, depth from nought to one, buffer at "
               "0.75: a quad at z -0.5 left %s and depth %.6f, one at z 1.5 left %s and depth "
               "%.6f (drawn and 0 or 1 is clamped, the clear and 0.75 is clipped)",
               clamp ? "on" : "off",
               memcmp(colour_left, DRAWN_COLOUR, 4) == 0    ? "its colour"
               : memcmp(colour_left, CLEAR_COLOUR, 4) == 0  ? "the clear"
                                                            : "another colour",
               (double)depth_left,
               memcmp(colour_right, OTHER_COLOUR, 4) == 0   ? "its colour"
               : memcmp(colour_right, CLEAR_COLOUR, 4) == 0 ? "the clear"
                                                            : "another colour",
               (double)depth_right);
}

// ---- devices of their own

static bool OpenContext(FixedContext *x, const char *name, uint32_t device_flags) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.depth_format = DkImageFormat_ZF32;
    spec.device_flags = device_flags;
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    if (!ReportStart(SUITE, name)) {
        return false;
    }
    const bool opened = GpuOpenSpec(&x->gpu, &spec);
    const bool loaded =
        opened &&
        GpuLoadShader(&x->gpu, &x->vertex, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&x->gpu, &x->flat, ShaderData(SHADER_FLAT_FRAG),
                      ShaderSize(SHADER_FLAT_FRAG)) &&
        GpuLoadShader(&x->gpu, &x->point_coord, ShaderData(SHADER_POINT_COORD_FRAG),
                      ShaderSize(SHADER_POINT_COORD_FRAG));
    CHECK_TRUE(SUITE, name, CHECK_NONE, loaded, "%s", x->gpu.failure);
    if (opened && !loaded) {
        GpuClose(&x->gpu);
    }
    return loaded;
}

/// Four columns at clip z -1, -0.5, 0 and 0.5 on a device made with depth from minus one to one,
/// over a buffer of 1: each column's depth is the window depth of its z, bit for bit.
static void DepthModeCase(FixedContext *x) {
    static const char *const NAME =
        "with depth from minus one to one, z of -1, -0.5, 0 and 0.5 store 0, 0.25, 0.5 and 0.75";
    static const float ZS[4] = { -1.0f, -0.5f, 0.0f, 0.5f };
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Clear(x);
    GpuClearDepth(&x->gpu, 1.0f);
    GpuBeginPass(&x->gpu);
    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = 1;
    depth.depthWriteEnable = 1;
    depth.depthCompareOp = DkCompareOp_Always;
    depth.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(x->gpu.cmdbuf, &depth);
    bool recorded = true;
    for (size_t i = 0; i < 4 && recorded; ++i) {
        GpuVertex rect[6];
        const float x0 = -1.0f + 0.5f * (float)i;
        Rect(rect, x0, -1.0f, x0 + 0.5f, 1.0f, ZS[i], DRAWN_COLOUR, 1.0f, true);
        recorded = GpuDrawTriangles(&x->gpu, &x->vertex, &x->flat, rect, 6);
    }
    GpuSubmit(&x->gpu);
    if (Unready(x, NAME, recorded)) {
        return;
    }
    const uint8_t *const colours = GpuReadBack(&x->gpu);
    uint32_t undrawn = 0;
    for (uint32_t i = 0; colours != NULL && i < PIXEL_COUNT; ++i) {
        undrawn += memcmp(colours + i * 4u, DRAWN_COLOUR, 4) != 0;
    }
    const uint8_t *const depths = colours != NULL ? GpuReadBackDepth(&x->gpu) : NULL;
    if (depths == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false,
                   x->gpu.failure[0] != '\0' ? x->gpu.failure : "the readback handed back nothing");
        return;
    }
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_BITS, depths);
    uint32_t wrong = 0;
    float got[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t xx = 0; xx < TARGET_WIDTH; ++xx) {
            const size_t column = xx / (TARGET_WIDTH / 4u);
            const float want = FixedWindowDepth(ZS[column], true);
            float have;
            memcpy(&have, GpuPixel(depths, TARGET_WIDTH, xx, y), 4);
            const bool right = memcmp(&have, &want, 4) == 0;
            uint8_t want_bytes[4];
            memcpy(want_bytes, &want, 4);
            PictureSet(picture, xx, y, want_bytes, right ? PICTURE_MATCHED : PICTURE_WRONG);
            wrong += !right;
            if (y == TARGET_HEIGHT / 2u && xx % (TARGET_WIDTH / 4u) == 8u) {
                got[column] = have;
            }
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, wrong == 0 && undrawn == 0,
               "%u of %u depths wrong and %u pixels not drawn; the columns hold %.6f, %.6f, %.6f "
               "and %.6f",
               (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)undrawn, (double)got[0],
               (double)got[1], (double)got[2], (double)got[3]);
}

void RunFixed(void) {
    FixedContext *const x = &context;
    if (!OpenContext(x, "graphics device opens with a depth target and the shaders load", 0u)) {
        return;
    }

    for (size_t i = 0; i < POINT_COUNT; ++i) {
        PointCase(x, i);
    }
    PointCoordNote(x);

    for (int op = 0; op < FIXED_COMPARE_COUNT; ++op) {
        AlphaCase(x, (FixedCompare)op);
    }

    BoundsCase(x, true, 0.125f, 0.375f);
    BoundsCase(x, true, 0.375f, 0.625f);
    BoundsCase(x, true, 0.5f, 0.75f);
    BoundsCase(x, true, 0.25f, 0.5f);
    BoundsCase(x, true, 0.625f, 0.875f);
    BoundsCase(x, false, 0.125f, 0.375f);

    RasterizerOffCase(x);

    FaceCase(x, "a counter clockwise triangle is front and a clockwise one is culled", FACE_PLAIN,
             false, true, true, "two rectangles, back faces culled, the front counter clockwise");
    FaceCase(x, "with the front face clockwise the clockwise triangle is the one drawn",
             FACE_PLAIN, false, true, false,
             "two rectangles, back faces culled, the front clockwise");
    FaceCase(x, "a positive y scale mirrors the picture and culls the counter clockwise triangle",
             FACE_SCALE_POSITIVE, false, true, true,
             "two rectangles, the viewport's y scale written positive");
    FaceCase(x, "the NegativeY swizzle mirrors the picture and culls the counter clockwise "
             "triangle",
             FACE_NEGATE_Y, false, true, true, "two rectangles, the swizzle NegativeY");
    FaceCase(x, "a positive y scale and the NegativeY swizzle cancel", FACE_SCALE_AND_NEGATE,
             false, true, true,
             "two rectangles, the viewport's y scale positive and the swizzle NegativeY");
    FaceCase(x, "the swizzle that swaps x and y transposes the picture", FACE_SWAP, false, false,
             true, "two rectangles, the swizzle PositiveY into x and PositiveX into y, no culling");

    OutsideNote(x, false);
    OutsideNote(x, true);
    GpuClose(&x->gpu);

    // A device with the lower left origin: clip space y of one is the target's last row, and the
    // window origin's FlipY bit keeps OpenGL's faces.
    if (OpenContext(x, "a device with the lower left origin opens",
                    DkDeviceFlags_OriginLowerLeft)) {
        FaceCase(x, "with the origin lower left, clip space y of one is the last row", FACE_PLAIN,
                 true, false, true, "two rectangles on a lower left device, no culling");
        FaceCase(x, "with the origin lower left, a counter clockwise triangle is still front",
                 FACE_PLAIN, true, true, true,
                 "two rectangles on a lower left device, back faces culled");
        GpuClose(&x->gpu);
    }

    // A device with depth from minus one to one.
    if (OpenContext(x, "a device with depth from minus one to one opens",
                    DkDeviceFlags_DepthMinusOneToOne)) {
        DepthModeCase(x);
        GpuClose(&x->gpu);
    }
}
