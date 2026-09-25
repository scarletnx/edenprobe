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

// A compute dispatch writes a buffer, a wait for idle follows, and the next command reads the
// buffer as vertices, indices, indirect arguments, a uniform buffer, a storage buffer, or the
// grid of another dispatch. Each reader must see the written words.
//
// The barrier is DkBarrier_Primitives, which the library documents as "completes the processing
// of all previous primitives and compute jobs" and emits as the 3D engine's WaitForIdle method,
// the same pattern deko3d's own compute example uses between the dispatch that generates
// vertices and the draw that consumes them (switch-examples, deko_examples,
// Example09_SimpleCompute.cpp). The shader-cache invalidation is added for the readers that go
// through a shader's data or uniform caches.
//
// The race is made as wide as the dispatch allows: every invocation spins before the copy, and
// only the last invocations copy, so a reader not ordered after the whole dispatch sees the
// buffer as it was. What it was is chosen so that a stale read draws nothing, launches nothing
// or draws the wrong word, and every case counts the pixels or words that still hold it.

static const char *const SUITE = "order";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)

// Invocations that spin before the copy, and how long. About a millisecond of work on a desktop
// graphics unit and a few tens on the console's; tune down only if a run times out.
#define COPY_GROUPS 256u
#define COPY_SPIN 20000u

// The word the readers are asked to see. Four different bytes, none nought, so a pixel that
// kept the clear (nought) or saw a zeroed buffer can never pass for it.
#define MARKER 0x7F3C19E5u

// Groups the indirect dispatch launches, and the word it leaves in every group's slot.
#define MARK_GROUPS 64u
#define MARK_BASE 0x51000000u

#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

typedef struct {
    uint32_t count;
    uint32_t spin;
    uint32_t zero;
    uint32_t pad;
} CopyParams;

typedef struct {
    GpuContext gpu;
    DkShader copy;
    DkShader mark;
    DkShader position;
    DkShader value_ubo;
    DkShader value_ssbo;
    DkShader store_ssbo;
    DkShader common_vert;
} OrderContext;

/// A buffer in scratch, the processor's pointer and the graphics unit's address of it.
typedef struct {
    uint32_t *cpu;
    DkGpuAddr gpu;
    uint32_t size;
} Scratch;

static bool Alloc(OrderContext *o, Scratch *out, uint32_t size, uint32_t alignment) {
    out->size = size;
    out->cpu = (uint32_t *)GpuAllocData(&o->gpu, size, alignment, &out->gpu);
    return out->cpu != NULL;
}

/// Records the dispatch that copies @p words words of @p source into @p destination late.
static bool RecordLateCopy(OrderContext *o, const Scratch *source, const Scratch *destination,
                           uint32_t words, uint32_t spin, uint32_t groups) {
    Scratch params;
    if (!Alloc(o, &params, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT)) {
        return false;
    }
    const CopyParams values = { words, spin, 0u, 0u };
    memcpy(params.cpu, &values, sizeof(values));
    const DkShader *const shaders[] = { &o->copy };
    dkCmdBufBindShaders(o->gpu.cmdbuf, DkStageFlag_Compute, shaders, 1);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Compute, 0, params.gpu, UNIFORM_SIZE);
    dkCmdBufBindStorageBuffer(o->gpu.cmdbuf, DkStage_Compute, 0, source->gpu, source->size);
    dkCmdBufBindStorageBuffer(o->gpu.cmdbuf, DkStage_Compute, 1, destination->gpu,
                              destination->size);
    dkCmdBufDispatchCompute(o->gpu.cmdbuf, groups, 1, 1);
    return true;
}

/// The wait for idle every case puts between the writer and the reader.
static void WaitForIdle(OrderContext *o) {
    dkCmdBufBarrier(o->gpu.cmdbuf, DkBarrier_Primitives, DkInvalidateFlags_Shader);
}

