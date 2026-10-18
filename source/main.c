// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "journal.h"
#include "maycrash.h"
#include "mode.h"
#include "perf.h"
#include "picture.h"
#include "report.h"
#include "settle.h"
#include "tests/tests.h"
#include "viewer.h"
#include "version.h"

// Where the run leaves its report on the machine it ran on. On an emulator this is a plain
// directory on the host; on the console it is the memory card. Each build, and each label the
// mode file gives, has a folder of its own under runs/, named after its hash: the report, the
// pictures of the failing cases and the journal go there, so one folder is the whole of what a
// run leaves behind and runs of two builds never mix. The mode file, the net file and the
// crash list stay at the top, since they belong to the person and the machine, not a build.
static const char *const PROBE_DIRECTORY = "sdmc:/edenprobe";
static const char *const RUNS_DIRECTORY = "sdmc:/edenprobe/runs";
static const char *const MODE_PATH = "sdmc:/edenprobe/mode";
// The folder the latest launch went to, relative to sdmc:/edenprobe: how a runner, or a person
// with a file manager, finds the report without knowing the build's hash.
static const char *const LAST_RUN_PATH = "sdmc:/edenprobe/last-run";

static char run_directory[128];
static char report_path[sizeof(run_directory) + 16u];
static char journal_path[sizeof(run_directory) + 16u];

/// The run's folder for this build and @p mode's label, made if it is not there, and every path
/// under it; and last-run, naming it.
static void PlaceRun(const RunMode *mode) {
    char folder[MODE_FOLDER_MAX + 1u];
    if (ModeRunFolder(folder, sizeof(folder), EDENPROBE_HASH, mode) == 0u) {
        snprintf(folder, sizeof(folder), "unknown");
    }
    snprintf(run_directory, sizeof(run_directory), "%s/%s", RUNS_DIRECTORY, folder);
    snprintf(report_path, sizeof(report_path), "%s/report.jsonl", run_directory);
    snprintf(journal_path, sizeof(journal_path), "%s/%s", run_directory, JOURNAL_FILE_NAME);
    mkdir(RUNS_DIRECTORY, 0777);
    mkdir(run_directory, 0777);
    char last[MODE_FOLDER_MAX + 8u];
    snprintf(last, sizeof(last), "runs/%s\n", folder);
    (void)JournalWriteFile(LAST_RUN_PATH, last);
}

_Static_assert(sizeof("sdmc:/edenprobe/runs/") + MODE_FOLDER_MAX <= sizeof(run_directory),
               "the longest folder fits run_directory");

/// What main runs as one, and the journal records as one: a suite, or a family run by one call.
typedef struct {
    const char *name;
    void (*run)(void);
    /// The family it belongs to, or nought for the steps every mode runs.
    uint32_t family;
    /// A drawing suite, timed with a warm-up and rehearsals before its verdict pass.
    bool timed;
    /// Its cases ask the machine something, so each says it started before its work.
    bool asks;
} Step;

static void RunSupportSteps(void) {
    RunSupport();
    RunSupportPerf();
    RunSupportSettle();
}

// The order they run in, which is the roadmap's: the judge first and always, since the
// selfcheck suite proves the comparisons every family then leans on; then the processor, which
// needs no device and runs in every mode; then the families.
static const Step STEPS[] = {
    { "selfcheck", RunSelfcheck, 0, false, false },
    { "support", RunSupportSteps, 0, false, false },
    { "reference", RunReference, 0, false, false },
    { "cpu", RunCpu, 0, false, true },
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
    { "texture", RunTexture, MODE_FAMILY_GRAPHICS, true, true },
    { "coherence", RunCoherence, MODE_FAMILY_GRAPHICS, true, true },
    { "fmath", RunFmath, MODE_FAMILY_GRAPHICS, true, true },
    { "order", RunOrder, MODE_FAMILY_GRAPHICS, true, true },
    { "index", RunIndex, MODE_FAMILY_GRAPHICS, true, true },
    { "instance", RunInstance, MODE_FAMILY_GRAPHICS, true, true },
    { "geometry", RunGeometry, MODE_FAMILY_GRAPHICS, true, true },
    { "bindless", RunBindless, MODE_FAMILY_GRAPHICS, true, true },
    { "astc", RunAstc, MODE_FAMILY_GRAPHICS, true, true },
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
};

