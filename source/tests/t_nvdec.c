// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "../check.h"
#include "../nvdec.h"
#include "../report.h"
#include "../vp9ref.h"
#include "tests.h"

// The video decode engine, given VP9 jobs built ready from libvpx's test vectors: one job per
// frame, each image read back off its surface and hashed against the list libvpx publishes.
// A case passes when every image the vector shows is right; the line says when the images were
// there (Q1), whether a hidden frame's job wrote its surface (Q2), and for the two cases built
// both ways, which way decodes (Q3, Q4).

static const char *const SUITE = "nvdec";
static const char *const OPENS = "the video decoder opens and maps its buffers";

#define TIMEOUT_US 1000000

static Vp9Pass passes[2];
static uint8_t table[VP9_PROBS_SIZE];
// A surface copied out of the uncached buffer before it is read byte by byte.
static uint8_t copy[VP9_SURFACE_MAX];
// Per question, the answers passing cases settled on: bit 0 for A, bit 1 for B.
static uint8_t settled[VP9_QUESTIONS];

static void ReadSurface(const Vp9Case *c, const Vp9Layout *l, uint32_t surface, Vp9Image *out) {
    memcpy(copy, NvdecDeviceMemory() + VP9_AT_SURFACES + surface * l->size, l->size);
    Vp9ReadImage(copy, l, c->width, c->height, out);
}

/// Everything a pass starts from: surfaces and the guard full of the fill, the engine's own
/// buffers nought, and the two fixed words of the tile size buffer.
static void Prepare(void) {
    uint8_t *const mem = NvdecDeviceMemory();
    memset(mem, 0, VP9_AT_SURFACES);
    memset(mem + VP9_AT_SURFACES, VP9_FILL, VP9_MAP_SIZE - VP9_AT_SURFACES);
    const uint16_t fixed[2] = {9u, 1u};
    memcpy(mem + VP9_AT_TILES + 0x37Au * 2u, fixed, sizeof(fixed));
}

