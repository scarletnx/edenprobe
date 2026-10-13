// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "../check.h"
#include "../picture.h"
#include "../report.h"
#include "../vic.h"
#include "../vicref.h"
#include "tests.h"

// The video image compositor, given input the processor wrote: an NV12 surface, or two, and a
// job built field by field, and its output read back byte by byte. What each case must find is
// vicref.c's model under each answer to the questions only a console settles; a case passes
// when one combination of answers explains every byte of every job it ran, and says which.

static const char *const SUITE = "vic";
static const char *const OPENS = "the video compositor opens and maps its buffers";

#define TIMEOUT_US 1000000

static uint8_t got[VIC_JOBS_MAX][VIC_OUTPUT_MAX];
static uint8_t expected[VIC_JOBS_MAX][VIC_OUTPUT_MAX];
static uint8_t picture_got[VIC_SIZE * VIC_SIZE * 4u];
static uint8_t config[VIC_CONFIG_SIZE];
// Per question, the answers passing cases settled on: bit 0 for A, bit 1 for B.
static uint8_t settled[VIC_QUESTIONS];

/// Runs every job of @p c and keeps what each left; false with the reason when one did not
/// run, or wrote past its output.
static bool RunJobs(const VicCase *c, char *why, size_t why_size) {
    for (uint32_t j = 0; j < c->job_count; ++j) {
        const VicJob *const job = &c->jobs[j];
        for (uint32_t s = 0; s < job->slot_count; ++s) {
            VicImageWrite(&job->slots[s].image, VicDeviceLuma(s), VicDeviceChroma(s));
        }
        VicPack(job, config);
        memcpy(VicDeviceConfig(), config, sizeof(config));
        memset(VicDeviceOutput(), VIC_FILL, VIC_OUTPUT_MAX);
        memset(VicDeviceGuard(), VIC_FILL, VIC_GUARD_SIZE);
        char said[160];
        if (!VicDeviceRun(job->slot_count, VicOutputIsNv12(job), TIMEOUT_US, said,
                          sizeof(said))) {
            snprintf(why, why_size, "job %u of %u: %s", (unsigned)(j + 1u),
                     (unsigned)c->job_count, said);
            return false;
        }
        memcpy(got[j], VicDeviceOutput(), VIC_OUTPUT_MAX);
        const uint8_t *const guard = VicDeviceGuard();
        for (uint32_t i = 0; i < VIC_GUARD_SIZE; ++i) {
            if (guard[i] != VIC_FILL) {
                snprintf(why, why_size,
                         "job %u of %u wrote past its output: byte %u after it is 0x%02x",
                         (unsigned)(j + 1u), (unsigned)c->job_count, (unsigned)i, guard[i]);
                return false;
            }
        }
    }
    return true;
}

/// The picture of an RGBA job 16 wide: what came back beside the model under the answers the
/// verdict shows, pixel by pixel. An NV12 output or a wider surface gets none, rather than part
/// of one.
static void Picture16(const VicCase *c, uint32_t j) {
    const VicJob *const job = &c->jobs[j];
    if (VicOutputIsNv12(job) || job->out_width != VIC_SIZE) {
        return;
    }
    for (uint32_t y = 0; y < VIC_SIZE; ++y) {
        memcpy(picture_got + y * VIC_SIZE * 4u, got[j] + y * VIC_RGBA_PITCH, VIC_SIZE * 4u);
    }
    Picture *const picture =
        PictureBegin(VIC_SIZE, VIC_SIZE,
                     job->out_format == VIC_FORMAT_A8B8G8R8 ? PICTURE_COLOUR : PICTURE_BITS,
                     picture_got);
    for (uint32_t y = 0; y < VIC_SIZE; ++y) {
        for (uint32_t x = 0; x < VIC_SIZE; ++x) {
            const uint8_t *const want = expected[j] + y * VIC_RGBA_PITCH + x * 4u;
            const bool right = memcmp(want, picture_got + (y * VIC_SIZE + x) * 4u, 4u) == 0;
            PictureSet(picture, x, y, want, right ? PICTURE_MATCHED : PICTURE_WRONG);
        }
    }
}

static void RunCase(const VicCase *c, const char *name) {
    char detail[400];
    if (!RunJobs(c, detail, sizeof(detail))) {
        ReportCase(SUITE, name, CHECK_NONE, false, detail);
        return;
    }
    VicVerdict verdict;
    VicJudge(c, (const uint8_t(*)[VIC_OUTPUT_MAX])got, expected, &verdict);
    VicDetail(c, &verdict, (const uint8_t(*)[VIC_OUTPUT_MAX])got,
              (const uint8_t(*)[VIC_OUTPUT_MAX])expected, detail, sizeof(detail));
    if (verdict.passed) {
        VicSettle(c, verdict.fits, settled);
    }
    // Written only for a failure, or for every case with the word images, as the drawing
    // suites' are.
    Picture16(c, verdict.passed ? c->job_count - 1u : verdict.job);
    ReportCase(SUITE, name, CHECK_EXACT, verdict.passed, detail);
}

/// One note for the other side of the handback: what the passing cases said about each open
/// question, and any question two cases answered apart.
static void NoteSettled(void) {
    char text[REPORT_NOTE_MAX - 32];
    if (VicSettled(settled, text, sizeof(text)) > 0u) {
        ReportNote("vic: the passing cases answer %s", text);
    }
}

void RunVic(void) {
    memset(settled, 0, sizeof(settled));
    bool open = false;
    char why[160] = "not asked";
    if (ReportStart(SUITE, OPENS)) {
        open = VicDeviceOpen(why, sizeof(why));
        ReportCase(SUITE, OPENS, CHECK_NONE, open, why);
    }
    for (size_t i = 0; i < VicCaseCount(); ++i) {
        const VicCase *const c = VicCaseAt(i);
        char name[160];
        snprintf(name, sizeof(name), "%s: %s", c->name, c->title);
        if (!open) {
            ReportSkip(SUITE, name, REPORT_REASON_DEPENDS, "the video compositor did not open");
            continue;
        }
        if (ReportStart(SUITE, name)) {
            RunCase(c, name);
        }
    }
    if (open) {
        VicDeviceClose();
    }
    NoteSettled();
}
