// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The samples counter, which the library leaves enabled (deko3d sets SampleCounterEnable at
// start-up), reported after each of 320 quads and after each of 320 cleared cells, in one list
// each: more than 256 reports, so an emulator that keeps its occlusion queries in banks of 256
// has to take a new bank in the middle of the list, at a draw or at a clear. Each quad covers
// one 8 by 8 cell of a 160 by 128 target and must pass exactly 64 samples, one per pixel, since
// the counter is reset before it; every quad and every cleared cell must also be there.
//
// An emulator that resets a new bank's queries by ending the render pass the draw or the clear
// has just begun records that draw or clear outside any render pass. In Eden that is
// SamplesQueryBank::Reset (renderer_vulkan/vk_query_cache.cpp), and only on a Vulkan driver
// without host query reset; Eden's validation messages then name vkCmdDraw or
// vkCmdClearAttachments outside a render pass.

static const char *const SUITE = "occlusion";

#define CELL 8u
#define COLUMNS 20u
#define ROWS 16u
#define TARGET_WIDTH (COLUMNS * CELL)
#define TARGET_HEIGHT (ROWS * CELL)
#define CELLS (COLUMNS * ROWS)
#define REPORT_SIZE 16u

/// Cell @p k's colour: every byte but alpha different between neighbours, none of it nought.
static void Colour(uint32_t k, uint8_t out[4]) {
    out[0] = (uint8_t)(k * 7u + 1u);
    out[1] = (uint8_t)(0xFEu - k * 3u);
    out[2] = (uint8_t)((k >> 2) * 29u + 5u);
    out[3] = 0xFFu;
}

