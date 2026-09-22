// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>

#include "report.h"
#include "tests/tests.h"

// The entry point for the host build. It runs only the suites that need no console, which means
// the two that check the suite's own judgement: the comparators, and the expected answers the
// drawing cases are measured against. Its value is that a change to either can be proven wrong
// in a second, without an emulator in the loop.
int main(void) {
    ReportOpen("gfxprobe-host-report.jsonl");
    ReportNote("gfxprobe host build");

    RunSelfcheck();
    RunReference();

    ReportSummary();
    ReportClose();

    printf("passed=%zu failed=%zu\n", ReportPassed(), ReportFailed());
    return ReportFailed() == 0 ? 0 : 1;
}
