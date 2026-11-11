// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "../vertexref.h"
#include "tests.h"

// How the 3D class fetches a vertex attribute, where Vulkan asks more of the layout than the
// guest's driver did: a stride that is no multiple of the components' size, poison between
// vertices, an attribute whose offset passes the stride into the next vertex's bytes, the
// components a format leaves out, BGRA order, scaled integers, a packed 10_10_10_2 word and a per
// instance attribute that steps every third instance. An emulator restrides, repacks or splits
// such a stream into a layout its host takes, and each case is a place where that goes wrong.
//
// Every case draws one point per vertex at the pixel the vertex's own number names, on a 16 by 16
// RGBA32F target, carrying the attribute as fetched; the target holds floats, so every value is
// judged bit for bit, every pixel the draw does not reach as the clear. What an attribute gives
// from its bytes is vertexref.c's, pinned by the reference suite. One thing only a console
// settles is a note: what a fixed attribute, which deko3d leaves every unbound one as, reads.

static const char *const SUITE = "vertex";

#define SIDE 16u
#define PIXELS (SIDE * SIDE)
#define MAX_POINTS 16u
#define BUFFER_BYTES 2048u
/// What the bytes between and around the vertices hold: a quiet NaN's pattern, which no case
/// expects in any component.
#define POISON 0xffu

typedef struct {
    const char *name;
    VertexLayout layout;
    bool bgra;
    uint32_t offset;
    uint32_t stride;
    /// Nought for a per vertex attribute; the instances one element lasts otherwise.
    uint32_t divisor;
    uint32_t vertices;
    uint32_t instances;
    const char *what;
} VertexCase;

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader flat;
    uint8_t bytes[BUFFER_BYTES];
    uint32_t expected[PIXELS * 4u];
} VertexContext;

static VertexContext context;

