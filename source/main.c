// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "mode.h"
#include "perf.h"
#include "picture.h"
#include "report.h"
#include "tests/tests.h"
#include "viewer.h"
#include "version.h"

// Where the run leaves its report on the machine it ran on. On an emulator this is a plain
// directory on the host; on the console it is the memory card. The pictures of the failing
// cases go under the same directory, so one folder is the whole of what a run leaves behind.
static const char *const REPORT_DIRECTORY = "sdmc:/edenprobe";
static const char *const REPORT_PATH = "sdmc:/edenprobe/report.jsonl";
static const char *const MODE_PATH = "sdmc:/edenprobe/mode";

/// Runs one drawing suite for its time and then for its verdict.
///
/// Each suite opens its own graphics device and closes it before it returns, so the passes before
/// the verdict pass leave it nothing to inherit but the emulator's shader cache, which a second
/// `make run` inherits anyway. They are muted: no line, no count, no picture. The verdict pass is
/// the same code with the report open, and each of its lines carries the smallest of its samples.
///
/// A hang in a rehearsal is read from two places: the report ends on this suite's timing note,
/// which says why nothing of the suite follows it, and the emulator's log ends on the
/// `EDENPROBE TIMING` line naming the pass and the case that was about to run. With the mode
/// word `notime` none of this happens and the suite runs once, as it did before cases were timed.
static void RunTimed(const char *suite, void (*run)(void), bool timing) {
    if (!timing) {
        run();
        return;
    }
    ReportNote("timing %s: %u warm-up and %u rehearsals before the verdict pass", suite,
               (unsigned)PERF_WARMUPS, (unsigned)PERF_REHEARSALS);

    PerfSuiteBegin();
    for (unsigned i = 0; i < PERF_WARMUPS + PERF_REHEARSALS; ++i) {
        PerfPassBegin(i < PERF_WARMUPS ? PERF_WARMUP : PERF_REHEARSAL);
        // After the pass has begun, so the first breadcrumb carries its number.
        ReportRehearse(suite);
        run();
    }
    ReportRehearse(NULL);
    PerfPassBegin(PERF_VERDICT);
    run();
    PerfPassBegin(PERF_UNTIMED);
    ReportPerf(suite);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // No console and no screen, even now that a suite draws: the report channels stay the whole
    // interface, so a bring-up problem on a new machine is never "did the screen work".
    mkdir(REPORT_DIRECTORY, 0777);

    // Opened first, because reading the mode file and asking the pad both touch services this
    // program otherwise never uses, and a run that dies in either should still leave a report.
    ReportOpen(REPORT_PATH);
    ReportNote("edenprobe %s (%s) starting", EDENPROBE_VERSION, EDENPROBE_BUILD);

    // What the mode changes is which families run, whether the finished cases and pictures are
    // kept in memory and shown once everything is over, and whether every drawing case writes
    // one. It changes no verdict, and before the summary the line below is all it adds to the
    // report.
    const RunMode mode = ViewerModeAtLaunch(MODE_PATH);
    char described[REPORT_NOTE_MAX + 1];
    ModeDescribe(described, sizeof(described), &mode, MODE_FAMILIES_PRESENT);
    ReportNote("%s", described);
    PictureSetRoot(REPORT_DIRECTORY);
    PictureWriteEveryCase(mode.every_case);
    PictureRetain(mode.detailed);
    ReportRetain(mode.detailed);

    // The judge first and always: the selfcheck suite proves the comparisons every family then
    // leans on, so a machine where both are broken says which one broke first.
    RunSelfcheck();
    RunSupport();
    RunSupportPerf();
    RunReference();
    // The processor's own instructions next: no device and a few microseconds, so every mode
    // runs them, whatever family is named.
    CpuSetFatal(mode.fatal);
    RunCpu();

    StreamSetStress(mode.stress);
    const uint32_t families = ModeFamiliesToRun(&mode, MODE_FAMILIES_PRESENT);
    const bool timing = !mode.no_timing;
    if ((families & MODE_FAMILY_GRAPHICS) != 0u) {
        RunTimed("clear", RunClear, timing);
        RunTimed("format", RunFormat, timing);
        RunTimed("raster", RunRaster, timing);
        RunTimed("intmath", RunIntmath, timing);
        RunTimed("blend", RunBlend, timing);
        RunTimed("depth", RunDepth, timing);
        RunTimed("texture", RunTexture, timing);
        RunTimed("coherence", RunCoherence, timing);
        RunTimed("fmath", RunFmath, timing);
        RunTimed("order", RunOrder, timing);
        RunTimed("index", RunIndex, timing);
        RunTimed("instance", RunInstance, timing);
        RunTimed("bindless", RunBindless, timing);
        RunTimed("astc", RunAstc, timing);
        RunTimed("eac", RunEac, timing);
        RunTimed("swizzle", RunSwizzle, timing);
        RunTimed("texel", RunTexel, timing);
        RunTimed("stream", RunStream, timing);
        RunTimed("fill", RunFill, timing);
        RunTimed("ring", RunRing, timing);
        RunTimed("occlusion", RunOcclusion, timing);
        RunTimed("drawtex", RunDrawtex, timing);
    }
    if ((families & MODE_FAMILY_NET) != 0u) {
        RunNet();
    }
    ReportSummary();

    ReportClose();

    // A runner watching the log knows the process is finished and what it decided, without
    // parsing the lines above or waiting out a timeout.
    char done[128];
    snprintf(done, sizeof(done), "EDENPROBE DONE passed=%zu failed=%zu skipped=%zu",
             ReportPassed(), ReportFailed(), ReportSkipped());
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
