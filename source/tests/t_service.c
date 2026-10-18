// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "../check.h"
#include "../report.h"
#include "../serviceref.h"
#include "tests.h"

// The service family: commands a game sends to the system's services, asked the way a game asks
// them, each answer judged by serviceref.c against the one reden gives. One suite per service,
// in the order the family runs them; a case that needs one before it is skipped as `depends`
// when that one did not leave what it needs.

static void Report(ServiceCaseId id, CheckTier tier, bool passed, const char *detail) {
    ReportCase(ServiceCaseSuite(id), ServiceCaseName(id), tier, passed, detail);
}

static void Skip(ServiceCaseId id, ReportReason reason, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void Skip(ServiceCaseId id, ReportReason reason, const char *fmt, ...) {
    char detail[480];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    ReportSkip(ServiceCaseSuite(id), ServiceCaseName(id), reason, detail);
}

static bool Start(ServiceCaseId id) {
    return ReportStart(ServiceCaseSuite(id), ServiceCaseName(id));
}

// ---------------------------------------------------------------------------------------------
// hwopus

/// Opens decoders of 48 kHz stereo, the way a game's codec library does, until one is refused or
/// two past reden's limit are open; closes one and opens again; closes them all. Each decoder is
/// an object of its own, with a work buffer handed over as transfer memory.
static void RunHwopus(void) {
    static HwopusDecoder decoders[SERVICE_OPUS_TRIES];
    static uint32_t results[SERVICE_OPUS_TRIES];
    memset(decoders, 0, sizeof(decoders));
    memset(results, 0, sizeof(results));
    char detail[480];

    bool opens = false;
    if (Start(SERVICE_OPUS_OPENS)) {
        results[0] = hwopusDecoderInitialize(&decoders[0], 48000, 2);
        opens = R_SUCCEEDED(results[0]);
        snprintf(detail, sizeof(detail), "hwopusDecoderInitialize(48000, 2) returned 0x%x",
                 (unsigned)results[0]);
        Report(SERVICE_OPUS_OPENS, CHECK_NONE, opens, detail);
    }

    bool refused = false;
    if (!opens) {
        Skip(SERVICE_OPUS_LIMITED, REPORT_REASON_DEPENDS, "no decoder opened to count from");
    } else if (Start(SERVICE_OPUS_LIMITED)) {
        uint32_t tried = 1;
        while (tried < SERVICE_OPUS_TRIES) {
            results[tried] = hwopusDecoderInitialize(&decoders[tried], 48000, 2);
            ++tried;
            if (R_FAILED(results[tried - 1u])) {
                refused = true;
                break;
            }
        }
        const bool passed = ServiceJudgeOpusLimit(results, tried, detail, sizeof(detail));
        Report(SERVICE_OPUS_LIMITED, CHECK_EXACT, passed, detail);
    }

    if (!refused) {
        Skip(SERVICE_OPUS_FREED, REPORT_REASON_DEPENDS,
             "the case above refused no decoder, so there is no room to free");
    } else if (Start(SERVICE_OPUS_FREED)) {
        // The first one closed; the one the limit case was refused is asked for again.
        hwopusDecoderExit(&decoders[0]);
        const uint32_t reopened = hwopusDecoderInitialize(&decoders[0], 48000, 2);
        const bool passed = ServiceJudgeOpusFreed(reopened, detail, sizeof(detail));
        Report(SERVICE_OPUS_FREED, CHECK_EXACT, passed, detail);
    }

    for (uint32_t i = 0; i < SERVICE_OPUS_TRIES; ++i) {
        hwopusDecoderExit(&decoders[i]);
    }
}

void RunService(void) {
    RunHwopus();
}