/// Binds position.vert with one vec4 attribute read from @p vertices.
static void BindPositions(OrderContext *o, const DkShader *fragment, DkGpuAddr vertices,
                          uint32_t size) {
    const DkShader *const shaders[] = { &o->position, fragment };
    dkCmdBufBindShaders(o->gpu.cmdbuf, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
    static const DkVtxAttribState attribute = { .bufferId = 0,
                                                .isFixed = 0,
                                                .offset = 0,
                                                .size = DkVtxAttribSize_4x32,
                                                .type = DkVtxAttribType_Float,
                                                .isBgra = 0 };
    static const DkVtxBufferState buffer_state = { 16, 0 };
    dkCmdBufBindVtxAttribState(o->gpu.cmdbuf, &attribute, 1);
    dkCmdBufBindVtxBufferState(o->gpu.cmdbuf, &buffer_state, 1);
    dkCmdBufBindVtxBuffer(o->gpu.cmdbuf, 0, vertices, size);
}

static uint32_t FloatBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// Two triangles over the whole target as vec4 positions, 24 words.
static void FullScreenTriangles(uint32_t out[24]) {
    static const float corners[6][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f },
                                         { -1.0f, -1.0f }, { 1.0f, 1.0f },  { -1.0f, 1.0f } };
    for (uint32_t i = 0; i < 6u; ++i) {
        out[i * 4u + 0u] = FloatBits(corners[i][0]);
        out[i * 4u + 1u] = FloatBits(corners[i][1]);
        out[i * 4u + 2u] = FloatBits(0.0f);
        out[i * 4u + 3u] = FloatBits(1.0f);
    }
}

/// The four corners of the target as vec4 positions, 16 words, for indexed draws.
static void FourCorners(uint32_t out[16]) {
    static const float corners[4][2] = {
        { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }
    };
    for (uint32_t i = 0; i < 4u; ++i) {
        out[i * 4u + 0u] = FloatBits(corners[i][0]);
        out[i * 4u + 1u] = FloatBits(corners[i][1]);
        out[i * 4u + 2u] = FloatBits(0.0f);
        out[i * 4u + 3u] = FloatBits(1.0f);
    }
}

