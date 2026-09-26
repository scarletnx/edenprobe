// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

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
#include "tests/tests.h"
#include "viewer.h"
#include "version.h"

// Where the run leaves its report on the machine it ran on. On an emulator this is a plain
// directory on the host; on the console it is the memory card. The pictures of the failing
// cases go under the same directory, so one folder is the whole of what a run leaves behind.
static const char *const REPORT_DIRECTORY = "sdmc:/edenprobe";
static const char *const REPORT_PATH = "sdmc:/edenprobe/report.jsonl";
static const char *const MODE_PATH = "sdmc:/edenprobe/mode";

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
}

// The order they run in, which is the roadmap's: the judge first and always, since the
// selfcheck suite proves the comparisons every family then leans on; then the processor, which
// needs no device and runs in every mode; then the families.
static const Step STEPS[] = {
    { "selfcheck", RunSelfcheck, 0, false, false },
    { "support", RunSupportSteps, 0, false, false },
    { "reference", RunReference, 0, false, false },
    { "cpu", RunCpu, 0, false, true },
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
    { "bindless", RunBindless, MODE_FAMILY_GRAPHICS, true, true },
    { "astc", RunAstc, MODE_FAMILY_GRAPHICS, true, true },
    { "eac", RunEac, MODE_FAMILY_GRAPHICS, true, true },
    { "swizzle", RunSwizzle, MODE_FAMILY_GRAPHICS, true, true },
    { "texel", RunTexel, MODE_FAMILY_GRAPHICS, true, true },
    { "stream", RunStream, MODE_FAMILY_GRAPHICS, true, true },
    { "fill", RunFill, MODE_FAMILY_GRAPHICS, true, true },
    { "ring", RunRing, MODE_FAMILY_GRAPHICS, true, true },
    { "occlusion", RunOcclusion, MODE_FAMILY_GRAPHICS, true, true },
    { "drawtex", RunDrawtex, MODE_FAMILY_GRAPHICS, true, true },
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

/// Starts the report and the journal the way the plan says.
static void OpenRun(const JournalPlan *plan, const RunMode *mode, const char *key) {
    if (plan->start == JOURNAL_FRESH) {
        ReportOpen(REPORT_PATH);
        ReportNote("edenprobe %s (%s) starting", EDENPROBE_VERSION, EDENPROBE_BUILD);
        if (mode->no_journal) {
            // A journal left by a run before this one would be taken, by the next launch, for
            // this run's.
            (void)remove(JOURNAL_PATH);
            return;
        }
        if (!ReportJournalOpen(JOURNAL_PATH, true)) {
            ReportNote("journal: %s cannot be written, so a crash costs the rest of the run",
                       JOURNAL_PATH);
            return;
        }
        JournalRecord record;
        memset(&record, 0, sizeof(record));
        record.kind = JOURNAL_RUN;
        snprintf(record.suite, sizeof(record.suite), "%s", EDENPROBE_VERSION);
        snprintf(record.name, sizeof(record.name), "%s", EDENPROBE_BUILD);
        snprintf(record.mode, sizeof(record.mode), "%s", key);
        ReportJournalWrite(&record);
        JournalSimple(JOURNAL_LAUNCH, NULL, 1u);
        return;
    }
    ReportResume(REPORT_PATH, plan->offset, plan->start == JOURNAL_RESUME_CASE, &plan->counts);
    if (!ReportJournalOpen(JOURNAL_PATH, false)) {
        ReportNote("journal: %s cannot be written to go on with, so a crash from here costs "
                   "the rest of the run",
                   JOURNAL_PATH);
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

/// The start screen, when a detailed run begins and something on it can be chosen: the
/// may-crash table's cases in the families that run, and the crash list's. What it adds to the
/// mode goes into the journal, so a launch that goes on with the run keeps it.
static void StartScreen(RunMode *mode, uint32_t families) {
    static UiStartInfo info;
    memset(&info, 0, sizeof(info));
    static char heading[96];
    snprintf(heading, sizeof(heading), "edenprobe %s (%s)", EDENPROBE_VERSION, EDENPROBE_BUILD);
    info.heading = heading;
    for (size_t i = 0; i < MayCrashCount() && info.may_crash_count < UI_START_LIST_MAX; ++i) {
        const MayCrash *const entry = MayCrashAt(i);
        // The cpu suite runs in every mode, whatever family is named.
        if (entry->family == MODE_FAMILY_CPU || (families & entry->family) != 0u) {
            info.may_crash_suite[info.may_crash_count] = entry->suite;
            info.may_crash_name[info.may_crash_count] = entry->name;
            ++info.may_crash_count;
        }
    }
    const CrashList *const listed = ReportCrashList();
    for (uint32_t i = 0; i < listed->count && info.listed_count < UI_START_LIST_MAX; ++i) {
        info.listed_suite[info.listed_count] = listed->entries[i].suite;
        info.listed_name[info.listed_count] = listed->entries[i].name;
        ++info.listed_count;
    }
    if (!UiStartShown(&info)) {
        return;
    }
    bool may_crash = mode->fatal;
    bool retry = mode->retry;
    if (!ViewerStartScreen(&info, &may_crash, &retry)) {
        return;
    }
    mode->fatal_by_screen = may_crash && !mode->fatal;
    mode->retry_by_screen = retry && !mode->retry;
    mode->fatal = may_crash;
    mode->retry = retry;
    if (mode->fatal_by_screen || mode->retry_by_screen) {
        JournalRecord record;
        memset(&record, 0, sizeof(record));
        record.kind = JOURNAL_CHOSEN;
        snprintf(record.mode, sizeof(record.mode), "%s%s%s", mode->fatal_by_screen ? "fatal" : "",
                 mode->fatal_by_screen && mode->retry_by_screen ? " " : "",
                 mode->retry_by_screen ? "retry" : "");
        ReportJournalWrite(&record);
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

    // No console and no screen, even now that a suite draws: the report channels stay the whole
    // interface, so a bring-up problem on a new machine is never "did the screen work".
    mkdir(REPORT_DIRECTORY, 0777);
    PictureSetRoot(REPORT_DIRECTORY);

    // What the card says before anything is opened: the mode file's words, the journal of the
    // run before, and the crash list. Whether this launch goes on with that run or starts a new
    // one depends on all three, and on this build.
    RunMode mode = ViewerModeFromFile(MODE_PATH);
    char key[JOURNAL_MODE_MAX];
    ModeKey(key, sizeof(key), &mode);
    JournalStateInit(&journal);
    (void)JournalReadPath(&journal, JOURNAL_PATH);
    CrashListInit(&crashes);
    {
        static char text[CRASH_LIST_MAX * (JOURNAL_SUITE_MAX + JOURNAL_NAME_MAX + 2u) + 1024u];
        const int64_t length = JournalReadFile(CRASH_LIST_PATH, text, sizeof(text), NULL);
        if (length > 0) {
            CrashListParse(&crashes, text, (size_t)length);
        }
    }
    JournalPlan plan = JournalPlanStart(&journal, EDENPROBE_VERSION, EDENPROBE_BUILD, key,
                                        JournalFileSize(REPORT_PATH));
    if (mode.no_journal && plan.start != JOURNAL_FRESH) {
        // Asked for no journal: nothing goes on, and the run starts over unjournaled.
        plan.start = JOURNAL_FRESH;
        plan.launch = 1u;
    }

    // Opened first, because reading the pad touches a service this program otherwise never
    // uses, and a run that dies there should still leave a report.
    OpenRun(&plan, &mode, key);
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
    // launch's start screen chose.
    mode = ViewerModeWithPad(mode);
    const uint32_t families = ModeFamiliesToRun(&mode, MODE_FAMILIES_PRESENT);
    if (plan.start != JOURNAL_FRESH) {
        const RunMode chosen = ModeFromText(journal.chosen);
        mode.fatal_by_screen = chosen.fatal && !mode.fatal;
        mode.retry_by_screen = chosen.retry && !mode.retry;
        mode.fatal = mode.fatal || chosen.fatal;
        mode.retry = mode.retry || chosen.retry;
    } else if (mode.detailed) {
        StartScreen(&mode, families);
    }
    ReportSetRetry(mode.retry);
    ReportSetMayCrash(mode.fatal);
    if (plan.start == JOURNAL_FRESH) {
        char described[REPORT_NOTE_MAX + 1];
        ModeDescribe(described, sizeof(described), &mode, MODE_FAMILIES_PRESENT);
        ReportNote("%s", described);
    }
    PictureWriteEveryCase(mode.every_case);
    PictureRetain(mode.detailed);
    ReportRetain(mode.detailed);

    StreamSetStress(mode.stress);
    const bool timing = !mode.no_timing;
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        const Step *const step = &STEPS[i];
        if (step->family != 0u && (families & step->family) == 0u) {
            continue;
        }
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
        ReportRequireStarts(step->asks);
        if (step->timed) {
            RunTimed(step->name, step->run, timing);
        } else {
            step->run();
        }
        ReportRequireStarts(false);
        JournalSimple(JOURNAL_DONE, step->name, 0);
    }
    {
        // A table entry this launch's suites never reached names a case this build lacks, and
        // would protect nothing.
        char unreached[REPORT_NOTE_MAX - 96];
        if (ReportMayCrashUnreached(unreached, sizeof(unreached)) > 0u) {
            ReportNote("may crash: the table names %s, which the suites that ran never reached",
                       unreached);
        }
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
        ReportAppend(REPORT_PATH);
        ReportNote("detailed mode: nothing pressed for %u seconds after the report closed, "
                   "left by itself",
                   (unsigned)(UI_UNATTENDED_NANOSECONDS / 1000000000ull));
        ReportClose();
    }

    return ReportFailed() == 0 ? 0 : 1;
}
