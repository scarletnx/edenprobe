// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "../topologyref.h"
#include "tests.h"

// The primitive types the 3D class draws and Vulkan does not: quads, quad strips, polygons and
// line loops, an index buffer that restarts at any index, quads and polygons drawn as lines, and
// which vertex a flat colour comes from in each of them. An emulator remakes every one of these
// out of triangle lists, strips and the all-ones restart, often with an index buffer of its own,
// and each case is a place where that goes wrong: a quad split so that half of it takes the wrong
// vertex's colour, an 8-bit 0xff that is no longer a restart once widened, a diagonal that was
// never an edge drawn as one.
//
// Every case draws flat colours, its geometry on whole pixel corners and its diagonals at slopes
// that keep every pixel centre a seventh of a pixel or more from an edge where the colour
// changes, and reads all 4096 pixels back: exact. Which triangles and segments a draw makes, the
// vertex each takes its colour from and where an index list restarts are topologyref.c's, pinned
// by the reference suite. Two things only a console settles are notes: the vertex a polygon takes
// its colour from, and the one a quad takes under the first vertex convention.

static const char *const SUITE = "topology";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)
/// The most vertices one case's buffer holds: enough that 0xff names a vertex, so an 8-bit
/// restart index an emulator keeps as a vertex reads one rather than past the buffer.
#define MAX_VERTICES 256u
#define MAX_INDICES 16u
#define MAX_TRIANGLES 64u
#define MAX_SEGMENTS 64u

static const uint8_t CLEAR_COLOUR[4] = { 0x00, 0x00, 0x00, 0xFF };

/// A colour for each vertex of the cases that tell vertices apart, every one a byte pattern of its
/// own, and none the clear.
#define PALETTE_SIZE 9u
static const uint8_t PALETTE[PALETTE_SIZE][4] = {
    { 0xFF, 0x00, 0x00, 0xFF }, { 0x00, 0xFF, 0x00, 0xFF }, { 0x00, 0x00, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF, 0xFF }, { 0x00, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x80, 0x40, 0x20, 0xFF }, { 0x20, 0x80, 0x40, 0xFF },
};
/// The one colour of the cases that are about coverage alone.
#define ONE_COLOUR 0u

typedef enum {
    /// Every vertex the same colour: which pixels are drawn is the question.
    COLOURS_ONE,
    /// Vertex i of the case's points in palette colour i: which vertex a flat colour comes from.
    COLOURS_EACH,
} Colours;

/// One case: what it draws, and how.
typedef struct {
    const char *name;
    TopologyKind kind;
    const TopologyPoint *points;
    uint32_t point_count;
    Colours colours;
    /// Copies of the far vertex before the points: the vertex offset's room.
    uint32_t lead;
    /// The buffer is filled with the far vertex up to this many vertices, for indices past the
    /// points that a case means as anything but vertices.
    uint32_t pad_to;
    /// 0 for a draw of vertices; 1, 2 or 4 for an indexed draw of indices that wide.
    uint32_t index_bytes;
    const uint32_t *indices;
    /// Vertices, or indices, the draw takes.
    uint32_t count;
    /// The first vertex, or the first index.
    uint32_t first;
    int32_t vertex_offset;
    bool restart;
    uint32_t restart_index;
    /// The first vertex convention rather than deko3d's default, the last.
    bool first_provoking;
    /// Triangles, quads and polygons drawn as lines (the polygon mode Line, both faces).
    bool as_lines;
    /// A sentence about what the case drew, for its line.
    const char *what;
} TopologyCase;

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader flat;
    /// The draw's buffer as the case lays it out, and each vertex's palette colour.
    TopologyPoint points[MAX_VERTICES];
    uint32_t colours[MAX_VERTICES];
    uint32_t vertex_count;
    GpuVertex vertices[MAX_VERTICES];
    /// What the case expects, and which pixels it judges: a line's ends and sides are not.
    uint8_t expected[PIXEL_COUNT * 4u];
    bool judged[PIXEL_COUNT];
} TopologyContext;

