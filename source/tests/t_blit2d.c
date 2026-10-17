// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../blit2dref.h"
#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "tests.h"

// The 2D engine's scaled copy from a block linear source into a window of a block linear
// destination, written as raw methods of class 0x902D, which the library never sends with a
// chosen origin. After the queue drains, each case reads its destination twice: the processor
// reads the raw bytes, which is what a game reading that memory sees, then the copy engine
// copies it into rows, which is what the drawing suites read. An emulator that keeps the copy in
// a cache of its own passes the second read and not the first. The source is written once; each
// case has a destination of its own, so no case reads another's copy. blit2dref.c holds the
// setup, the expected images and the judge.

static const char *const SUITE = "blit2d";
static const char *const OPENS = "graphics device opens with both surfaces laid out block linear";

typedef struct {
    GpuContext gpu;
    DkImage source;
    DkImage destination[BLIT2D_CASES];
    uint8_t *destination_bytes[BLIT2D_CASES];
} Blit2dContext;

static Blit2dContext context;
static uint8_t raw[BLIT2D_DST_BYTES];
static uint8_t raw_rows[BLIT2D_DST_BYTES];
static uint32_t fits[BLIT2D_CASES];
static uint32_t buffer_fits[BLIT2D_CASES];

/// Lays out one surface in the processor-visible block, two gobs tall, and checks the library
/// gave it the size the block linear rule does, since the raw methods describe it by that rule.
static bool Surface(GpuContext *gpu, DkImage *image, uint32_t width, uint32_t height,
                    uint32_t bytes, uint8_t **memory, char *why, size_t why_size) {
    DkImageLayout layout;
    if (!GpuCreateImage(gpu, image, &layout, DkImageFormat_RGBA8_Unorm, width, height,
                        DkImageFlags_CustomTileSize | DkImageFlags_Usage2DEngine,
                        DkTileSize_TwoGobs, memory)) {
        snprintf(why, why_size, "%s", gpu->failure);
        return false;
    }
    if (dkImageLayoutGetSize(&layout) != bytes) {
        snprintf(why, why_size,
                 "the library laid a %ux%u surface out in %llu bytes, blocks two gobs tall "
                 "make %u",
                 (unsigned)width, (unsigned)height,
                 (unsigned long long)dkImageLayoutGetSize(&layout), (unsigned)bytes);
        return false;
    }
    return true;
}

static bool Open(Blit2dContext *b, char *why, size_t why_size) {
    uint8_t *source = NULL;
    if (!Surface(&b->gpu, &b->source, BLIT2D_SRC_WIDTH, BLIT2D_SRC_HEIGHT, BLIT2D_SRC_BYTES,
                 &source, why, why_size)) {
        return false;
    }
    for (uint32_t i = 0; i < BLIT2D_CASES; ++i) {
        if (!Surface(&b->gpu, &b->destination[i], BLIT2D_DST_WIDTH, BLIT2D_DST_HEIGHT,
                     BLIT2D_DST_BYTES, &b->destination_bytes[i], why, why_size)) {
            return false;
        }
    }
    Blit2dFillSource(source);
    snprintf(why, why_size, "a %ux%u source and %u destinations of %ux%u, blocks two gobs tall",
             (unsigned)BLIT2D_SRC_WIDTH, (unsigned)BLIT2D_SRC_HEIGHT, (unsigned)BLIT2D_CASES,
             (unsigned)BLIT2D_DST_WIDTH, (unsigned)BLIT2D_DST_HEIGHT);
    return true;
}

/// The picture of the read that went wrong: the copy to buffer, or the raw bytes put in rows
/// when the copy to buffer was right and they were not.
static void Draw(const Blit2dCase *c, const Blit2dVerdict *v, const uint8_t *buffer) {
    const uint8_t *shown = buffer;
    if ((v->buffer.fits >> v->shown & 1u) != 0u && (v->raw.fits >> v->shown & 1u) == 0u) {
        for (uint32_t y = 0; y < BLIT2D_DST_HEIGHT; ++y) {
            for (uint32_t x = 0; x < BLIT2D_DST_WIDTH; ++x) {
                const uint32_t word = Blit2dRawPixel(raw, x, y);
                uint8_t *const at = raw_rows + ((size_t)y * BLIT2D_DST_WIDTH + x) * 4u;
                for (uint32_t i = 0; i < 4u; ++i) {
                    at[i] = (uint8_t)(word >> (8u * i));
                }
            }
        }
        shown = raw_rows;
    }
    Picture *const picture = PictureBegin(BLIT2D_DST_WIDTH, BLIT2D_DST_HEIGHT, PICTURE_BITS, shown);
    for (uint32_t y = 0; y < BLIT2D_DST_HEIGHT; ++y) {
        for (uint32_t x = 0; x < BLIT2D_DST_WIDTH; ++x) {
            const uint32_t want = Blit2dWant(c, v->shown, x, y);
            const uint8_t expect[4] = { (uint8_t)want, (uint8_t)(want >> 8), (uint8_t)(want >> 16),
                                        (uint8_t)(want >> 24) };
            PictureSet(picture, x, y, expect,
                       Blit2dBufferPixel(shown, x, y) == want ? PICTURE_MATCHED : PICTURE_WRONG);
        }
    }
}

