// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "journal.h"
#include "mode.h"
#include "picture.h"
#include "report.h"
#include "runner.h"
#include "tests/tests.h"
#include "viewer.h"
#include "version.h"

// The console's frontend (docs/Frontends.md): the run itself is runner.c's, the same on every
// frontend; what is the console's own is here: its steps, the screens a person sees around the
// run, and the settings its suites take from the mode.

// Where the run leaves its report on the machine it ran on. On an emulator this is a plain
// directory on the host; on the console it is the memory card.
static const char *const PROBE_DIRECTORY = "sdmc:/edenprobe";

static void RunSupportSteps(void) {
    RunSupport();
    RunSupportPerf();
    RunSupportSettle();
}

// The order they run in, which is the roadmap's: the judge first and always, since the
// selfcheck suite proves the comparisons every family then leans on; then the processor, which
// needs no device and runs in every mode; then the families.
static const RunnerStep STEPS[] = {
    { "selfcheck", RunSelfcheck, 0, false, false },
    { "support", RunSupportSteps, 0, false, false },
    { "reference", RunReference, 0, false, false },
    { "cpu", RunCpu, 0, false, true },
    // The C library over the processor, the same cases every frontend runs (docs/families/Libc.md):
    // only when named here, so a run that names no family reports what it did before them.
    { "libc", RunLibc, MODE_FAMILY_LIBC, false, true },
    { "system", RunSystem, MODE_FAMILY_LIBC, false, true },
    { "socket", RunSocket, MODE_FAMILY_LIBC, false, true },
    // Right after the processor, which takes seconds: a person sits in front of this one, and
    // should not wait for the families that need nobody. Untimed: a person's press is the point.
    { "pad", RunPad, MODE_FAMILY_PAD, false, true },
    // The touch screen after the controller: the same person, still holding the console.
    { "touch", RunTouch, MODE_FAMILY_PAD, false, true },
    { "clear", RunClear, MODE_FAMILY_GRAPHICS, true, true },
    { "format", RunFormat, MODE_FAMILY_GRAPHICS, true, true },
    { "raster", RunRaster, MODE_FAMILY_GRAPHICS, true, true },
    { "intmath", RunIntmath, MODE_FAMILY_GRAPHICS, true, true },
    { "blend", RunBlend, MODE_FAMILY_GRAPHICS, true, true },
    { "depth", RunDepth, MODE_FAMILY_GRAPHICS, true, true },
    // After depth, whose test the depth bounds and the depth mode cases lean on.
    { "fixed", RunFixed, MODE_FAMILY_GRAPHICS, true, true },
    { "stencil", RunStencil, MODE_FAMILY_GRAPHICS, true, true },
    { "texture", RunTexture, MODE_FAMILY_GRAPHICS, true, true },
    { "lod", RunLod, MODE_FAMILY_GRAPHICS, true, true },
    // After lod: its last cases fetch a four sample texture, which leans on the texture suites.
    { "coverage", RunCoverage, MODE_FAMILY_GRAPHICS, true, true },
    { "coherence", RunCoherence, MODE_FAMILY_GRAPHICS, true, true },
    { "fmath", RunFmath, MODE_FAMILY_GRAPHICS, true, true },
    { "order", RunOrder, MODE_FAMILY_GRAPHICS, true, true },
    { "index", RunIndex, MODE_FAMILY_GRAPHICS, true, true },
    // After index, whose indexed draws the restart and indexed quad cases lean on.
    { "topology", RunTopology, MODE_FAMILY_GRAPHICS, true, true },
    { "instance", RunInstance, MODE_FAMILY_GRAPHICS, true, true },
    { "geometry", RunGeometry, MODE_FAMILY_GRAPHICS, true, true },
    { "bindless", RunBindless, MODE_FAMILY_GRAPHICS, true, true },
    { "astc", RunAstc, MODE_FAMILY_GRAPHICS, true, true },
    { "astcimage", RunAstcImage, MODE_FAMILY_GRAPHICS, true, true },
    { "eac", RunEac, MODE_FAMILY_GRAPHICS, true, true },
    { "etc2", RunEtc2, MODE_FAMILY_GRAPHICS, true, true },
    { "bcn", RunBcn, MODE_FAMILY_GRAPHICS, true, true },
    { "zeta", RunZeta, MODE_FAMILY_GRAPHICS, true, true },
    { "swizzle", RunSwizzle, MODE_FAMILY_GRAPHICS, true, true },
    { "texel", RunTexel, MODE_FAMILY_GRAPHICS, true, true },
    { "stream", RunStream, MODE_FAMILY_GRAPHICS, true, true },
    { "fill", RunFill, MODE_FAMILY_GRAPHICS, true, true },
    { "ring", RunRing, MODE_FAMILY_GRAPHICS, true, true },
    { "occlusion", RunOcclusion, MODE_FAMILY_GRAPHICS, true, true },
    { "drawtex", RunDrawtex, MODE_FAMILY_GRAPHICS, true, true },
    { "query", RunQuery, MODE_FAMILY_GRAPHICS, true, true },
    { "dma", RunDma, MODE_FAMILY_GRAPHICS, true, true },
    { "blit2d", RunBlit2d, MODE_FAMILY_GRAPHICS, true, true },
    // After dma and blit2d, whose engines its copies go through by the library's own calls.
    { "copy", RunCopy, MODE_FAMILY_GRAPHICS, true, true },
    // Last of the family, since a machine that runs out of memory on a texture this large may
    // take the launch down with it. Untimed by the passes: one pass makes the uploads a game
    // makes once, and each case times its own work.
    { "tex8k", RunTex8k, MODE_FAMILY_GRAPHICS, false, true },
    // The graphics questions asked through NVN, its own family since it runs only when named.
    // Untimed: what it compares is a machine's NVN, not how fast it runs.
    { "nvn", RunNvn, MODE_FAMILY_NVN, false, true },
    { "fs", RunFs, MODE_FAMILY_FS, true, true },
    // Untimed: what a case judges is the order its threads ran in, and each waits on purpose.
    { "sched", RunSched, MODE_FAMILY_KERNEL, false, true },
    // After every drawing suite: under Eden a graphics device made after a framebuffer was
    // closed never finished its first submit, and each case here closes one of its own.
    { "binder", RunBinder, MODE_FAMILY_DISPLAY, false, true },
    // Untimed: one job per case is the point, and the time a job takes is not.
    { "vic", RunVic, MODE_FAMILY_VIDEO, false, true },
    // Untimed too: a case runs a job per frame, and what the line judges is every image.
    { "nvdec", RunNvdec, MODE_FAMILY_VIDEO, false, true },
    // Before the net family, whose cases can wait out timeouts. Untimed: its cases wait for
    // sound to play, and a rehearsal would only play it again.
    { "audio", RunAudio, MODE_FAMILY_AUDIO, false, true },
    // Untimed: what a case judges is a service's answer, and some open two dozen objects.
    { "service", RunService, MODE_FAMILY_SERVICE, false, true },
    { "net", RunNet, MODE_FAMILY_NET, false, true },
    // Last: it asks everything the families above ask one at a time, all at once. Untimed by
    // the passes: it runs its frames once and times each of them itself.
    { "game", RunGame, MODE_FAMILY_GAME, false, true },
};