/// One pass of @p c's frames: a job for each decoded one, an image read for each, and every
/// image read again after the job that follows it.
static void RunPass(const Vp9Case *c, Vp9Question question, bool b, Vp9Pass *pass) {
    memset(pass, 0, sizeof(*pass));
    pass->question = question;
    pass->b = b;
    Prepare();
    uint8_t *const mem = NvdecDeviceMemory();
    const Vp9Layout l = Vp9LayoutOf(c->width, c->height);
    uint32_t jobs = 0;
    for (uint32_t i = 0; i < c->frame_count; ++i) {
        jobs += !c->frames[i].existing;
    }
    uint32_t pending[VP9_FRAMES_MAX];
    uint32_t pending_count = 0, index = 0, segment_read = 0;
    bool after_existing = false;
    for (uint32_t i = 0; i < c->frame_count; ++i) {
        const Vp9Frame *const f = &c->frames[i];
        if (f->existing) {
            ReadSurface(c, &l, f->surface, &pass->now[i]);
            pending[pending_count++] = i;
            after_existing = true;
            pass->frames_run = i + 1u;
            continue;
        }
        uint8_t setup[VP9_SETUP_SIZE];
        Vp9Setup(f, after_existing, question, b, setup);
        memcpy(mem + VP9_AT_SETUP, setup, sizeof(setup));
        Vp9TableApply(f, table);
        memcpy(mem + VP9_AT_PROBS, table, sizeof(table));
        memcpy(mem + VP9_AT_TILES, f->tile_sizes, f->tiles * 2u * sizeof(uint16_t));
        const uint32_t body = f->size - f->body;
        memcpy(mem + VP9_AT_BITSTREAM, f->data + f->body, body);
        memset(mem + VP9_AT_BITSTREAM + body, 0, 16u);
        memset(mem + VP9_AT_STATUS, 0, 0x100u);
        const Vp9JobAt at = {
            .iova = NvdecDeviceIova(),
            .surface_size = l.size,
            .chroma = l.chroma,
            .index = index,
            .segment_read = segment_read,
            .surfaces = {f->refs[0], f->refs[1], f->refs[2], f->surface},
        };
        const size_t words = Vp9Commands((uint32_t *)(mem + VP9_AT_COMMANDS),
                                         VP9_COMMAND_WORDS, &at, NvdecDeviceSyncpt());
        char said[160] = "the command words do not fit";
        if (words == 0u || !NvdecDeviceRun((uint32_t)words, TIMEOUT_US, said, sizeof(said))) {
            snprintf(pass->failure, sizeof(pass->failure), "job %u of %u (frame %u): %s",
                     (unsigned)(index + 1u), (unsigned)jobs, (unsigned)i, said);
            pass->frames_run = i;
            return;
        }
        // nvdec_status_s: macroblocks in error at 4, the error status at 12.
        uint32_t status[4];
        memcpy(status, mem + VP9_AT_STATUS, sizeof(status));
        pass->mbs_in_error[i] = status[1];
        pass->error_status[i] = status[3];
        ReadSurface(c, &l, f->surface, &pass->now[i]);
        for (uint32_t p = 0; p < pending_count; ++p) {
            ReadSurface(c, &l, c->frames[pending[p]].surface, &pass->later[pending[p]]);
        }
        pending[0] = i;
        pending_count = 1;
        // A job that wrote a segment map leaves it for the next one to read.
        segment_read ^= setup[0x81] != 0u;
        ++index;
        after_existing = false;
        pass->frames_run = i + 1u;
    }
    const uint8_t *const guard = mem + VP9_AT_GUARD;
    for (uint32_t i = 0; i < VP9_GUARD_SIZE; ++i) {
        if (guard[i] != VP9_FILL) {
            snprintf(pass->failure, sizeof(pass->failure),
                     "a job wrote past the surfaces: byte %u after them is 0x%02x",
                     (unsigned)i, guard[i]);
            return;
        }
    }
}

static void RunCase(const Vp9Case *c, const char *name) {
    const Vp9Question question = Vp9CaseQuestion(c);
    RunPass(c, question, false, &passes[0]);
    uint32_t count = 1;
    if (question != VP9_QUESTIONS) {
        RunPass(c, question, true, &passes[1]);
        count = 2;
    }
    char detail[480];
    uint32_t fits = 0;
    const bool passed = Vp9Detail(c, passes, count, detail, sizeof(detail), &fits);
    if (passed) {
        Vp9Settle(fits, settled);
    }
    ReportCase(SUITE, name, CHECK_EXACT, passed, detail);
}

/// One note that sums up the suite for whoever compares two runs: what the passing cases said
/// about each open question, and any question two cases answered apart.
static void NoteSettled(void) {
    char text[REPORT_NOTE_MAX - 32];
    if (Vp9Settled(settled, text, sizeof(text)) > 0u) {
        ReportNote("nvdec: the passing cases answer %s", text);
    }
}

void RunNvdec(void) {
    memset(settled, 0, sizeof(settled));
    bool open = false;
    char why[200] = "not asked";
    if (ReportStart(SUITE, OPENS)) {
        open = NvdecDeviceOpen(why, sizeof(why));
        ReportCase(SUITE, OPENS, CHECK_NONE, open, why);
    }
    for (uint32_t i = 0; i < VP9_CASE_COUNT; ++i) {
        const Vp9Case *const c = &VP9_CASES[i];
        char name[160];
        snprintf(name, sizeof(name), "%s: %s", c->name, c->title);
        if (!open) {
            ReportSkip(SUITE, name, REPORT_REASON_DEPENDS, "the video decoder did not open");
            continue;
        }
        if (ReportStart(SUITE, name)) {
            RunCase(c, name);
        }
    }
    if (open) {
        NvdecDeviceClose();
    }
    NoteSettled();
}
