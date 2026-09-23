// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "picture.h"
#include "report.h"
#include "tests/tests.h"
#include "viewer.h"

// Where the run leaves its report on the machine it ran on. On an emulator this is a plain
// directory on the host; on the console it is the memory card. The pictures of the failing
// cases go under the same directory, so one folder is the whole of what a run leaves behind.
static const char *const REPORT_DIRECTORY = "sdmc:/edenprobe";
static const char *const REPORT_PATH = "sdmc:/edenprobe/report.jsonl";
static const char *const MODE_PATH = "sdmc:/edenprobe/mode";

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // No console and no screen, even now that a suite draws: the report channels stay the whole
    // interface, so a bring-up problem on a new machine is never "did the screen work".
    mkdir(REPORT_DIRECTORY, 0777);

    // Opened first, because reading the mode file and asking the pad both touch services this
    // program otherwise never uses, and a run that dies in either should still leave a report.
    ReportOpen(REPORT_PATH);
    ReportNote("edenprobe starting");

    // What the mode changes is whether the finished pictures are kept in memory and shown once
    // everything is over. It changes no verdict, writes no line, and the two modes leave the
    // same report behind.
    const UiMode mode = ViewerModeAtLaunch(MODE_PATH);
    PictureSetRoot(REPORT_DIRECTORY);
    PictureWriteEveryCase(mode.every_case);
    PictureRetain(mode.detailed);

    // Order matters: the selfcheck suite is what proves the comparisons the drawing suite then
    // leans on, so a machine where both are broken says which one broke first.
    RunSelfcheck();
    RunSupport();
    RunReference();
    RunClear();
    RunRaster();
    RunIntmath();
    RunBlend();
    RunDepth();
    RunTexture();
    RunCoherence();
    RunFmath();
    ReportSummary();

    ReportClose();

    // A runner watching the log knows the process is finished and what it decided, without
    // parsing the lines above or waiting out a timeout.
    char done[96];
    snprintf(done, sizeof(done), "EDENPROBE DONE passed=%zu failed=%zu", ReportPassed(),
             ReportFailed());
    svcOutputDebugString(done, strlen(done));

    // Last, and only now: the report is written, counted and closed, so nothing a person does
    // in front of the screen can reach it.
    if (mode.detailed) {
        ViewerRun();
    }

    return ReportFailed() == 0 ? 0 : 1;
}
