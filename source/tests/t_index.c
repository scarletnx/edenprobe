// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../rawref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Indexed draws whose index buffer limit is programmed to the last byte of the indices, the way a
// driver that bounds its draws exactly does. The library never writes that register after start
// up (it leaves it at the end of the address space), so each case writes it with raw methods
// before the draw and puts the library's value back after it.
//
// The register holds the last valid byte, not one past it: the 3D class names it a limit, and
// nouveau programs it as the address plus the size minus one (mesa nvc0_vbo.c). The draw's last
// index is the one that decides whether the second of the two triangles over the target exists,
// so a reader that treats the limit as one past the end loses that triangle and half the target.

static const char *const SUITE = "index";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)
#define MARKER 0x6BD2459Au
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

typedef struct {
    GpuContext gpu;
    DkShader position;
    DkShader value_ubo;
    DkGpuAddr vertices;
    DkGpuAddr uniform;
} IndexContext;

static uint32_t FloatBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// Writes index @p i of value @p value at the width @p bytes.
static void PutIndex(uint8_t *indices, uint32_t bytes, uint32_t i, uint32_t value) {
    for (uint32_t b = 0; b < bytes; ++b) {
        indices[i * bytes + b] = (uint8_t)(value >> (8u * b));
    }
}

static uint32_t IndexBytes(DkIdxFormat format) {
    return format == DkIdxFormat_Uint8 ? 1u : format == DkIdxFormat_Uint16 ? 2u : 4u;
}

static void JudgeFull(IndexContext *x, const char *name, bool drawn) {
    if (!drawn || !x->gpu.ready) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   x->gpu.failure[0] != '\0' ? x->gpu.failure : "the list was not recorded");
        return;
    }
    const uint8_t *const pixels = GpuReadBack(&x->gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   x->gpu.failure[0] != '\0' ? x->gpu.failure : "the readback handed back nothing");
        return;
    }
    const uint8_t want[4] = { (uint8_t)MARKER, (uint8_t)(MARKER >> 8), (uint8_t)(MARKER >> 16),
                              (uint8_t)(MARKER >> 24) };
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    // The second triangle is the upper left half of the target: x below y, in the readback's
    // rows, which run from the top of clip space.
    uint32_t second_half_missing = 0;
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t xx = 0; xx < TARGET_WIDTH; ++xx) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, xx, y);
            const bool right = memcmp(pixel, want, 4) == 0;
            PictureSet(picture, xx, y, want, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                ++wrong;
                if (xx + 1u < TARGET_HEIGHT - y) {
                    ++second_half_missing;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels not drawn, %u of them in the triangle the last index closes",
               (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)second_half_missing);
}