// The console's frontend for the runner: its folder, its build and its steps.
static const RunnerFrontend FRONTEND = {
    .root = PROBE_DIRECTORY,
    .version = EDENPROBE_VERSION,
    .build = EDENPROBE_BUILD,
    .hash = EDENPROBE_HASH,
    .steps = STEPS,
    .step_count = sizeof(STEPS) / sizeof(STEPS[0]),
    .present = MODE_FAMILIES_PRESENT,
    .asked = MODE_FAMILIES_ASKED,
};

// The run, static: the journal and the crash list it holds are tens of kilobytes.
static Runner runner;

/// Writes the words the start screen chose into the mode file, when it was asked to; a file that
/// cannot be written leaves the choice this run's alone, and says so.
static void SaveModeFile(RunMode *mode) {
    if (!mode->saved_by_screen) {
        return;
    }
    static char text[MODE_FILE_MAX];
    const size_t length = ModeFormat(text, sizeof(text) - 1u, mode);
    text[length] = '\n';
    text[length + 1u] = '\0';
    if (!JournalWriteFile(runner.mode_path, text)) {
        mode->saved_by_screen = false;
        ReportNote("mode file: %s could not be written, so what the start screen chose is this "
                   "run's only",
                   runner.mode_path);
    }
}

/// The start screen, when a new detailed run begins: the menu that chooses the families, quick,
/// notime, the crash list's cases, and whether to keep the choice in the mode file. Hands back
/// the mode the run begins with; as it came when there was no screen.
static RunMode StartScreen(const RunMode *from_file) {
    static UiStartInfo info;
    memset(&info, 0, sizeof(info));
    static char heading[96];
    snprintf(heading, sizeof(heading), "edenprobe %s (%s)", EDENPROBE_VERSION, EDENPROBE_BUILD);
    info.heading = heading;
    info.families_present = MODE_FAMILIES_PRESENT;
    info.nvn_named = (ModeFamiliesNamed(from_file, MODE_FAMILIES_PRESENT) & MODE_FAMILY_NVN) != 0u;
    const CrashList *const listed = ReportCrashList();
    for (uint32_t i = 0; i < listed->count && info.listed_count < UI_START_LIST_MAX; ++i) {
        info.listed_suite[info.listed_count] = listed->entries[i].suite;
        info.listed_name[info.listed_count] = listed->entries[i].name;
        ++info.listed_count;
    }
    UiStartState chosen;
    if (!UiStartShown(&info) || !ViewerStartScreen(&info, from_file, &chosen)) {
        return *from_file;
    }
    return UiStartMode(&chosen, *from_file, MODE_FAMILIES_PRESENT);
}