/// One context at a time, so the expected image is not on the stack.
static TopologyContext context;

/// Where every bridge a missed restart, an index read as a vertex or a quad left over reaches.
static const TopologyPoint FAR = { 60.0, 8.0 };

static float ClipX(double x) {
    return (float)(x / (TARGET_WIDTH * 0.5) - 1.0);
}
static float ClipY(double y) {
    return (float)(1.0 - y / (TARGET_HEIGHT * 0.5));
}

static GpuVertex Vertex(TopologyPoint p, const uint8_t colour[4]) {
    GpuVertex v;
    v.x = ClipX(p.x);
    v.y = ClipY(p.y);
    v.z = 0.0f;
    v.u = 0.0f;
    v.v = 0.0f;
    v.r = (float)colour[0] / 255.0f;
    v.g = (float)colour[1] / 255.0f;
    v.b = (float)colour[2] / 255.0f;
    v.a = (float)colour[3] / 255.0f;
    return v;
}

/// Lays the case's buffer out in the context: @p c's lead, its points, then the far vertex up to
/// its pad, each with its colour.
static void Layout(TopologyContext *x, const TopologyCase *c) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->lead && n < MAX_VERTICES; ++i, ++n) {
        x->points[n] = FAR;
        x->colours[n] = ONE_COLOUR;
    }
    for (uint32_t i = 0; i < c->point_count && n < MAX_VERTICES; ++i, ++n) {
        x->points[n] = c->points[i];
        x->colours[n] = c->colours == COLOURS_EACH ? i % PALETTE_SIZE : ONE_COLOUR;
    }
    for (; n < c->pad_to && n < MAX_VERTICES; ++n) {
        x->points[n] = FAR;
        x->colours[n] = ONE_COLOUR;
    }
    x->vertex_count = n;
}

/// The vertex numbers a draw of @p c reaches, in order, with no restart: the vertices from the
/// first, or the indices from the first plus the offset.
static uint32_t Reached(const TopologyCase *c, uint32_t *out) {
    for (uint32_t i = 0; i < c->count && i < MAX_INDICES * 4u; ++i) {
        out[i] = c->index_bytes == 0u
                     ? c->first + i
                     : (uint32_t)((int64_t)c->indices[c->first + i] + c->vertex_offset);
    }
    return c->count;
}

/// Builds the picture @p c leaves: each triangle in its provoking vertex's colour, or each line
/// with its ends and sides left unjudged.
static void Expect(TopologyContext *x, const TopologyCase *c) {
    for (uint32_t i = 0; i < PIXEL_COUNT; ++i) {
        memcpy(x->expected + i * 4u, CLEAR_COLOUR, 4);
        x->judged[i] = true;
    }
    const bool lines = c->as_lines || c->kind == TOPOLOGY_LINES || c->kind == TOPOLOGY_LINE_LOOP ||
                       c->kind == TOPOLOGY_LINE_STRIP;
    uint32_t reached[MAX_INDICES * 4u];
    if (lines) {
        // The line cases restart nowhere.
        const uint32_t n = Reached(c, reached);
        TopologySegment segments[MAX_SEGMENTS];
        const uint32_t count = TopologySegments(c->kind, reached, n, segments, MAX_SEGMENTS);
        for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
            for (uint32_t xx = 0; xx < TARGET_WIDTH; ++xx) {
                const uint32_t at = y * TARGET_WIDTH + xx;
                const TopologyLinePixel judge =
                    TopologyLineJudge(x->points, segments, count, xx, y);
                if (judge == TOPOLOGY_LINE_ON) {
                    memcpy(x->expected + at * 4u, PALETTE[ONE_COLOUR], 4);
                }
                x->judged[at] = judge != TOPOLOGY_LINE_EITHER;
            }
        }
        return;
    }
    TopologyTriangle triangles[MAX_TRIANGLES];
    uint32_t count;
    if (c->index_bytes == 0u) {
        const uint32_t n = Reached(c, reached);
        count =
            TopologyTriangles(c->kind, reached, n, !c->first_provoking, triangles, MAX_TRIANGLES);
    } else {
        count = TopologyIndexedTriangles(c->kind, c->indices + c->first, c->count, c->restart,
                                         c->restart_index, c->vertex_offset, !c->first_provoking,
                                         triangles, MAX_TRIANGLES);
    }
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t xx = 0; xx < TARGET_WIDTH; ++xx) {
            const int32_t t = TopologyCoveringTriangle(x->points, triangles, count, xx, y);
            if (t >= 0) {
                memcpy(x->expected + (y * TARGET_WIDTH + xx) * 4u,
                       PALETTE[x->colours[triangles[t].provoking]], 4);
            }
        }
    }
}