/// One indexed draw of @p padding degenerate triangles and then the two over the target, the
/// indices starting @p first indices into the buffer, with the limit programmed tightly when
/// @p tight, and the library's own limit otherwise.
static void IndexedCase(IndexContext *x, const char *name, DkIdxFormat format,
                        uint32_t padding, uint32_t first, bool tight) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const uint32_t bytes = IndexBytes(format);
    const uint32_t count = padding * 3u + 6u;
    GpuClear(&x->gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginList(&x->gpu);
    DkGpuAddr index_address = 0;
    uint8_t *const indices =
        (uint8_t *)GpuAllocData(&x->gpu, (first + count) * bytes, 256, &index_address);
    if (indices == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the indices");
        return;
    }
    // What sits before the first index is never drawn: vertex 3 three times, which would cover
    // nothing even if it were.
    for (uint32_t i = 0; i < first; ++i) {
        PutIndex(indices, bytes, i, 3u);
    }
    for (uint32_t i = 0; i < padding * 3u; ++i) {
        PutIndex(indices, bytes, first + i, 0u);
    }
    static const uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
    for (uint32_t i = 0; i < 6u; ++i) {
        PutIndex(indices, bytes, first + padding * 3u + i, quad[i]);
    }

    const DkShader *const shaders[] = { &x->position, &x->value_ubo };
    dkCmdBufBindShaders(x->gpu.cmdbuf, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
    static const DkVtxAttribState attribute = { .bufferId = 0,
                                                .isFixed = 0,
                                                .offset = 0,
                                                .size = DkVtxAttribSize_4x32,
                                                .type = DkVtxAttribType_Float,
                                                .isBgra = 0 };
    static const DkVtxBufferState buffer_state = { 16, 0 };
    dkCmdBufBindVtxAttribState(x->gpu.cmdbuf, &attribute, 1);
    dkCmdBufBindVtxBufferState(x->gpu.cmdbuf, &buffer_state, 1);
    dkCmdBufBindVtxBuffer(x->gpu.cmdbuf, 0, x->vertices, 64);
    dkCmdBufBindUniformBuffer(x->gpu.cmdbuf, DkStage_Fragment, 0, x->uniform, UNIFORM_SIZE);
    dkCmdBufBindIdxBuffer(x->gpu.cmdbuf, format, index_address);

    bool ok = true;
    uint32_t words[3];
    if (tight) {
        RawIndexLimit(words, RawTightIndexLimit(index_address, first, count, bytes));
        ok = GpuRawMethods(&x->gpu, words, 3);
    }
    dkCmdBufDrawIndexed(x->gpu.cmdbuf, DkPrimitive_Triangles, count, 1, first, 0, 0);
    if (tight) {
        RawIndexLimit(words, RAW_INDEX_LIMIT_UNBOUNDED);
        ok = ok && GpuRawMethods(&x->gpu, words, 3);
    }
    GpuSubmit(&x->gpu);
    JudgeFull(x, name, ok);
}

void RunIndex(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 256u * 1024u;

    if (!ReportStart(SUITE, "graphics device opens with room for index buffers")) {
        return;
    }
    IndexContext x;
    const bool opened = GpuOpenSpec(&x.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for index buffers", CHECK_NONE, opened,
               "%s", x.gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the drawing shaders load")) {
        GpuClose(&x.gpu);
        return;
    }
    const bool loaded = GpuLoadShader(&x.gpu, &x.position, ShaderData(SHADER_POSITION_VERT),
                                      ShaderSize(SHADER_POSITION_VERT)) &&
                        GpuLoadShader(&x.gpu, &x.value_ubo, ShaderData(SHADER_VALUE_UBO_FRAG),
                                      ShaderSize(SHADER_VALUE_UBO_FRAG));
    CHECK_TRUE(SUITE, "the drawing shaders load", CHECK_NONE, loaded, "%s", x.gpu.failure);
    if (!loaded) {
        GpuClose(&x.gpu);
        return;
    }
    uint32_t *const vertices = (uint32_t *)GpuAllocData(&x.gpu, 64, 256, &x.vertices);
    uint32_t *const uniform =
        (uint32_t *)GpuAllocData(&x.gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &x.uniform);
    if (vertices == NULL || uniform == NULL) {
        if (ReportStart(SUITE, "the vertices and the colour have somewhere to live")) {
            ReportCase(SUITE, "the vertices and the colour have somewhere to live", CHECK_NONE,
                       false, "no scratch left");
        }
        GpuClose(&x.gpu);
        return;
    }
    static const float corners[4][2] = {
        { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }
    };
    for (uint32_t i = 0; i < 4u; ++i) {
        vertices[i * 4u + 0u] = FloatBits(corners[i][0]);
        vertices[i * 4u + 1u] = FloatBits(corners[i][1]);
        vertices[i * 4u + 2u] = FloatBits(0.0f);
        vertices[i * 4u + 3u] = FloatBits(1.0f);
    }
    memset(uniform, 0, UNIFORM_SIZE);
    uniform[0] = MARKER;

    IndexedCase(&x, "an indexed draw with the library's open limit uses every index",
                DkIdxFormat_Uint16, 0, 0, false);
    IndexedCase(&x, "a limit on the last byte of 16-bit indices keeps the last index",
                DkIdxFormat_Uint16, 0, 0, true);
    IndexedCase(&x, "a limit on the last byte of 8-bit indices keeps the last index",
                DkIdxFormat_Uint8, 0, 0, true);
    IndexedCase(&x, "a limit on the last byte of 32-bit indices keeps the last index",
                DkIdxFormat_Uint32, 0, 0, true);
    IndexedCase(&x, "a limit on the last byte of 3006 16-bit indices keeps the last index",
                DkIdxFormat_Uint16, 1000, 0, true);
    IndexedCase(&x, "a limit on the last byte keeps the last index when the draw starts at 4",
                DkIdxFormat_Uint16, 0, 4, true);

    GpuClose(&x.gpu);
}
