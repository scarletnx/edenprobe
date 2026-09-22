// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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

    // No console and no screen, even now that a suite draws: the report channels stay the whole
    // interface, so a bring-up problem on a new machine is never "did the screen work".
    mkdir("sdmc:/gfxprobe", 0777);
    ReportOpen(REPORT_PATH);

    ReportNote("gfxprobe starting");
    // Order matters: the selfcheck suite is what proves the comparisons the drawing suite then
    // leans on, so a machine where both are broken says which one broke first.
    RunSelfcheck();
    RunClear();
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