/// The palette entry @p pixel holds, -1 for none of them.
static int32_t PaletteOf(const uint8_t *pixel) {
    for (uint32_t i = 0; i < PALETTE_SIZE; ++i) {
        if (memcmp(pixel, PALETTE[i], 4) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool Unready(TopologyContext *x, const char *name, bool recorded) {
    if (recorded && x->gpu.ready) {
        return false;
    }
    ReportCase(SUITE, name, CHECK_NONE, false,
               x->gpu.failure[0] != '\0' ? x->gpu.failure : "the list was not recorded");
    return true;
}

/// Reads the target back and holds every judged pixel to the expected picture; the line names the
/// first wrong pixel, and in a case that tells vertices apart, whose colour it holds.
static void Judge(TopologyContext *x, const TopologyCase *c, bool recorded) {
    if (Unready(x, c->name, recorded)) {
        return;
    }
    const uint8_t *const pixels = GpuReadBack(&x->gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, c->name, CHECK_NONE, false,
                   x->gpu.failure[0] != '\0' ? x->gpu.failure : "the readback handed back nothing");
        return;
    }
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0, judged = 0, drawn_where_clear = 0, clear_where_drawn = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < PIXEL_COUNT; ++i) {
        const uint32_t xx = i % TARGET_WIDTH, y = i / TARGET_WIDTH;
        if (!x->judged[i]) {
            PictureSet(picture, xx, y, x->expected + i * 4u, PICTURE_MATCHED);
            continue;
        }
        ++judged;
        const bool right = memcmp(pixels + i * 4u, x->expected + i * 4u, 4) == 0;
        PictureSet(picture, xx, y, x->expected + i * 4u, right ? PICTURE_MATCHED : PICTURE_WRONG);
        if (right) {
            continue;
        }
        if (wrong == 0) {
            first = i;
        }
        ++wrong;
        if (memcmp(x->expected + i * 4u, CLEAR_COLOUR, 4) == 0) {
            ++drawn_where_clear;
        } else if (memcmp(pixels + i * 4u, CLEAR_COLOUR, 4) == 0) {
            ++clear_where_drawn;
        }
    }
    char whose[64] = "";
    if (wrong != 0 && c->colours == COLOURS_EACH) {
        const int32_t got = PaletteOf(pixels + first * 4u);
        const int32_t want = PaletteOf(x->expected + first * 4u);
        if (got >= 0 && want >= 0) {
            snprintf(whose, sizeof(whose), ", vertex %d's colour where vertex %d's belongs",
                     (int)got, (int)want);
        }
    }
    const uint8_t *const g = pixels + first * 4u;
    const uint8_t *const w = x->expected + first * 4u;
    CHECK_TRUE(SUITE, c->name, CHECK_EXACT, wrong == 0,
               "%s: %u wrong pixels of %u judged, %u drawn where the target keeps its clear, %u "
               "left clear where something is drawn; the first at (%u, %u) is %02x %02x %02x "
               "%02x, expected %02x %02x %02x %02x%s",
               c->what, (unsigned)wrong, (unsigned)judged, (unsigned)drawn_where_clear,
               (unsigned)clear_where_drawn, (unsigned)(first % TARGET_WIDTH),
               (unsigned)(first / TARGET_WIDTH), g[0], g[1], g[2], g[3], w[0], w[1], w[2], w[3],
               whose);
}

static void PutIndex(uint8_t *indices, uint32_t bytes, uint32_t i, uint32_t value) {
    for (uint32_t b = 0; b < bytes; ++b) {
        indices[i * bytes + b] = (uint8_t)(value >> (8u * b));
    }
}

static DkIdxFormat IndexFormat(uint32_t bytes) {
    return bytes == 1u ? DkIdxFormat_Uint8 : bytes == 2u ? DkIdxFormat_Uint16 : DkIdxFormat_Uint32;
}

/// Records @p c's draw into the open pass, its rasteriser state and restart first.
static bool Record(TopologyContext *x, const TopologyCase *c) {
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = DkFace_None;
    rasterizer.provokingVertex =
        c->first_provoking ? DkProvokingVertex_First : DkProvokingVertex_Last;
    if (c->as_lines) {
        rasterizer.polygonModeFront = DkPolygonMode_Line;
        rasterizer.polygonModeBack = DkPolygonMode_Line;
    }
    dkCmdBufBindRasterizerState(x->gpu.cmdbuf, &rasterizer);
    dkCmdBufSetLineWidth(x->gpu.cmdbuf, 1.0f);
    // The 3D class keeps its restart state from one list to the next, so every case sets its own.
    dkCmdBufSetPrimitiveRestart(x->gpu.cmdbuf, c->restart, c->restart_index);

    for (uint32_t i = 0; i < x->vertex_count; ++i) {
        x->vertices[i] = Vertex(x->points[i], PALETTE[x->colours[i]]);
    }
    if (!GpuBindTriangleState(&x->gpu, &x->vertex, &x->flat, x->vertices, x->vertex_count)) {
        return false;
    }
    const DkPrimitive primitive = (DkPrimitive)c->kind;
    if (c->index_bytes == 0u) {
        dkCmdBufDraw(x->gpu.cmdbuf, primitive, c->count, 1, c->first, 0);
    } else {
        // Every index the case lists, from the first, so the indices before it are in the buffer
        // too and the draw has to start past them.
        const uint32_t total = c->first + c->count;
        DkGpuAddr address = 0;
        uint8_t *const indices =
            (uint8_t *)GpuAllocData(&x->gpu, total * c->index_bytes, 256, &address);
        if (indices == NULL) {
            snprintf(x->gpu.failure, sizeof(x->gpu.failure), "no scratch left for the indices");
            return false;
        }
        for (uint32_t i = 0; i < total; ++i) {
            PutIndex(indices, c->index_bytes, i, c->indices[i]);
        }
        dkCmdBufBindIdxBuffer(x->gpu.cmdbuf, IndexFormat(c->index_bytes), address);
        dkCmdBufDrawIndexed(x->gpu.cmdbuf, primitive, c->count, 1, c->first, c->vertex_offset, 0);
    }
    dkCmdBufSetPrimitiveRestart(x->gpu.cmdbuf, false, 0);
    return true;
}

static void Clear(TopologyContext *x) {
    GpuClear(&x->gpu, (float)CLEAR_COLOUR[0] / 255.0f, (float)CLEAR_COLOUR[1] / 255.0f,
             (float)CLEAR_COLOUR[2] / 255.0f, (float)CLEAR_COLOUR[3] / 255.0f);
}

static void RunCase(TopologyContext *x, const TopologyCase *c) {
    if (!ReportStart(SUITE, c->name)) {
        return;
    }
    Layout(x, c);
    Clear(x);
    GpuBeginPass(&x->gpu);
    const bool recorded = Record(x, c);
    GpuSubmit(&x->gpu);
    Expect(x, c);
    Judge(x, c, recorded);
}

// ---- the cases

static const uint32_t QUADS_REVERSED[] = { 4, 5, 6, 7, 0, 1, 2, 3 };
/// Three indices before the draw's first, then the two quads; with a vertex offset of two, 8
/// reaches the far vertex.
static const uint32_t QUADS_OFFSET[] = { 8, 8, 8, 0, 1, 2, 3, 4, 5, 6, 7 };
/// Two strips, the restart index between them.
static const uint32_t STRIPS_8[] = { 0, 1, 2, 3, 8, 4, 5, 6, 7 };
static const uint32_t STRIPS_FF[] = { 0, 1, 2, 3, 0xFF, 4, 5, 6, 7 };
/// Two fans over the same squares, each from its upper left corner.
static const uint32_t FANS_8[] = { 0, 1, 3, 2, 8, 4, 5, 7, 6 };
/// Two quads, the restart index between them.
static const uint32_t QUADS_8[] = { 0, 1, 2, 3, 8, 4, 5, 6, 7 };
/// The outline's square as two triangles sharing its diagonal from the first corner.
static const uint32_t TWO_TRIANGLES[] = { 0, 1, 2, 0, 2, 3 };

#define POINTS(array) array, (uint32_t)(sizeof(array) / sizeof(array[0]))

static const TopologyCase CASES[] = {
    { "two quads in one draw cover their two rectangles", TOPOLOGY_QUADS, POINTS(TOPOLOGY_QUADS_AT),
      COLOURS_ONE, 0, 0, 0, NULL, 8, 0, 0, false, 0, false, false,
      "two quads of four vertices each" },
    { "a quad takes its flat colour from its fourth vertex", TOPOLOGY_QUADS,
      POINTS(TOPOLOGY_QUADS_AT), COLOURS_EACH, 0, 0, 0, NULL, 8, 0, 0, false, 0, false, false,
      "two quads, each vertex its own colour, the last vertex provoking" },
    { "quads drawn from 8-bit indices cover their rectangles", TOPOLOGY_QUADS,
      POINTS(TOPOLOGY_QUADS_AT), COLOURS_ONE, 0, 0, 1, QUADS_REVERSED, 8, 0, 0, false, 0, false,
      false, "two quads from 8-bit indices, the second quad's first" },
    { "quads drawn from 16-bit indices cover their rectangles", TOPOLOGY_QUADS,
      POINTS(TOPOLOGY_QUADS_AT), COLOURS_ONE, 0, 0, 2, QUADS_REVERSED, 8, 0, 0, false, 0, false,
      false, "two quads from 16-bit indices, the second quad's first" },
    { "quads drawn from 32-bit indices cover their rectangles", TOPOLOGY_QUADS,
      POINTS(TOPOLOGY_QUADS_AT), COLOURS_ONE, 0, 0, 4, QUADS_REVERSED, 8, 0, 0, false, 0, false,
      false, "two quads from 32-bit indices, the second quad's first" },
    { "indexed quads begin at the first index and add the vertex offset", TOPOLOGY_QUADS,
      POINTS(TOPOLOGY_QUADS_AT), COLOURS_ONE, 2, 16, 2, QUADS_OFFSET, 8, 3, 2, false, 0, false,
      false, "two quads from 16-bit indices, first index 3, vertex offset 2" },
    { "a quad draw of six vertices draws one quad and leaves two over", TOPOLOGY_QUADS,
      POINTS(TOPOLOGY_QUADS_AT), COLOURS_ONE, 0, 0, 0, NULL, 6, 0, 0, false, 0, false, false,
      "six vertices drawn as quads, the next two in the buffer finishing a second" },
    { "a quad strip of eight vertices covers its three quads", TOPOLOGY_QUAD_STRIP,
      POINTS(TOPOLOGY_QUAD_STRIP_AT), COLOURS_ONE, 0, 0, 0, NULL, 8, 0, 0, false, 0, false, false,
      "a quad strip of eight vertices" },
    { "a quad strip's quads each take their last vertex's colour", TOPOLOGY_QUAD_STRIP,
      POINTS(TOPOLOGY_QUAD_STRIP_AT), COLOURS_EACH, 0, 0, 0, NULL, 8, 0, 0, false, 0, false, false,
      "a quad strip, each vertex its own colour, the last vertex provoking" },
    { "a polygon of six corners covers the rectangle they bound", TOPOLOGY_POLYGON,
      POINTS(TOPOLOGY_POLYGON_AT), COLOURS_ONE, 0, 0, 0, NULL, 6, 0, 0, false, 0, false, false,
      "a polygon of six corners, two of them on its sides" },
    { "a triangle fan covers the rectangle around its hub", TOPOLOGY_TRIANGLE_FAN,
      POINTS(TOPOLOGY_FAN_AT), COLOURS_ONE, 0, 0, 0, NULL, 6, 0, 0, false, 0, false, false,
      "a fan of four triangles" },
    { "a fan's triangles each take their last vertex's colour", TOPOLOGY_TRIANGLE_FAN,
      POINTS(TOPOLOGY_FAN_AT), COLOURS_EACH, 0, 0, 0, NULL, 6, 0, 0, false, 0, false, false,
      "a fan, each vertex its own colour, the last vertex provoking" },
    { "a strip's triangles each take their last vertex's colour", TOPOLOGY_TRIANGLE_STRIP,
      POINTS(TOPOLOGY_STRIP_AT), COLOURS_EACH, 0, 0, 0, NULL, 6, 0, 0, false, 0, false, false,
      "a strip of four triangles, each vertex its own colour, the last vertex provoking" },
    { "with the first vertex provoking, a strip's triangles take their first vertex's colour",
      TOPOLOGY_TRIANGLE_STRIP, POINTS(TOPOLOGY_STRIP_AT), COLOURS_EACH, 0, 0, 0, NULL, 6, 0, 0,
      false, 0, true, false,
      "a strip of four triangles, each vertex its own colour, the first vertex provoking" },
    { "with the first vertex provoking, a fan's triangles take their first rim vertex's colour",
      TOPOLOGY_TRIANGLE_FAN, POINTS(TOPOLOGY_FAN_AT), COLOURS_EACH, 0, 0, 0, NULL, 6, 0, 0, false,
      0, true, false, "a fan, each vertex its own colour, the first vertex provoking" },
    { "a triangle strip restarts at index 8 of 16-bit indices", TOPOLOGY_TRIANGLE_STRIP,
      POINTS(TOPOLOGY_RESTART_AT), COLOURS_ONE, 0, MAX_VERTICES, 2, STRIPS_8, 9, 0, 0, true, 8,
      false, false, "two strips of two triangles, 16-bit indices, restart index 8" },
    { "a triangle strip restarts at index 8 of 8-bit indices", TOPOLOGY_TRIANGLE_STRIP,
      POINTS(TOPOLOGY_RESTART_AT), COLOURS_ONE, 0, MAX_VERTICES, 1, STRIPS_8, 9, 0, 0, true, 8,
      false, false, "two strips of two triangles, 8-bit indices, restart index 8" },
    { "a triangle strip restarts at index 8 of 32-bit indices", TOPOLOGY_TRIANGLE_STRIP,
      POINTS(TOPOLOGY_RESTART_AT), COLOURS_ONE, 0, MAX_VERTICES, 4, STRIPS_8, 9, 0, 0, true, 8,
      false, false, "two strips of two triangles, 32-bit indices, restart index 8" },
    { "a triangle strip restarts at 0xff of 8-bit indices", TOPOLOGY_TRIANGLE_STRIP,
      POINTS(TOPOLOGY_RESTART_AT), COLOURS_ONE, 0, MAX_VERTICES, 1, STRIPS_FF, 9, 0, 0, true, 0xFF,
      false, false, "two strips of two triangles, 8-bit indices, restart index 0xff" },
    { "with restart off, index 8 is a vertex like any other", TOPOLOGY_TRIANGLE_STRIP,
      POINTS(TOPOLOGY_RESTART_AT), COLOURS_ONE, 0, MAX_VERTICES, 2, STRIPS_8, 9, 0, 0, false, 8,
      false, false, "one strip of seven triangles through vertex 8, 16-bit indices, no restart" },
    { "a triangle fan restarts at index 8 with a hub of its own", TOPOLOGY_TRIANGLE_FAN,
      POINTS(TOPOLOGY_RESTART_AT), COLOURS_ONE, 0, MAX_VERTICES, 2, FANS_8, 9, 0, 0, true, 8, false,
      false, "two fans of two triangles, 16-bit indices, restart index 8" },
    { "indexed quads restart at index 8", TOPOLOGY_QUADS, POINTS(TOPOLOGY_QUADS_AT), COLOURS_ONE, 0,
      16, 2, QUADS_8, 9, 0, 0, true, 8, false, false,
      "two quads, 16-bit indices, restart index 8 between them" },
    { "a line loop closes from its last vertex to its first", TOPOLOGY_LINE_LOOP,
      POINTS(TOPOLOGY_OUTLINE_AT), COLOURS_ONE, 0, 0, 0, NULL, 4, 0, 0, false, 0, false, false,
      "a line loop of four vertices" },
    { "a line strip of the same four vertices leaves that side open", TOPOLOGY_LINE_STRIP,
      POINTS(TOPOLOGY_OUTLINE_AT), COLOURS_ONE, 0, 0, 0, NULL, 4, 0, 0, false, 0, false, false,
      "a line strip of four vertices" },
    { "a quad drawn as lines shows its four sides and no diagonal", TOPOLOGY_QUADS,
      POINTS(TOPOLOGY_OUTLINE_AT), COLOURS_ONE, 0, 0, 0, NULL, 4, 0, 0, false, 0, false, true,
      "a quad, the polygon mode Line" },
    { "a polygon drawn as lines shows its outline and no diagonal", TOPOLOGY_POLYGON,
      POINTS(TOPOLOGY_OUTLINE_AT), COLOURS_ONE, 0, 0, 0, NULL, 4, 0, 0, false, 0, false, true,
      "a polygon of four corners, the polygon mode Line" },
    { "two triangles drawn as lines show the diagonal they share", TOPOLOGY_TRIANGLES,
      POINTS(TOPOLOGY_OUTLINE_AT), COLOURS_ONE, 0, 0, 2, TWO_TRIANGLES, 6, 0, 0, false, 0, false,
      true, "the same corners as two triangles from 16-bit indices, the polygon mode Line" },
};

// ---- notes

/// Which palette colours the pixels inside @p x0..x1, @p y0..y1 hold, as "vertex n: count" words.
static void Tally(const uint8_t *pixels, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                  char *out, size_t size) {
    uint32_t counts[PALETTE_SIZE + 1u];
    memset(counts, 0, sizeof(counts));
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t xx = x0; xx < x1; ++xx) {
            const int32_t p = PaletteOf(GpuPixel(pixels, TARGET_WIDTH, xx, y));
            ++counts[p < 0 ? PALETTE_SIZE : (uint32_t)p];
        }
    }
    size_t used = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i <= PALETTE_SIZE && used < size; ++i) {
        if (counts[i] == 0u) {
            continue;
        }
        const int n = i < PALETTE_SIZE
                          ? snprintf(out + used, size - used, "%svertex %u's %u", used ? ", " : "",
                                     (unsigned)i, (unsigned)counts[i])
                          : snprintf(out + used, size - used, "%sno vertex's %u", used ? ", " : "",
                                     (unsigned)counts[i]);
        used += n > 0 ? (size_t)n : 0u;
    }
}