/// Every pixel must hold @p word's four bytes. Counts the ones still holding the clear (nought),
/// which is what a reader that ran before the writer leaves.
static void JudgeTarget(OrderContext *o, const char *name, uint32_t word, bool drawn) {
    if (!drawn || !o->gpu.ready) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   o->gpu.failure[0] != '\0' ? o->gpu.failure : "the list was not recorded");
        return;
    }
    const uint8_t *const pixels = GpuReadBack(&o->gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   o->gpu.failure[0] != '\0' ? o->gpu.failure : "the readback handed back nothing");
        return;
    }
    const uint8_t want[4] = { (uint8_t)word, (uint8_t)(word >> 8), (uint8_t)(word >> 16),
                              (uint8_t)(word >> 24) };
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t cleared = 0;
    uint8_t first[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            const bool right = memcmp(pixel, want, 4) == 0;
            PictureSet(picture, x, y, want, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (right) {
                continue;
            }
            if (wrong == 0) {
                memcpy(first, pixel, 4);
            }
            ++wrong;
            if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0) {
                ++cleared;
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong (%u still hold the clear, what a read of the buffer "
               "before the dispatch finished draws); first is %02x%02x%02x%02x, expected "
               "%02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)cleared, first[0], first[1],
               first[2], first[3], want[0], want[1], want[2], want[3]);
}

/// The words at @p got must equal @p want. @p stale counts the ones equal to @p old.
static void JudgeWords(const char *name, const uint32_t *got, const uint32_t *want,
                       uint32_t count, uint32_t old, bool ready, const char *failure) {
    if (!ready) {
        ReportCase(SUITE, name, CHECK_NONE, false, failure);
        return;
    }
    uint32_t wrong = 0;
    uint32_t stale = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (got[i] == want[i]) {
            continue;
        }
        if (wrong == 0) {
            first = i;
        }
        ++wrong;
        if (got[i] == old) {
            ++stale;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u words wrong (%u still hold the value from before), first at %u is "
               "0x%08x, expected 0x%08x",
               (unsigned)wrong, (unsigned)count, (unsigned)stale, (unsigned)first,
               (unsigned)got[first], (unsigned)want[first]);
}

/// A fresh list over a target cleared to nought.
static void Begin(OrderContext *o) {
    GpuClear(&o->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginList(&o->gpu);
}

static void CaseVertices(OrderContext *o) {
    const char *const name = "vertices a dispatch wrote before a wait for idle reach the draw";
    Scratch source, destination, uniform;
    Begin(o);
    if (!Alloc(o, &source, 96, 256) || !Alloc(o, &destination, 96, 256) ||
        !Alloc(o, &uniform, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FullScreenTriangles(source.cpu);
    memset(destination.cpu, 0, 96);
    uniform.cpu[0] = MARKER;
    bool ok = RecordLateCopy(o, &source, &destination, 24, COPY_SPIN, COPY_GROUPS);
    WaitForIdle(o);
    BindPositions(o, &o->value_ubo, destination.gpu, 96);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, uniform.gpu, UNIFORM_SIZE);
    dkCmdBufDraw(o->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
    GpuSubmit(&o->gpu);
    JudgeTarget(o, name, MARKER, ok);
}

static void CaseIndices(OrderContext *o) {
    const char *const name = "indices a dispatch wrote before a wait for idle reach the draw";
    Scratch vertices, source, destination, uniform;
    Begin(o);
    if (!Alloc(o, &vertices, 64, 256) || !Alloc(o, &source, 64, 256) ||
        !Alloc(o, &destination, 64, 256) ||
        !Alloc(o, &uniform, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FourCorners(vertices.cpu);
    static const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    memcpy(source.cpu, indices, sizeof(indices));
    memset(destination.cpu, 0, 64);
    uniform.cpu[0] = MARKER;
    bool ok = RecordLateCopy(o, &source, &destination, 6, COPY_SPIN, COPY_GROUPS);
    WaitForIdle(o);
    BindPositions(o, &o->value_ubo, vertices.gpu, 64);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, uniform.gpu, UNIFORM_SIZE);
    dkCmdBufBindIdxBuffer(o->gpu.cmdbuf, DkIdxFormat_Uint32, destination.gpu);
    dkCmdBufDrawIndexed(o->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0, 0);
    GpuSubmit(&o->gpu);
    JudgeTarget(o, name, MARKER, ok);
}

static void CaseDrawIndirect(OrderContext *o) {
    const char *const name =
        "draw arguments a dispatch wrote before a wait for idle reach the indirect draw";
    Scratch vertices, source, destination, uniform;
    Begin(o);
    if (!Alloc(o, &vertices, 96, 256) || !Alloc(o, &source, 16, 256) ||
        !Alloc(o, &destination, 16, 256) ||
        !Alloc(o, &uniform, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FullScreenTriangles(vertices.cpu);
    // DkDrawIndirectData: vertex count, instance count, first vertex, first instance. Before the
    // dispatch it asks for no vertices, which draws nothing.
    const uint32_t args[4] = { 6u, 1u, 0u, 0u };
    const uint32_t before[4] = { 0u, 1u, 0u, 0u };
    memcpy(source.cpu, args, sizeof(args));
    memcpy(destination.cpu, before, sizeof(before));
    uniform.cpu[0] = MARKER;
    bool ok = RecordLateCopy(o, &source, &destination, 4, COPY_SPIN, COPY_GROUPS);
    WaitForIdle(o);
    BindPositions(o, &o->value_ubo, vertices.gpu, 96);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, uniform.gpu, UNIFORM_SIZE);
    dkCmdBufDrawIndirect(o->gpu.cmdbuf, DkPrimitive_Triangles, destination.gpu);
    GpuSubmit(&o->gpu);
    JudgeTarget(o, name, MARKER, ok);
}

static void CaseUniform(OrderContext *o) {
    const char *const name =
        "a uniform buffer a dispatch wrote before a wait for idle reaches the fragments";
    Scratch vertices, source, destination;
    Begin(o);
    if (!Alloc(o, &vertices, 96, 256) || !Alloc(o, &source, 16, 256) ||
        !Alloc(o, &destination, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FullScreenTriangles(vertices.cpu);
    const uint32_t word[4] = { MARKER, 0u, 0u, 0u };
    memcpy(source.cpu, word, sizeof(word));
    memset(destination.cpu, 0, UNIFORM_SIZE);
    bool ok = RecordLateCopy(o, &source, &destination, 4, COPY_SPIN, COPY_GROUPS);
    WaitForIdle(o);
    BindPositions(o, &o->value_ubo, vertices.gpu, 96);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, destination.gpu,
                              UNIFORM_SIZE);
    dkCmdBufDraw(o->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
    GpuSubmit(&o->gpu);
    JudgeTarget(o, name, MARKER, ok);
}

static void CaseStorage(OrderContext *o) {
    const char *const name =
        "a storage buffer a dispatch wrote before a wait for idle reaches the fragments";
    Scratch vertices, source, destination;
    Begin(o);
    if (!Alloc(o, &vertices, 96, 256) || !Alloc(o, &source, 16, 256) ||
        !Alloc(o, &destination, 16, 256)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FullScreenTriangles(vertices.cpu);
    const uint32_t word[4] = { MARKER, 0u, 0u, 0u };
    memcpy(source.cpu, word, sizeof(word));
    memset(destination.cpu, 0, 16);
    bool ok = RecordLateCopy(o, &source, &destination, 4, COPY_SPIN, COPY_GROUPS);
    WaitForIdle(o);
    BindPositions(o, &o->value_ssbo, vertices.gpu, 96);
    dkCmdBufBindStorageBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, destination.gpu, 16);
    dkCmdBufDraw(o->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
    GpuSubmit(&o->gpu);
    JudgeTarget(o, name, MARKER, ok);
}

static void CaseDispatchIndirect(OrderContext *o) {
    const char *const name =
        "a grid a dispatch wrote before a wait for idle sizes the indirect dispatch";
    Scratch source, destination, params, marks;
    Begin(o);
    if (!Alloc(o, &source, 16, 256) || !Alloc(o, &destination, 16, 256) ||
        !Alloc(o, &params, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT) ||
        !Alloc(o, &marks, (MARK_GROUPS + 1u) * 4u, 256)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    // DkDispatchIndirectData: three group counts. Before the dispatch the grid is empty.
    const uint32_t grid[4] = { MARK_GROUPS, 1u, 1u, 0u };
    const uint32_t before[4] = { 0u, 1u, 1u, 0u };
    memcpy(source.cpu, grid, sizeof(grid));
    memcpy(destination.cpu, before, sizeof(before));
    memset(marks.cpu, 0, (MARK_GROUPS + 1u) * 4u);
    params.cpu[0] = MARK_BASE;
    bool ok = RecordLateCopy(o, &source, &destination, 3, COPY_SPIN, COPY_GROUPS);
    WaitForIdle(o);
    const DkShader *const shaders[] = { &o->mark };
    dkCmdBufBindShaders(o->gpu.cmdbuf, DkStageFlag_Compute, shaders, 1);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Compute, 0, params.gpu, UNIFORM_SIZE);
    dkCmdBufBindStorageBuffer(o->gpu.cmdbuf, DkStage_Compute, 0, marks.gpu,
                              (MARK_GROUPS + 1u) * 4u);
    dkCmdBufDispatchComputeIndirect(o->gpu.cmdbuf, destination.gpu);
    GpuSubmit(&o->gpu);
    uint32_t want[MARK_GROUPS + 1u];
    for (uint32_t g = 0; g < MARK_GROUPS; ++g) {
        want[g] = MARK_BASE + g;
    }
    // One past the grid stays nought: a grid larger than written would reach it.
    want[MARK_GROUPS] = 0u;
    JudgeWords(name, marks.cpu, want, MARK_GROUPS + 1u, 0u, ok && o->gpu.ready,
               o->gpu.failure[0] != '\0' ? o->gpu.failure : "the list was not recorded");
}

static void CaseComputeToCompute(OrderContext *o) {
    const char *const name =
        "words a dispatch wrote before a wait for idle reach the next dispatch";
    Scratch source, middle, destination;
    Begin(o);
    if (!Alloc(o, &source, 1024, 256) || !Alloc(o, &middle, 1024, 256) ||
        !Alloc(o, &destination, 1024, 256)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    for (uint32_t i = 0; i < 256u; ++i) {
        source.cpu[i] = MARKER ^ (i * 0x01010101u);
    }
    memset(middle.cpu, 0, 1024);
    memset(destination.cpu, 0, 1024);
    bool ok = RecordLateCopy(o, &source, &middle, 256, COPY_SPIN, COPY_GROUPS);
    WaitForIdle(o);
    ok = ok && RecordLateCopy(o, &middle, &destination, 256, 0u, 4u);
    GpuSubmit(&o->gpu);
    JudgeWords(name, destination.cpu, source.cpu, 256, 0u, ok && o->gpu.ready,
               o->gpu.failure[0] != '\0' ? o->gpu.failure : "the list was not recorded");
}

static void CaseFragmentToCompute(OrderContext *o) {
    const char *const name =
        "words the fragments stored before a wait for idle reach the next dispatch";
    Scratch vertices, params, stored, copied;
    Begin(o);
    if (!Alloc(o, &vertices, 96, 256) || !Alloc(o, &params, UNIFORM_SIZE,
                                                 DK_UNIFORM_BUF_ALIGNMENT) ||
        !Alloc(o, &stored, PIXEL_COUNT * 4u, 256) || !Alloc(o, &copied, PIXEL_COUNT * 4u, 256)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FullScreenTriangles(vertices.cpu);
    params.cpu[0] = MARKER;
    params.cpu[1] = TARGET_WIDTH;
    memset(stored.cpu, 0, PIXEL_COUNT * 4u);
    memset(copied.cpu, 0, PIXEL_COUNT * 4u);
    BindPositions(o, &o->store_ssbo, vertices.gpu, 96);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, params.gpu, UNIFORM_SIZE);
    dkCmdBufBindStorageBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, stored.gpu, PIXEL_COUNT * 4u);
    dkCmdBufDraw(o->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
    WaitForIdle(o);
    bool ok = RecordLateCopy(o, &stored, &copied, PIXEL_COUNT, 0u, PIXEL_COUNT / 64u);
    GpuSubmit(&o->gpu);
    static uint32_t want[PIXEL_COUNT];
    for (uint32_t i = 0; i < PIXEL_COUNT; ++i) {
        want[i] = MARKER + i;
    }
    JudgeWords(name, copied.cpu, want, PIXEL_COUNT, 0u, ok && o->gpu.ready,
               o->gpu.failure[0] != '\0' ? o->gpu.failure : "the list was not recorded");
}

// The cost cases: the same dispatch and draw pairs with and without a wait for idle between
// them. Their times, not their pixels, are the point; the ordered one is judged like the rest.
#define PAIRS 128u

static void PairsCase(OrderContext *o, bool ordered) {
    const char *const name =
        ordered ? "128 dispatch and draw pairs, each ordered by a wait for idle, all draw"
                : "128 dispatch and draw pairs with no wait for idle between them are recorded";
    Scratch source, destination, vertices;
    Begin(o);
    if (!Alloc(o, &source, 16, 256) || !Alloc(o, &destination, PAIRS * 256u, 256) ||
        !Alloc(o, &vertices, 96, 256)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FullScreenTriangles(vertices.cpu);
    const uint32_t word[4] = { MARKER, 0u, 0u, 0u };
    memcpy(source.cpu, word, sizeof(word));
    memset(destination.cpu, 0, PAIRS * 256u);
    bool ok = true;
    for (uint32_t i = 0; i < PAIRS && ok; ++i) {
        Scratch slot = { destination.cpu + i * 64u, destination.gpu + i * 256u, 16u };
        ok = RecordLateCopy(o, &source, &slot, 4, 0u, 1u);
        if (ordered) {
            WaitForIdle(o);
        }
        BindPositions(o, &o->value_ssbo, vertices.gpu, 96);
        dkCmdBufBindStorageBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, slot.gpu, 16);
        dkCmdBufDraw(o->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
    }
    GpuSubmit(&o->gpu);
    if (ordered) {
        JudgeTarget(o, name, MARKER, ok);
    } else {
        CHECK_TRUE(SUITE, name, CHECK_NONE, ok && o->gpu.ready, "%s",
                   o->gpu.failure[0] != '\0' ? o->gpu.failure : "the list was not recorded");
    }
}

// The same question without compute: draws into one target with and without a wait for idle
// after each. An emulator that ends its render pass at every wait (a real cost on tiled
// graphics units) shows it here, and one that ignores the wait shows nothing.
#define DRAWS 256u

static void DrawsCase(OrderContext *o, bool ordered) {
    const char *const name =
        ordered ? "256 draws into one target, each followed by a wait for idle, all land"
                : "256 draws into one target with no wait for idle between them are recorded";
    Scratch vertices, uniform;
    Begin(o);
    if (!Alloc(o, &vertices, 96, 256) ||
        !Alloc(o, &uniform, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    FullScreenTriangles(vertices.cpu);
    uniform.cpu[0] = MARKER;
    BindPositions(o, &o->value_ubo, vertices.gpu, 96);
    dkCmdBufBindUniformBuffer(o->gpu.cmdbuf, DkStage_Fragment, 0, uniform.gpu, UNIFORM_SIZE);
    for (uint32_t i = 0; i < DRAWS; ++i) {
        dkCmdBufDraw(o->gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
        if (ordered) {
            WaitForIdle(o);
        }
    }
    GpuSubmit(&o->gpu);
    if (ordered) {
        JudgeTarget(o, name, MARKER, true);
    } else {
        CHECK_TRUE(SUITE, name, CHECK_NONE, o->gpu.ready, "%s", o->gpu.failure);
    }
}

void RunOrder(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.compute = true;
    spec.code_size = 64u * 1024u;
    spec.data_size = 2u * 1024u * 1024u;
    spec.command_size = 256u * 1024u;

    OrderContext o;
    const bool opened = GpuOpenSpec(&o.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with a compute queue", CHECK_NONE, opened, "%s",
               o.gpu.failure);
    if (!opened) {
        return;
    }
    const bool loaded =
        GpuLoadShader(&o.gpu, &o.copy, ShaderData(SHADER_ORDER_COPY_COMP),
                      ShaderSize(SHADER_ORDER_COPY_COMP)) &&
        GpuLoadShader(&o.gpu, &o.mark, ShaderData(SHADER_ORDER_MARK_COMP),
                      ShaderSize(SHADER_ORDER_MARK_COMP)) &&
        GpuLoadShader(&o.gpu, &o.position, ShaderData(SHADER_POSITION_VERT),
                      ShaderSize(SHADER_POSITION_VERT)) &&
        GpuLoadShader(&o.gpu, &o.value_ubo, ShaderData(SHADER_VALUE_UBO_FRAG),
                      ShaderSize(SHADER_VALUE_UBO_FRAG)) &&
        GpuLoadShader(&o.gpu, &o.value_ssbo, ShaderData(SHADER_VALUE_SSBO_FRAG),
                      ShaderSize(SHADER_VALUE_SSBO_FRAG)) &&
        GpuLoadShader(&o.gpu, &o.store_ssbo, ShaderData(SHADER_STORE_SSBO_FRAG),
                      ShaderSize(SHADER_STORE_SSBO_FRAG));
    CHECK_TRUE(SUITE, "the compute and drawing shaders load", CHECK_NONE, loaded, "%s",
               o.gpu.failure);
    if (!loaded) {
        GpuClose(&o.gpu);
        return;
    }
    ReportNote("order: %u groups of 64 spin %u times before the last invocations copy; "
               "marker 0x%08x",
               COPY_GROUPS, COPY_SPIN, MARKER);

    CaseVertices(&o);
    CaseIndices(&o);
    CaseDrawIndirect(&o);
    CaseUniform(&o);
    CaseStorage(&o);
    CaseDispatchIndirect(&o);
    CaseComputeToCompute(&o);
    CaseFragmentToCompute(&o);
    PairsCase(&o, true);
    PairsCase(&o, false);
    DrawsCase(&o, true);
    DrawsCase(&o, false);

    GpuClose(&o.gpu);
}
