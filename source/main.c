// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "mode.h"
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

    // What the mode changes is which families run, whether the finished pictures are kept in
    // memory and shown once everything is over, and whether every drawing case writes one. It
    // changes no verdict, and before the summary the line below is all it adds to the report.
    const RunMode mode = ViewerModeAtLaunch(MODE_PATH);
    char described[REPORT_NOTE_MAX + 1];
    ModeDescribe(described, sizeof(described), &mode, MODE_FAMILIES_PRESENT);
    ReportNote("%s", described);
    PictureSetRoot(REPORT_DIRECTORY);
    PictureWriteEveryCase(mode.every_case);
    PictureRetain(mode.detailed);

    // The judge first and always: the selfcheck suite proves the comparisons every family then
    // leans on, so a machine where both are broken says which one broke first.
    RunSelfcheck();
    RunSupport();
    RunReference();

    const uint32_t families = ModeFamiliesToRun(&mode, MODE_FAMILIES_PRESENT);
    if ((families & MODE_FAMILY_GRAPHICS) != 0u) {
        RunClear();
        RunFormat();
        RunRaster();
        RunIntmath();
        RunBlend();
        RunDepth();
        RunTexture();
        RunCoherence();
        RunFmath();
    }
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
    if (mode.detailed && ViewerRun() == VIEWER_UNATTENDED) {
        // The one line that comes after the summary, and only on this path: a detailed run
        // nobody attended says it left by itself, on both channels, so a script that forgot the
        // mode file finds out why its emulator took ten seconds longer to stop.
        ReportAppend(REPORT_PATH);
        ReportNote("detailed mode: nothing pressed for %u seconds after the report closed, "
                   "left by itself",
                   (unsigned)(UI_UNATTENDED_NANOSECONDS / 1000000000ull));
        ReportClose();
    }

    return ReportFailed() == 0 ? 0 : 1;
}