/// Draws @p c and says which vertices' colours the pixels inside each of the @p count rectangles
/// at @p rects (x0, y0, x1, y1) hold: OpenGL gives a polygon its first vertex's colour under
/// either convention, and quads the convention's vertex when its QUADS_FOLLOW_PROVOKING_VERTEX
/// is true, but nothing says the 3D class does the same.
static void ProvokingNote(TopologyContext *x, const TopologyCase *c, const char *what,
                          const uint32_t (*rects)[4], uint32_t count) {
    Layout(x, c);
    Clear(x);
    GpuBeginPass(&x->gpu);
    const bool recorded = Record(x, c);
    GpuSubmit(&x->gpu);
    const uint8_t *const pixels = recorded && x->gpu.ready ? GpuReadBack(&x->gpu) : NULL;
    if (pixels == NULL) {
        ReportNote("topology %s: no readback (%s)", what,
                   x->gpu.failure[0] != '\0' ? x->gpu.failure : "the list was not recorded");
        return;
    }
    char text[2][160];
    for (uint32_t i = 0; i < count && i < 2u; ++i) {
        Tally(pixels, rects[i][0], rects[i][1], rects[i][2], rects[i][3], text[i], sizeof(text[i]));
    }
    if (count == 1u) {
        ReportNote("topology %s: %s", what, text[0]);
    } else {
        ReportNote("topology %s: the first %s; the second %s", what, text[0], text[1]);
    }
}