/// The screen that asks whether to go on with the run the journal left in the middle, before
/// anything is opened. Starting over makes the plan a new run's; the case the program ended
/// inside stays in it, for the crash list.
static void AskToGoOn(const RunMode *mode) {
    static UiResumeInfo info;
    memset(&info, 0, sizeof(info));
    static char heading[96];
    snprintf(heading, sizeof(heading), "edenprobe %s (%s)", EDENPROBE_VERSION, EDENPROBE_BUILD);
    static char where[JOURNAL_SUITE_MAX + JOURNAL_NAME_MAX + 96u];
    JournalDescribeStop(where, sizeof(where), &runner.plan, &runner.journal);
    info.heading = heading;
    info.where = where;
    info.folder = runner.run_directory;
    info.launch = runner.plan.launch;
    const uint32_t families = RunnerFamilies(&runner, mode);
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        if (STEPS[i].family == 0u || (families & STEPS[i].family) != 0u) {
            ++info.steps;
            info.steps_done += JournalStepDone(&runner.journal, STEPS[i].name) ? 1u : 0u;
        }
    }
    info.passed = runner.plan.counts.counts[JOURNAL_PASSED];
    info.failed = runner.plan.counts.counts[JOURNAL_FAILED];
    info.skipped = runner.plan.counts.counts[JOURNAL_SKIPPED];
    info.crashed = runner.plan.counts.counts[JOURNAL_CRASHED];
    bool start_over = false;
    if (ViewerResumeScreen(&info, &start_over) && start_over) {
        JournalPlanRestart(&runner.plan);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // No console. The report channels stay the interface a verdict is read from, so a bring-up
    // problem on a new machine is never "did the screen work". The screens up before the report
    // closes, the one that asks whether to go on, the start screen, the running screen and the
    // listening sequence, only choose what runs and say what is happening.
    (void)RunnerInit(&runner, &FRONTEND);

    // What the card says before anything is opened: the mode file's words, then, in the folder
    // they and this build name, the journal of the run before, and the crash list. Whether this
    // launch goes on with that run or starts a new one depends on all three, and on this build.
    const RunMode from_file = RunnerModeFromFile(runner.mode_path);
    RunnerPlan(&runner, &from_file);
    RunMode mode = RunnerModeAtStart(&runner, &from_file);
    // A person in front of a detailed run chooses whether to go on with the run left in the
    // middle or start over; a quick run is a script's, and goes on.
    if (JournalPlanAsks(&runner.plan, mode.detailed)) {
        AskToGoOn(&mode);
        if (runner.plan.start == JOURNAL_FRESH) {
            mode = from_file;
        }
    }

    // Opened first, because reading the pad touches a service this program otherwise never
    // uses, and a run that dies there should still leave a report.
    RunnerOpen(&runner, &mode);

    // What the mode changes is which families run, whether the finished cases and pictures are
    // kept in memory and shown once everything is over, and whether every drawing case writes
    // one. It changes no verdict, and before the summary the line below is all it adds to the
    // report; a launch that goes on with a run said it when the run began, and keeps what that
    // launch's start screen chose. A new detailed run's start screen chooses it, and the
    // journal begins once it has.
    mode = ViewerModeWithPad(mode);
    if (runner.plan.start == JOURNAL_FRESH) {
        const RunMode asked = mode;
        if (mode.detailed) {
            mode = StartScreen(&asked);
            SaveModeFile(&mode);
        }
        RunnerBeginJournal(&runner, &asked, &mode);
    }
    const uint32_t families = RunnerFamilies(&runner, &mode);
    RunnerApplyMode(&runner, &mode);
    // A step the screens know no family for is shown under other, away from its own. The net
    // and service steps report under their suites' names, which the screens know.
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        if (strcmp(STEPS[i].name, "net") != 0 && strcmp(STEPS[i].name, "service") != 0 &&
            UiFamilyOf(STEPS[i].name) == UI_FAMILY_OTHER) {
            ReportNote("bookkeeping: the screens have no family for suite %s and show it under "
                       "other",
                       STEPS[i].name);
        }
    }
    PictureWriteEveryCase(mode.every_case);
    PictureRetain(mode.detailed);
    ReportRetain(mode.detailed);

    StreamSetStress(mode.stress);
    AudioSetListen(mode.listen);
    const bool timing = !mode.no_timing;
    GameSetFrames(mode.frames);
    GameSetTiming(timing);
    Tex8kSetTiming(timing);
    Tex8kSetVram(mode.vram);
    // The running screen, for a run with a screen: a detailed one, or one the start screen made
    // quick. Counted over every step the run has, done ones included, so a launch that goes on
    // says how far the whole run is.
    uint32_t steps = 0;
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        steps += STEPS[i].family == 0u || (families & STEPS[i].family) != 0u ? 1u : 0u;
    }
    static char running[96];
    snprintf(running, sizeof(running), "edenprobe %s (%s)", EDENPROBE_VERSION, EDENPROBE_BUILD);
    PadSetHeading(running);
    TouchSetHeading(running);
    if (!mode.no_progress) {
        ViewerProgressBegin(running, runner.run_directory, steps, !mode.detailed, mode.detailed);
    }
    RunnerSteps(&runner, families, timing, ViewerProgressStep);
    ViewerProgressEnd();
    RunnerFinish(&runner, &mode, timing);

    // Last, and only now: the report is written, counted and closed, so nothing a person does
    // in front of the screen can reach it.
    if (mode.detailed && ViewerRun() == VIEWER_UNATTENDED) {
        // The one line that comes after the summary, and only on this path: a detailed run
        // nobody attended says it left by itself, on both channels, so a script that forgot the
        // mode file finds out why its emulator took ten seconds longer to stop.
        ReportAppend(runner.report_path);
        ReportNote("detailed mode: nothing pressed for %u seconds after the report closed, "
                   "left by itself",
                   (unsigned)(UI_UNATTENDED_NANOSECONDS / 1000000000ull));
        ReportClose();
    }
    // A quick run the start screen chose has its screen still open.
    ViewerEndScreen();

    return ReportFailed() == 0 ? 0 : 1;
}