static void JournalSimple(JournalKind kind, const char *step, uint32_t number) {
    JournalRecord record;
    memset(&record, 0, sizeof(record));
    record.kind = kind;
    snprintf(record.suite, sizeof(record.suite), "%s", step != NULL ? step : "");
    record.number = number;
    ReportJournalWrite(&record);
}

/// Runs one drawing suite for its time and then for its verdict.
///
/// Each suite opens its own graphics device and closes it before it returns, so the passes before
/// the verdict pass leave it nothing to inherit but the emulator's shader cache, which a second
/// `make run` inherits anyway. They are muted: no line, no count, no picture. The verdict pass is
/// the same code with the report open, and each of its lines carries the smallest of its samples.
///
/// A hang in a rehearsal is read from three places: the report ends on this suite's timing note,
/// which says why nothing of the suite follows it, the emulator's log ends on the
/// `EDENPROBE TIMING` line naming the pass and the case that was about to run, and the journal
/// ends on that case's start. With the mode word `notime` none of this happens and the suite
/// runs once, as it did before cases were timed.
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
        JournalSimple(JOURNAL_PASS, suite, PerfPassNumber());
        // After the pass has begun, so the first breadcrumb carries its number.
        ReportRehearse(suite);
        run();
    }
    ReportRehearse(NULL);
    PerfPassBegin(PERF_VERDICT);
    JournalSimple(JOURNAL_PASS, suite, PerfPassNumber());
    run();
    PerfPassBegin(PERF_UNTIMED);
    ReportPerf(suite);
}

// What the card holds about the run before this launch, and the crash list.
static JournalState journal;
static CrashList crashes;

/// Starts the report the way the plan says, and the journal of a run that goes on. A new run's
/// journal is begun by BeginJournal, once the start screen has chosen what runs; the run before's
/// goes now, so a launch that ends before then does not go on with that run in this report.
static void OpenRun(const JournalPlan *plan) {
    if (plan->start == JOURNAL_FRESH) {
        ReportOpen(report_path);
        ReportNote("edenprobe %s (%s) starting", EDENPROBE_VERSION, EDENPROBE_BUILD);
        ReportNote("folder: %s", run_directory);
        (void)remove(journal_path);
        return;
    }
    ReportResume(report_path, plan->offset, plan->start == JOURNAL_RESUME_CASE, &plan->counts);
    if (!ReportJournalOpen(journal_path, false)) {
        ReportNote("journal: %s cannot be written to go on with, so a crash from here costs "
                   "the rest of the run",
                   journal_path);
    }
    JournalSimple(JOURNAL_LAUNCH, NULL, plan->launch);
    if (plan->start == JOURNAL_RESUME_CASE) {
        JournalRecord record = plan->crash;
        record.kind = JOURNAL_CRASH;
        ReportJournalWrite(&record);
        // What the step drew before the program ended: it runs again from its start, and the
        // pictures it leaves are the ones its new lines name.
        (void)PictureRemoveSuite(journal.step.suite);
    } else if (plan->start == JOURNAL_RESUME_STEP) {
        JournalSimple(JOURNAL_DONE, journal.step.suite, 0);
    }
}

/// A new run's journal: the run record, with the words a launch that goes on with it will find
/// in the mode file, the screen's when it wrote them there; what the screen chose, when the file
/// does not say it; and the first launch.
static void BeginJournal(const RunMode *from_file, const RunMode *mode) {
    if (mode->no_journal) {
        return;
    }
    if (!ReportJournalOpen(journal_path, true)) {
        ReportNote("journal: %s cannot be written, so a crash costs the rest of the run",
                   journal_path);
        return;
    }
    char run_key[JOURNAL_MODE_MAX];
    char chosen[JOURNAL_MODE_MAX];
    ModeJournalWords(run_key, sizeof(run_key), chosen, sizeof(chosen), from_file, mode);
    JournalRecord record;
    memset(&record, 0, sizeof(record));
    record.kind = JOURNAL_RUN;
    snprintf(record.suite, sizeof(record.suite), "%s", EDENPROBE_VERSION);
    snprintf(record.name, sizeof(record.name), "%s", EDENPROBE_BUILD);
    snprintf(record.mode, sizeof(record.mode), "%s", run_key);
    ReportJournalWrite(&record);
    if (chosen[0] != '\0') {
        memset(&record, 0, sizeof(record));
        record.kind = JOURNAL_CHOSEN;
        snprintf(record.mode, sizeof(record.mode), "%s", chosen);
        ReportJournalWrite(&record);
    }
    JournalSimple(JOURNAL_LAUNCH, NULL, 1u);
}

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
    if (!JournalWriteFile(MODE_PATH, text)) {
        mode->saved_by_screen = false;
        ReportNote("mode file: %s could not be written, so what the start screen chose is this "
                   "run's only",
                   MODE_PATH);
    }
}