static void Notes(TopologyContext *x) {
    static const TopologyCase POLYGON = { "a polygon's flat colour",
                                          TOPOLOGY_POLYGON,
                                          POINTS(TOPOLOGY_POLYGON_AT),
                                          COLOURS_EACH,
                                          0,
                                          0,
                                          0,
                                          NULL,
                                          6,
                                          0,
                                          0,
                                          false,
                                          0,
                                          false,
                                          false,
                                          "" };
    static const uint32_t POLYGON_RECT[1][4] = { { 12, 16, 52, 48 } };
    ProvokingNote(x, &POLYGON, "polygon, the last vertex provoking, six corners", POLYGON_RECT, 1u);
    static const TopologyCase QUADS_FIRST = { "a quad's flat colour under the first vertex",
                                              TOPOLOGY_QUADS,
                                              POINTS(TOPOLOGY_QUADS_AT),
                                              COLOURS_EACH,
                                              0,
                                              0,
                                              0,
                                              NULL,
                                              8,
                                              0,
                                              0,
                                              false,
                                              0,
                                              true,
                                              false,
                                              "" };
    static const uint32_t QUAD_RECTS[2][4] = { { 8, 8, 28, 24 }, { 36, 32, 56, 56 } };
    ProvokingNote(x, &QUADS_FIRST, "quads, the first vertex provoking, vertices 0 to 3 and 4 to 7",
                  QUAD_RECTS, 2u);
}

void RunTopology(void) {
    TopologyContext *const x = &context;
    static const char *const OPEN = "graphics device opens and the shaders load";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    // Every case's vertices and indices stay where they were written (GpuAllocData never gives
    // any back): about 9 KiB for the cases with a 256 vertex buffer, 1 KiB for the rest.
    spec.data_size = 512u * 1024u;
    const bool opened = GpuOpenSpec(&x->gpu, &spec);
    const bool loaded = opened &&
                        GpuLoadShader(&x->gpu, &x->vertex, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&x->gpu, &x->flat, ShaderData(SHADER_FLAT_FRAG),
                                      ShaderSize(SHADER_FLAT_FRAG));
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, loaded, "%s", x->gpu.failure);
    if (!loaded) {
        if (opened) {
            GpuClose(&x->gpu);
        }
        return;
    }
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); ++i) {
        RunCase(x, &CASES[i]);
    }
    Notes(x);
    GpuClose(&x->gpu);
}