static void RunCase(Blit2dContext *b, Blit2dCaseId id, const char *name) {
    const Blit2dCase *const c = Blit2dCaseAt(id);
    GpuContext *const gpu = &b->gpu;
    memset(b->destination_bytes[id], BLIT2D_FILL, BLIT2D_DST_BYTES);
    uint32_t words[BLIT2D_WORDS];
    const size_t count = Blit2dRawWords(c, dkImageGetGpuAddr(&b->source),
                                        dkImageGetGpuAddr(&b->destination[id]), words);
    GpuBeginPass(gpu);
    const bool recorded = GpuRawMethods(gpu, words, (uint32_t)count);
    // The copy is done before the fence the drain waits on, whose cache flush puts it in memory.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);
    GpuSubmit(gpu);
    if (!recorded || !gpu->ready) {
        ReportCase(SUITE, name, CHECK_EXACT, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the copy was not recorded");
        return;
    }
    // The raw bytes first: the copy to buffer is itself a read an emulator may answer from the
    // cache it keeps the copy in, and might write that back to memory as it does.
    memcpy(raw, b->destination_bytes[id], BLIT2D_DST_BYTES);
    const uint8_t *const buffer =
        GpuReadBackImage(gpu, &b->destination[id], BLIT2D_DST_WIDTH, BLIT2D_DST_HEIGHT);
    if (buffer == NULL) {
        ReportCase(SUITE, name, CHECK_EXACT, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the copy to buffer did not happen");
        return;
    }
    Blit2dVerdict v;
    Blit2dJudge(c, raw, buffer, &v);
    fits[id] = v.fits;
    buffer_fits[id] = v.buffer.fits;
    static char detail[1024];
    Blit2dDetail(c, &v, raw, buffer, detail, sizeof(detail));
    Draw(c, &v, buffer);
    ReportCase(SUITE, name, CHECK_EXACT, v.passed, detail);
}

void RunBlit2d(void) {
    memset(fits, 0, sizeof(fits));
    memset(buffer_fits, 0, sizeof(buffer_fits));
    if (!ReportStart(SUITE, OPENS)) {
        return;
    }
    GpuSpec spec;
    // The main target is only there to size the readback: one destination's worth.
    GpuSpecDefaults(&spec, BLIT2D_DST_WIDTH, BLIT2D_DST_HEIGHT);
    spec.data_size = 16u * 1024u;
    spec.cpu_image_size = 128u * 1024u;
    Blit2dContext *const b = &context;
    memset(b, 0, sizeof(*b));
    char why[320];
    const bool opened = GpuOpenSpec(&b->gpu, &spec);
    bool open = opened;
    if (opened) {
        open = Open(b, why, sizeof(why));
    } else {
        snprintf(why, sizeof(why), "%s", b->gpu.failure);
    }
    ReportCase(SUITE, OPENS, CHECK_NONE, open, why);
    for (uint32_t i = 0; i < BLIT2D_CASES; ++i) {
        const Blit2dCase *const c = Blit2dCaseAt((Blit2dCaseId)i);
        char name[128];
        snprintf(name, sizeof(name), "%s: %s", c->name, c->title);
        if (!open) {
            ReportSkip(SUITE, name, REPORT_REASON_DEPENDS, "the device or its surfaces did not "
                                                           "come up");
            continue;
        }
        if (ReportStart(SUITE, name)) {
            RunCase(b, (Blit2dCaseId)i, name);
        }
    }
    if (opened) {
        GpuClose(&b->gpu);
    }
    char note[REPORT_NOTE_MAX];
    if (Blit2dNote(fits, buffer_fits, note, sizeof(note))) {
        ReportNote("%s", note);
    }
}
