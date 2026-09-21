// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "report.h"
#include "tests/tests.h"

// Where the run leaves its report on the machine it ran on. On an emulator this is a plain
// directory on the host; on the console it is the memory card.
static const char *const REPORT_PATH = "sdmc:/gfxprobe/report.jsonl";

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Nothing here draws yet, so no console or graphics setup: the report channels are the
    // whole interface. Keeping it that way means the first bring-up problem on a new machine is
    // never "did the screen work".
    mkdir("sdmc:/gfxprobe", 0777);
    ReportOpen(REPORT_PATH);

    ReportNote("gfxprobe starting");
    RunSelfcheck();
    ReportSummary();

    ReportClose();

    // A runner watching the log knows the process is finished and what it decided, without
    // parsing the lines above or waiting out a timeout.
    char done[96];
    snprintf(done, sizeof(done), "GFXPROBE DONE passed=%zu failed=%zu", ReportPassed(),
             ReportFailed());
    svcOutputDebugString(done, strlen(done));

    return ReportFailed() == 0 ? 0 : 1;
}