/// The start screen, when a new detailed run begins: the menu that chooses the families, quick,
/// notime, the may-crash table's cases and the crash list's, and whether to keep the choice in the
/// mode file. Hands back the mode the run begins with; as it came when there was no screen.
static RunMode StartScreen(const RunMode *from_file) {
    static UiStartInfo info;
    memset(&info, 0, sizeof(info));
    static char heading[96];
    snprintf(heading, sizeof(heading), "edenprobe %s (%s)", EDENPROBE_VERSION, EDENPROBE_BUILD);
    info.heading = heading;
    info.families_present = MODE_FAMILIES_PRESENT;
    // Every entry, with its family: the screen counts those of the families chosen.
    for (size_t i = 0; i < MayCrashCount() && info.may_crash_count < UI_START_LIST_MAX; ++i) {
        const MayCrash *const entry = MayCrashAt(i);
        info.may_crash_suite[info.may_crash_count] = entry->suite;
        info.may_crash_name[info.may_crash_count] = entry->name;
        info.may_crash_family[info.may_crash_count] = entry->family;
        ++info.may_crash_count;
    }
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

/// Whether @p step runs in a run of @p families: the steps every mode runs, and its families'.
static bool StepRuns(const Step *step, uint32_t families) {
    return step->family == 0u || (families & step->family) != 0u;
}

/// The screen that asks whether to go on with the run the journal left in the middle, before
/// anything is opened. Starting over makes the plan a new run's; the case the program ended
/// inside stays in it, for the crash list.
static void AskToGoOn(JournalPlan *plan, const RunMode *mode) {
    static UiResumeInfo info;
    memset(&info, 0, sizeof(info));
    static char heading[96];
    snprintf(heading, sizeof(heading), "edenprobe %s (%s)", EDENPROBE_VERSION, EDENPROBE_BUILD);
    static char where[JOURNAL_SUITE_MAX + JOURNAL_NAME_MAX + 96u];
    JournalDescribeStop(where, sizeof(where), plan, &journal);
    info.heading = heading;
    info.where = where;
    info.folder = run_directory;
    info.launch = plan->launch;
    const uint32_t families = ModeFamiliesToRun(mode, MODE_FAMILIES_PRESENT);
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        if (StepRuns(&STEPS[i], families)) {
            ++info.steps;
            info.steps_done += JournalStepDone(&journal, STEPS[i].name) ? 1u : 0u;
        }
    }
    info.passed = plan->counts.counts[JOURNAL_PASSED];
    info.failed = plan->counts.counts[JOURNAL_FAILED];
    info.skipped = plan->counts.counts[JOURNAL_SKIPPED];
    info.crashed = plan->counts.counts[JOURNAL_CRASHED];
    bool start_over = false;
    if (ViewerResumeScreen(&info, &start_over) && start_over) {
        JournalPlanRestart(plan);
    }
}

