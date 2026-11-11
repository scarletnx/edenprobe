// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <string.h>

#include "../check.h"
#include "../report.h"
#include "../svcref.h"
#include "tests.h"

// The svc suite's result codes, against the values the console's kernel returns, written out by
// hand from switchbrew's Error codes page (module 1, the description from bit 9): what a case's
// line prints is the code the machine returned, under the name the console gives it.

static const char *const SUITE = "reference";

void RunReferenceSvc(void) {
    const bool codes = SvcCode(SVC_TIMED_OUT) == 0xEA01u && SvcCode(SVC_CANCELLED) == 0xEC01u &&
                       SvcCode(SVC_OUT_OF_RANGE) == 0xEE01u &&
                       SvcCode(SVC_INVALID_HANDLE) == 0xE401u &&
                       SvcCode(SVC_INVALID_STATE) == 0xFA01u &&
                       SvcCode(SVC_INVALID_SIZE) == 0xCA01u && SvcCode(SVC_SUCCESS) == 0u;
    CHECK_TRUE(SUITE, "the svc suite's kernel results are the codes the console returns",
               CHECK_NONE, codes,
               "TimedOut 0x%x, Cancelled 0x%x, OutOfRange 0x%x, InvalidHandle 0x%x, InvalidState "
               "0x%x, InvalidSize 0x%x",
               (unsigned)SvcCode(SVC_TIMED_OUT), (unsigned)SvcCode(SVC_CANCELLED),
               (unsigned)SvcCode(SVC_OUT_OF_RANGE), (unsigned)SvcCode(SVC_INVALID_HANDLE),
               (unsigned)SvcCode(SVC_INVALID_STATE), (unsigned)SvcCode(SVC_INVALID_SIZE));

    char known[48], success[48], unknown[48];
    SvcDescribe(0xEA01u, known, sizeof(known));
    SvcDescribe(0u, success, sizeof(success));
    SvcDescribe(0x1234u, unknown, sizeof(unknown));
    const bool named = strcmp(known, "TimedOut (0xea01)") == 0 && strcmp(success, "Success") == 0 &&
                       strcmp(unknown, "0x1234") == 0 && SvcName(0x1234u) == NULL;
    CHECK_TRUE(SUITE, "an svc case's line names a kernel result and gives a code it does not know",
               CHECK_NONE, named, "\"%s\", \"%s\", \"%s\"", known, success, unknown);
}
