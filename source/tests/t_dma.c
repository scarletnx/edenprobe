// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The copy engine copying a buffer, as dkCmdBufCopyBuffer asks it to, 32 times in one list:
// first from words the processor wrote, then from words a dispatch wrote just before. Every copy
// must land whole. The two times are the point: an emulator that stops the processor until the
// graphics unit is idle on every copy of data the graphics unit wrote (Eden's buffer cache,
// `DMACopy`) pays for it in the second case only, once per copy.

static const char *const SUITE = "dma";

#define WORDS 16384u
#define BYTES (WORDS * 4u)
#define COPIES 32u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

typedef struct {
    uint32_t count;
    uint32_t spin;
    uint32_t zero;
    uint32_t pad;
} CopyParams;

typedef struct {
    uint32_t *cpu;
    DkGpuAddr gpu;
} Scratch;

static bool Alloc(GpuContext *gpu, Scratch *out, uint32_t size) {
    out->cpu = (uint32_t *)GpuAllocData(gpu, size, 256u, &out->gpu);
    return out->cpu != NULL;
}

/// Word i of the pattern: every word differs, and none is nought.
static uint32_t Word(uint32_t i, uint32_t salt) {
    return ((i + 1u) * 2654435761u) ^ salt;
}

static void Case(GpuContext *gpu, const DkShader *copy, bool from_dispatch, uint32_t count,
                 const char *name) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const uint32_t salt = from_dispatch ? 0x5A17C0DEu : 0x0C0FFEE5u;
    Scratch source, written, params, copies;
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginList(gpu);
    if (!Alloc(gpu, &source, BYTES) || !Alloc(gpu, &written, BYTES) ||
        !Alloc(gpu, &params, UNIFORM_SIZE) || !Alloc(gpu, &copies, count * BYTES)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    for (uint32_t i = 0; i < WORDS; ++i) {
        source.cpu[i] = Word(i, salt);
    }
    memset(written.cpu, 0, BYTES);
    memset(copies.cpu, 0, count * BYTES);
    const CopyParams values = { WORDS, 0u, 0u, 0u };
    memcpy(params.cpu, &values, sizeof(values));

    // Both cases run the dispatch, so both hand the emulator the same buffers; only the copies'
    // source differs.
    const DkShader *const shaders[] = { copy };
    dkCmdBufBindShaders(gpu->cmdbuf, DkStageFlag_Compute, shaders, 1);
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Compute, 0, params.gpu, UNIFORM_SIZE);
    dkCmdBufBindStorageBuffer(gpu->cmdbuf, DkStage_Compute, 0, source.gpu, BYTES);
    dkCmdBufBindStorageBuffer(gpu->cmdbuf, DkStage_Compute, 1, written.gpu, BYTES);
    dkCmdBufDispatchCompute(gpu->cmdbuf, WORDS / 64u, 1, 1);
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);

    const DkGpuAddr from = from_dispatch ? written.gpu : source.gpu;
    for (uint32_t k = 0; k < count; ++k) {
        dkCmdBufCopyBuffer(gpu->cmdbuf, from, copies.gpu + (DkGpuAddr)k * BYTES, BYTES);
    }
    GpuSubmit(gpu);
    if (!gpu->ready) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the list was not recorded");
        return;
    }
    uint32_t wrong = 0;
    uint32_t zero = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < count * WORDS; ++i) {
        if (copies.cpu[i] == Word(i % WORDS, salt)) {
            continue;
        }
        if (wrong == 0) {
            first = i;
        }
        ++wrong;
        if (copies.cpu[i] == 0u) {
            ++zero;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u words wrong (%u still nought), first in copy %u at word %u is "
               "0x%08x, expected 0x%08x",
               (unsigned)wrong, (unsigned)(count * WORDS), (unsigned)zero,
               (unsigned)(first / WORDS), (unsigned)(first % WORDS), (unsigned)copies.cpu[first],
               (unsigned)Word(first % WORDS, salt));
}

void RunDma(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, 64u, 64u);
    spec.compute = true;
    spec.code_size = 16u * 1024u;
    // Each timed case takes its buffers anew: 1 + 32 + 32 copies of 64 KiB and their sources.
    spec.data_size = 8u * 1024u * 1024u;

    static const char *const OPENS =
        "graphics device opens with a compute queue and room for 32 copies";
    if (!ReportStart(SUITE, OPENS)) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, OPENS, CHECK_NONE, opened, "%s", gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the copy shader loads")) {
        GpuClose(&gpu);
        return;
    }
    DkShader copy;
    const bool loaded = GpuLoadShader(&gpu, &copy, ShaderData(SHADER_ORDER_COPY_COMP),
                                      ShaderSize(SHADER_ORDER_COPY_COMP));
    CHECK_TRUE(SUITE, "the copy shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    // Whichever case comes first in a pass pays for the first use of the buffers and the copy
    // engine, as the fill suite found; a single judged copy takes it so the two timed ones compare.
    Case(&gpu, &copy, false, 1u, "one copy lands first, paying for the first use");
    Case(&gpu, &copy, false, COPIES, "32 copies of words the processor wrote land whole");
    Case(&gpu, &copy, true, COPIES, "32 copies of words a dispatch wrote just before land whole");
    GpuClose(&gpu);
}