static const float CLEAR[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

static void PutWord(uint8_t *at, uint32_t word) {
    at[0] = (uint8_t)word, at[1] = (uint8_t)(word >> 8), at[2] = (uint8_t)(word >> 16);
    at[3] = (uint8_t)(word >> 24);
}

/// Element @p e's bytes for @p layout: values of their own per element and component, each exact
/// in the format (singles with a fraction, whole numbers, nought or full).
static void FillElement(uint8_t *at, VertexLayout layout, uint32_t e) {
    switch (layout) {
    case VERTEX_RGBA32F:
    case VERTEX_RGB32F:
    case VERTEX_RG32F:
    case VERTEX_R32F:
        for (uint32_t c = 0; c < VertexLayoutBytes(layout) / 4u; ++c) {
            // One and a fraction, e sixteenths and c sixty-fourths, signed by component:
            // distinct for every element under 16 and component, and exact as singles.
            const uint32_t bits = (c & 1u ? 0x80000000u : 0u) | 0x3f800000u | (e << 19) | (c << 17);
            PutWord(at + c * 4u, bits);
        }
        break;
    case VERTEX_RGBA8_UNORM:
        // Each byte nought or full, by the element's bits and the component.
        for (uint32_t c = 0; c < 4u; ++c) {
            at[c] = ((e >> c) & 1u) != 0u ? 0xffu : 0x00u;
        }
        break;
    case VERTEX_RGBA16_SSCALED: {
        const int16_t values[4] = { (int16_t)(-32768 + (int32_t)e), (int16_t)(-1 - (int32_t)e),
                                    (int16_t)(1234 + (int32_t)e * 7),
                                    (int16_t)(32767 - (int32_t)e) };
        for (uint32_t c = 0; c < 4u; ++c) {
            at[c * 2u] = (uint8_t)(uint16_t)values[c];
            at[c * 2u + 1u] = (uint8_t)((uint16_t)values[c] >> 8);
        }
        break;
    }
    case VERTEX_RGBA8_USCALED:
        at[0] = (uint8_t)e, at[1] = (uint8_t)(255u - e), at[2] = (uint8_t)(128u + e);
        at[3] = (uint8_t)(e * 13u);
        break;
    case VERTEX_RGB10A2_UNORM: {
        // Each 10-bit field nought or full and the 2-bit one nought or 3, by the element's bits.
        const uint32_t word = ((e & 1u) ? 0x3ffu : 0u) | ((e & 2u) ? 0x3ffu << 10 : 0u) |
                              ((e & 4u) ? 0x3ffu << 20 : 0u) | ((e & 8u) ? 3u << 30 : 0u);
        PutWord(at, word);
        break;
    }
    }
}

static DkVtxAttribSize Size(VertexLayout layout) {
    switch (layout) {
    case VERTEX_RGBA32F:
        return DkVtxAttribSize_4x32;
    case VERTEX_RGB32F:
        return DkVtxAttribSize_3x32;
    case VERTEX_RG32F:
        return DkVtxAttribSize_2x32;
    case VERTEX_R32F:
        return DkVtxAttribSize_1x32;
    case VERTEX_RGBA16_SSCALED:
        return DkVtxAttribSize_4x16;
    case VERTEX_RGB10A2_UNORM:
        return DkVtxAttribSize_10_10_10_2;
    default:
        return DkVtxAttribSize_4x8;
    }
}

static DkVtxAttribType Type(VertexLayout layout) {
    switch (layout) {
    case VERTEX_RGBA8_UNORM:
    case VERTEX_RGB10A2_UNORM:
        return DkVtxAttribType_Unorm;
    case VERTEX_RGBA16_SSCALED:
        return DkVtxAttribType_Sscaled;
    case VERTEX_RGBA8_USCALED:
        return DkVtxAttribType_Uscaled;
    default:
        return DkVtxAttribType_Float;
    }
}

static void ExpectClear(VertexContext *x) {
    for (uint32_t i = 0; i < PIXELS; ++i) {
        memcpy(&x->expected[i * 4u], CLEAR, sizeof(CLEAR));
    }
}

/// Clears the target, draws @p vertices points of @p instances instances from the buffer at
/// @p address with @p attribute and @p buffer, and reads the floats back.
static const uint8_t *Draw(VertexContext *x, const DkVtxAttribState *attribute,
                           const DkVtxBufferState *buffer, DkGpuAddr address, uint32_t vertices,
                           uint32_t instances) {
    GpuClear(&x->gpu, CLEAR[0], CLEAR[1], CLEAR[2], CLEAR[3]);
    GpuBeginPass(&x->gpu);
    const DkShader *const shaders[] = { &x->vertex, &x->flat };
    dkCmdBufBindShaders(x->gpu.cmdbuf, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
    dkCmdBufBindVtxAttribState(x->gpu.cmdbuf, attribute, attribute != NULL ? 1u : 0u);
    dkCmdBufBindVtxBufferState(x->gpu.cmdbuf, buffer, 1);
    dkCmdBufBindVtxBuffer(x->gpu.cmdbuf, 0, address, BUFFER_BYTES);
    dkCmdBufSetPointSize(x->gpu.cmdbuf, 1.0f);
    dkCmdBufDraw(x->gpu.cmdbuf, DkPrimitive_Points, vertices, instances, 0, 0);
    GpuSubmit(&x->gpu);
    return x->gpu.ready ? GpuReadBackImageBytes(&x->gpu, &x->gpu.image, SIDE, SIDE, 16u) : NULL;
}

static void RunCase(VertexContext *x, const VertexCase *c) {
    if (!ReportStart(SUITE, c->name)) {
        return;
    }
    DkGpuAddr address = 0;
    uint8_t *const cpu = (uint8_t *)GpuAllocData(&x->gpu, BUFFER_BYTES, 256u, &address);
    if (cpu == NULL) {
        ReportCase(SUITE, c->name, CHECK_NONE, false, "no scratch left for the vertices");
        return;
    }
    memset(x->bytes, POISON, sizeof(x->bytes));
    const uint32_t elements =
        c->divisor != 0u ? (c->instances + c->divisor - 1u) / c->divisor : c->vertices;
    for (uint32_t e = 0; e < elements; ++e) {
        FillElement(x->bytes + VertexAddress(e, c->stride, c->offset, 0), c->layout, e);
    }
    memcpy(cpu, x->bytes, BUFFER_BYTES);
    ExpectClear(x);
    bool exact = true;
    for (uint32_t i = 0; i < c->instances; ++i) {
        for (uint32_t v = 0; v < c->vertices; ++v) {
            const uint32_t at = c->divisor != 0u
                                    ? VertexAddress(i, c->stride, c->offset, c->divisor)
                                    : VertexAddress(v, c->stride, c->offset, 0);
            exact = VertexExpected(c->layout, c->bgra, x->bytes + at,
                                   &x->expected[VertexPixel(v, i) * 4u]) &&
                    exact;
        }
    }
    const DkVtxAttribState attribute = { .bufferId = 0,
                                         .isFixed = 0,
                                         .offset = c->offset,
                                         .size = Size(c->layout),
                                         .type = Type(c->layout),
                                         .isBgra = c->bgra ? 1u : 0u };
    const DkVtxBufferState buffer = { c->stride, c->divisor };
    const uint8_t *const got = Draw(x, &attribute, &buffer, address, c->vertices, c->instances);
    if (got == NULL || !exact) {
        ReportCase(SUITE, c->name, CHECK_NONE, false,
                   !exact                      ? "a value the case wrote has no exact float"
                   : x->gpu.failure[0] != '\0' ? x->gpu.failure
                                               : "the readback handed back nothing");
        return;
    }
    uint32_t wrong = 0, first = 0;
    for (uint32_t p = 0; p < PIXELS; ++p) {
        if (memcmp(got + p * 16u, &x->expected[p * 4u], 16u) != 0) {
            first = wrong == 0u ? p : first;
            ++wrong;
        }
    }
    uint32_t have[4];
    memcpy(have, got + first * 16u, sizeof(have));
    const uint32_t *const want = &x->expected[first * 4u];
    CHECK_TRUE(SUITE, c->name, CHECK_EXACT, wrong == 0u,
               "%s: %u of %u pixels wrong; the first, vertex %u's pixel (%u, %u), holds %08x %08x "
               "%08x %08x, expected %08x %08x %08x %08x",
               c->what, (unsigned)wrong, (unsigned)PIXELS, (unsigned)first,
               (unsigned)(first % SIDE), (unsigned)(first / SIDE), (unsigned)have[0],
               (unsigned)have[1], (unsigned)have[2], (unsigned)have[3], (unsigned)want[0],
               (unsigned)want[1], (unsigned)want[2], (unsigned)want[3]);
}

static const VertexCase CASES[] = {
    { "a stride of 69 bytes, no multiple of four, reads each RGBA32F vertex from its own place",
      VERTEX_RGBA32F, false, 0, 69, 0, 8, 1, "eight vertices 69 bytes apart, poison between" },
    { "a stride of 48 reads 16 bytes at offset 8 of each vertex and none of the poison around them",
      VERTEX_RGBA32F, false, 8, 48, 0, 8, 1, "eight vertices 48 bytes apart, the attribute at 8" },
    { "an attribute at offset 40 of a 32 byte stride reads into the next vertex's bytes",
      VERTEX_RGBA32F, false, 40, 32, 0, 8, 1,
      "eight vertices 32 bytes apart, the attribute at 40" },
    { "an RGB32F attribute reads one in the w it lacks", VERTEX_RGB32F, false, 0, 12, 0, 8, 1,
      "eight RGB32F vertices, packed" },
    { "an RG32F attribute reads nought and one in the z and w it lacks", VERTEX_RG32F, false, 0, 8,
      0, 8, 1, "eight RG32F vertices, packed" },
    { "an R32F attribute reads nought, nought and one in the y, z and w it lacks", VERTEX_R32F,
      false, 0, 4, 0, 8, 1, "eight R32F vertices, packed" },
    { "an RGBA8 attribute in BGRA order takes red from the third byte", VERTEX_RGBA8_UNORM, true, 0,
      4, 0, 16, 1, "sixteen RGBA8 unsigned normalised vertices of nought and full, BGRA" },
    { "16-bit signed scaled attributes read whole numbers of both signs", VERTEX_RGBA16_SSCALED,
      false, 0, 8, 0, 8, 1, "eight 4x16 signed scaled vertices, -32768 to 32767" },
    { "8-bit unsigned scaled attributes read whole numbers to 255", VERTEX_RGBA8_USCALED, false, 0,
      4, 0, 8, 1, "eight 4x8 unsigned scaled vertices" },
    { "a 10_10_10_2 unsigned normalised attribute reads nought and one in each field",
      VERTEX_RGB10A2_UNORM, false, 0, 4, 0, 16, 1,
      "sixteen 10_10_10_2 vertices, each field nought or full" },
    { "a per instance attribute with divisor 3 steps every third instance", VERTEX_RGBA32F, false,
      0, 16, 3, 1, 9, "one vertex of nine instances, the attribute per instance, divisor 3" },
};

/// What an attribute deko3d leaves fixed (IsFixed, size 1x32, a float) gives the shader: no
/// buffer is read, and the value is the 3D class's, which no specification here sets.
static void FixedNote(VertexContext *x) {
    const DkVtxBufferState buffer = { 16u, 0u };
    DkGpuAddr address = 0;
    if (GpuAllocData(&x->gpu, BUFFER_BYTES, 256u, &address) == NULL) {
        ReportNote("vertex fixed attribute: no scratch left");
        return;
    }
    const uint8_t *const got = Draw(x, NULL, &buffer, address, 1, 1);
    if (got == NULL) {
        ReportNote("vertex fixed attribute: no readback (%s)", x->gpu.failure);
        return;
    }
    uint32_t have[4];
    memcpy(have, got, sizeof(have));
    ReportNote("vertex fixed attribute: an unbound attribute reads %08x %08x %08x %08x "
               "(0 0 0 3f800000 is OpenGL's default, all nought is a constant of nought)",
               (unsigned)have[0], (unsigned)have[1], (unsigned)have[2], (unsigned)have[3]);
}

void RunVertex(void) {
    VertexContext *const x = &context;
    static const char *const OPEN =
        "graphics device opens with an RGBA32F target and the shaders load";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    GpuSpec spec;
    GpuSpecDefaults(&spec, SIDE, SIDE);
    spec.color_format = DkImageFormat_RGBA32_Float;
    spec.code_size = 32u * 1024u;
    spec.data_size = 256u * 1024u;
    const bool opened = GpuOpenSpec(&x->gpu, &spec);
    const bool loaded = opened &&
                        GpuLoadShader(&x->gpu, &x->vertex, ShaderData(SHADER_VERTEX_VALUE_VERT),
                                      ShaderSize(SHADER_VERTEX_VALUE_VERT)) &&
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
    FixedNote(x);
    GpuClose(&x->gpu);
}