/// The note before the summary of a run that took more than one launch or skipped cases as
/// crashed: how many launches, how many such cases, and which ended the program in this run.
static void NoteCrashes(const JournalPlan *plan) {
    char text[REPORT_NOTE_MAX + 1];
    if (JournalDescribeRun(text, sizeof(text), plan, &journal, (uint32_t)ReportCrashed()) > 0u) {
        ReportNote("%s", text);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // No console. The report channels stay the interface a verdict is read from, so a bring-up
    // problem on a new machine is never "did the screen work". The screens up before the report
    // closes, the one that asks whether to go on, the start screen, the running screen and the
    // listening sequence, only choose what runs and say what is happening.
    mkdir(PROBE_DIRECTORY, 0777);

    // What the card says before anything is opened: the mode file's words, then, in the folder
    // they and this build name, the journal of the run before, and the crash list. Whether this
    // launch goes on with that run or starts a new one depends on all three, and on this build.
    const RunMode from_file = ViewerModeFromFile(MODE_PATH);
    PlaceRun(&from_file);
    PictureSetRoot(run_directory);
    char key[JOURNAL_MODE_MAX];
    ModeKey(key, sizeof(key), &from_file);
    JournalStateInit(&journal);
    (void)JournalReadPath(&journal, journal_path);
    CrashListInit(&crashes);
    {
        static char text[CRASH_LIST_MAX * (JOURNAL_SUITE_MAX + JOURNAL_NAME_MAX + 2u) + 1024u];
        const int64_t length = JournalReadFile(CRASH_LIST_PATH, text, sizeof(text), NULL);
        if (length > 0) {
            CrashListParse(&crashes, text, (size_t)length);
        }
    }
    JournalPlan plan = JournalPlanStart(&journal, EDENPROBE_VERSION, EDENPROBE_BUILD, key,
                                        JournalFileSize(report_path));
    if (from_file.no_journal && plan.start != JOURNAL_FRESH) {
        // Asked for no journal: nothing goes on, and the run starts over unjournaled.
        plan.start = JOURNAL_FRESH;
        plan.launch = 1u;
    }
    // A launch that goes on with a run keeps what its start screen chose; the label is the
    // file's, since it named the folder the journal is in.
    RunMode mode = plan.start != JOURNAL_FRESH ? ModeWithChosen(from_file, journal.chosen)
                                               : from_file;
    // A person in front of a detailed run chooses whether to go on with the run left in the
    // middle or start over; a quick run is a script's, and goes on.
    if (JournalPlanAsks(&plan, mode.detailed)) {
        AskToGoOn(&plan, &mode);
        if (plan.start == JOURNAL_FRESH) {
            mode = from_file;
        }
    }

    // Opened first, because reading the pad touches a service this program otherwise never
    // uses, and a run that dies there should still leave a report.
    OpenRun(&plan);
    ReportSetCrashList(&crashes, CRASH_LIST_PATH, mode.retry);
    // A case the program ended inside goes on the crash list whether this run goes on or not:
    // the machine did end inside it.
    const bool listed = plan.crashed && ReportCrashListAdd(plan.crash.suite, plan.crash.name);
    const bool usual = plan.start == JOURNAL_FRESH && !plan.crashed &&
                       (plan.why_new == JOURNAL_NEW_NO_JOURNAL ||
                        plan.why_new == JOURNAL_NEW_FINISHED);
    if (!usual) {
        char said[REPORT_NOTE_MAX + 1];
        JournalDescribePlan(said, sizeof(said), &plan, &journal);
        ReportNote("%s%s", said,
                   plan.crashed && !listed ? " (it was on the crash list already)" : "");
    }
    if (plan.start != JOURNAL_FRESH) {
        for (uint32_t i = 0; i < journal.crash_count; ++i) {
            ReportAddCrashed(&journal.crashes[i]);
        }
        if (plan.start == JOURNAL_RESUME_CASE) {
            ReportAddCrashed(&plan.crash);
            if (journal.crash_count < JOURNAL_CRASHES_MAX) {
                journal.crashes[journal.crash_count++] = plan.crash;
            }
        }
    }

    // What the mode changes is which families run, whether the finished cases and pictures are
    // kept in memory and shown once everything is over, and whether every drawing case writes
    // one. It changes no verdict, and before the summary the line below is all it adds to the
    // report; a launch that goes on with a run said it when the run began, and keeps what that
    // launch's start screen chose. A new detailed run's start screen chooses it, and the
    // journal begins once it has.
    mode = ViewerModeWithPad(mode);
    if (plan.start == JOURNAL_FRESH) {
        const RunMode asked = mode;
        if (mode.detailed) {
            mode = StartScreen(&asked);
            SaveModeFile(&mode);
        }
        BeginJournal(&asked, &mode);
    }
    const uint32_t families = ModeFamiliesToRun(&mode, MODE_FAMILIES_PRESENT);
    ReportSetRetry(mode.retry);
    ReportSetMayCrash(mode.fatal);
    ReportSetHang(mode.hang);
    if (plan.start == JOURNAL_FRESH) {
        char described[REPORT_NOTE_MAX + 1];
        ModeDescribe(described, sizeof(described), &mode, MODE_FAMILIES_PRESENT);
        ReportNote("%s", described);
    }
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
    // The running screen, for a run with a screen: a detailed one, or one the start screen made
    // quick. Counted over every step the run has, done ones included, so a launch that goes on
    // says how far the whole run is.
    uint32_t steps = 0;
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        steps += StepRuns(&STEPS[i], families) ? 1u : 0u;
    }
    static char running[96];
    snprintf(running, sizeof(running), "edenprobe %s (%s)", EDENPROBE_VERSION, EDENPROBE_BUILD);
    PadSetHeading(running);
    TouchSetHeading(running);
    if (!mode.no_progress) {
        ViewerProgressBegin(running, run_directory, steps, !mode.detailed, mode.detailed);
    }
    uint32_t step_number = 0;
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        const Step *const step = &STEPS[i];
        if (!StepRuns(step, families)) {
            continue;
        }
        ++step_number;
        if (plan.start != JOURNAL_FRESH &&
            (JournalStepDone(&journal, step->name) ||
             (plan.start == JOURNAL_RESUME_STEP && journal.in_step &&
              strcmp(journal.step.suite, step->name) == 0))) {
            continue;
        }
        JournalRecord record;
        memset(&record, 0, sizeof(record));
        record.kind = JOURNAL_STEP;
        snprintf(record.suite, sizeof(record.suite), "%s", step->name);
        record.offset = ReportOffset();
        record.counts = ReportCounts();
        ReportJournalWrite(&record);
        ViewerProgressStep(step_number, step->name, step->timed && timing);
        ReportRequireStarts(step->asks);
        if (step->timed) {
            RunTimed(step->name, step->run, timing);
        } else {
            step->run();
        }
        ReportRequireStarts(false);
        JournalSimple(JOURNAL_DONE, step->name, 0);
    }
    ViewerProgressEnd();
    {
        // A table entry this launch's suites never reached names a case this build lacks, and
        // would protect nothing.
        char unreached[REPORT_NOTE_MAX - 96];
        if (ReportMayCrashUnreached(unreached, sizeof(unreached)) > 0u) {
            ReportNote("may crash: the table names %s, which the suites that ran never reached",
                       unreached);
        }
    }
    // Only when some close had to wait: a machine that delivers its copies before the queue is
    // idle, as the console does, gets the same report it always did.
    char settled[REPORT_NOTE_MAX + 1];
    if (SettleDescribe(settled, sizeof(settled), SettleRun())) {
        ReportNote("%s", settled);
    }
    NoteCrashes(&plan);
    ReportSummary();

    ReportClose();
    JournalRecord finished;
    memset(&finished, 0, sizeof(finished));
    finished.kind = JOURNAL_FINISHED;
    finished.counts = ReportCounts();
    ReportJournalWrite(&finished);
    ReportJournalClose();

    // A runner watching the log knows the process is finished and what it decided, without
    // parsing the lines above or waiting out a timeout.
    char done[160];
    snprintf(done, sizeof(done), "EDENPROBE DONE passed=%zu failed=%zu skipped=%zu crashed=%zu",
             ReportPassed(), ReportFailed(), ReportSkipped(), ReportCrashed());
    svcOutputDebugString(done, strlen(done));

    // Last, and only now: the report is written, counted and closed, so nothing a person does
    // in front of the screen can reach it.
    if (mode.detailed && ViewerRun() == VIEWER_UNATTENDED) {
        // The one line that comes after the summary, and only on this path: a detailed run
        // nobody attended says it left by itself, on both channels, so a script that forgot the
        // mode file finds out why its emulator took ten seconds longer to stop.
        ReportAppend(report_path);
        ReportNote("detailed mode: nothing pressed for %u seconds after the report closed, "
                   "left by itself",
                   (unsigned)(UI_UNATTENDED_NANOSECONDS / 1000000000ull));
        ReportClose();
    }
    // A quick run the start screen chose has its screen still open.
    ViewerEndScreen();

    return ReportFailed() == 0 ? 0 : 1;
}