/// Counts the cells whose 64 pixels are not all their colour; the first one and what it holds.
static void JudgeCells(const uint8_t *pixels, const char *name) {
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong_cells = 0;
    int32_t first = -1;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    for (uint32_t k = 0; k < CELLS; ++k) {
        uint8_t want[4];
        Colour(k, want);
        bool cell_right = true;
        for (uint32_t y = (k / COLUMNS) * CELL; y < (k / COLUMNS + 1u) * CELL; ++y) {
            for (uint32_t x = (k % COLUMNS) * CELL; x < (k % COLUMNS + 1u) * CELL; ++x) {
                const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
                const bool right = memcmp(pixel, want, 4) == 0;
                PictureSet(picture, x, y, want, right ? PICTURE_MATCHED : PICTURE_WRONG);
                if (!right && cell_right) {
                    cell_right = false;
                    if (first < 0) {
                        first = (int32_t)k;
                        memcpy(seen, pixel, 4);
                    }
                }
            }
        }
        if (!cell_right) {
            ++wrong_cells;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong_cells == 0,
               "%u of %u cells wrong, the first cell %d holding %02x%02x%02x%02x",
               (unsigned)wrong_cells, (unsigned)CELLS, (int)first, seen[0], seen[1], seen[2],
               seen[3]);
}

void RunOcclusion(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 16u * 1024u;
    spec.data_size = 256u * 1024u;
    spec.command_size = 256u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for 320 reports", CHECK_NONE, opened,
               "%s", gpu.failure);
    if (!opened) {
        return;
    }
    DkShader vs;
    DkShader fs;
    const bool loaded = GpuLoadShader(&gpu, &vs, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fs, ShaderData(SHADER_FLAT_FRAG),
                                      ShaderSize(SHADER_FLAT_FRAG));
    CHECK_TRUE(SUITE, "the drawing shaders load", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    DkGpuAddr reports_address = 0;
    uint8_t *const reports =
        (uint8_t *)GpuAllocData(&gpu, CELLS * REPORT_SIZE, REPORT_SIZE, &reports_address);
    static GpuVertex quads[CELLS * 6u];
    CHECK_TRUE(SUITE, "the reports have somewhere to live", CHECK_NONE, reports != NULL,
               "the scratch block had no room");
    if (reports == NULL) {
        GpuClose(&gpu);
        return;
    }
    for (uint32_t k = 0; k < CELLS; ++k) {
        uint8_t colour[4];
        Colour(k, colour);
        const float left = -1.0f + 2.0f * (float)((k % COLUMNS) * CELL) / (float)TARGET_WIDTH;
        const float right = left + 2.0f * (float)CELL / (float)TARGET_WIDTH;
        const float top = 1.0f - 2.0f * (float)((k / COLUMNS) * CELL) / (float)TARGET_HEIGHT;
        const float bottom = top - 2.0f * (float)CELL / (float)TARGET_HEIGHT;
        const float corners[6][2] = { { left, bottom }, { right, bottom }, { right, top },
                                      { left, bottom }, { right, top },    { left, top } };
        for (uint32_t i = 0; i < 6u; ++i) {
            quads[k * 6u + i] = (GpuVertex){ corners[i][0],       corners[i][1],
                                             0.0f,                0.0f,
                                             0.0f,                colour[0] / 255.0f,
                                             colour[1] / 255.0f,  colour[2] / 255.0f,
                                             colour[3] / 255.0f };
        }
    }

    // 320 quads, the counter reset before each and reported after it.
    static const char *const DRAWN = "320 quads counted one by one are all drawn";
    static const char *const COUNTED = "320 quads counted one by one pass 64 samples each";
    memset(reports, 0xFF, CELLS * REPORT_SIZE);
    GpuClear(&gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&gpu);
    bool ok = GpuBindTriangleState(&gpu, &vs, &fs, quads, CELLS * 6u);
    for (uint32_t k = 0; k < CELLS && ok; ++k) {
        dkCmdBufResetCounter(gpu.cmdbuf, DkCounter_SamplesPassed);
        dkCmdBufDraw(gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, k * 6u, 0);
        dkCmdBufReportCounter(gpu.cmdbuf, DkCounter_SamplesPassed,
                              reports_address + (DkGpuAddr)k * REPORT_SIZE);
    }
    GpuSubmit(&gpu);
    const uint8_t *pixels = ok && gpu.ready ? GpuReadBack(&gpu) : NULL;
    if (pixels == NULL) {
        const char *const why =
            gpu.failure[0] != '\0' ? gpu.failure : "the list or the readback did not happen";
        ReportCase(SUITE, DRAWN, CHECK_NONE, false, why);
        ReportCase(SUITE, COUNTED, CHECK_NONE, false, why);
    } else {
        JudgeCells(pixels, DRAWN);
        uint32_t wrong = 0;
        int32_t first = -1;
        uint64_t seen = 0;
        for (uint32_t k = 0; k < CELLS; ++k) {
            uint64_t value;
            memcpy(&value, reports + k * REPORT_SIZE, sizeof(value));
            if (value != CELL * CELL) {
                if (first < 0) {
                    first = (int32_t)k;
                    seen = value;
                }
                ++wrong;
            }
        }
        CHECK_TRUE(SUITE, COUNTED, CHECK_EXACT, wrong == 0,
                   "%u of %u reports are not 64, the first report %d holding %llu",
                   (unsigned)wrong, (unsigned)CELLS, (int)first, (unsigned long long)seen);
    }

    // 320 cells cleared one by one, the counter reported after each clear.
    static const char *const CLEARED = "320 cells cleared one by one between reports are cleared";
    GpuClear(&gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(&gpu);
    for (uint32_t k = 0; k < CELLS; ++k) {
        uint8_t colour[4];
        Colour(k, colour);
        const DkScissor scissor = { (k % COLUMNS) * CELL, (k / COLUMNS) * CELL, CELL, CELL };
        dkCmdBufSetScissors(gpu.cmdbuf, 0, &scissor, 1);
        dkCmdBufClearColorFloat(gpu.cmdbuf, 0, DkColorMask_RGBA, colour[0] / 255.0f,
                                colour[1] / 255.0f, colour[2] / 255.0f, colour[3] / 255.0f);
        dkCmdBufReportCounter(gpu.cmdbuf, DkCounter_SamplesPassed,
                              reports_address + (DkGpuAddr)k * REPORT_SIZE);
    }
    GpuSubmit(&gpu);
    pixels = gpu.ready ? GpuReadBack(&gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, CLEARED, CHECK_NONE, false,
                   gpu.failure[0] != '\0' ? gpu.failure : "the list or the readback did not happen");
    } else {
        JudgeCells(pixels, CLEARED);
    }
    GpuClose(&gpu);
}
