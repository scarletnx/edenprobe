// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "report.h"
#include "tests/tests.h"

// The entry point for the host build. It runs only the suites that need no console, which are
// the ones that check this program's own judgement and what it writes around a verdict, so a
// change to any of them can be proven wrong in a second with no emulator in the loop.
int main(void) {
    ReportOpen("edenprobe-host-report.jsonl");
    ReportNote("edenprobe host build");

    RunSelfcheck();
    RunSupport();
    RunReference();

    ReportSummary();
    ReportClose();

    printf("passed=%zu failed=%zu\n", ReportPassed(), ReportFailed());
    return ReportFailed() == 0 ? 0 : 1;
}
