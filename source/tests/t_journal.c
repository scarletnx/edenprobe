// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Before any header: the host regression below uses fopencookie, which glibc hides
// without this. The console build defines __SWITCH__ and has no such function.
#ifndef __SWITCH__
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef EDENPROBE_HOST
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../bans.h"
#include "../check.h"
#include "../cpuref.h"
#include "../hang.h"
#include "../journal.h"
#include "../mode.h"
#include "../report.h"
#ifdef EDENPROBE_HOST
#include "../runner.h"
#endif
#include "tests.h"

// The journal, the crash list and what a launch decides from them, checked without a crash: the
// records as they are written and read back, a last line cut short, where a run stopped, and
// whether a case runs. Support cases, so they run in both builds, and the file cases write into
// the support suite's own corner and take their file away again.

static const char *const SUITE = "support";

static JournalRecord Record(JournalKind kind, const char *suite, const char *name,
                            uint32_t number) {
    JournalRecord r;
    memset(&r, 0, sizeof(r));
    r.kind = kind;
    snprintf(r.suite, sizeof(r.suite), "%s", suite != NULL ? suite : "");
    snprintf(r.name, sizeof(r.name), "%s", name != NULL ? name : "");
    r.number = number;
    return r;
}

static bool SameRecord(const JournalRecord *a, const JournalRecord *b) {
    return a->kind == b->kind && strcmp(a->suite, b->suite) == 0 &&
           strcmp(a->name, b->name) == 0 && strcmp(a->mode, b->mode) == 0 &&
           a->number == b->number && a->result == b->result && a->offset == b->offset &&
           a->limit == b->limit && a->quiet == b->quiet &&
           memcmp(&a->counts, &b->counts, sizeof(a->counts)) == 0;
}

static void CheckRecords(void) {
    JournalRecord records[13];
    records[0] = Record(JOURNAL_RUN, "0.2.0", "v0.2.0-20-g2ddafce-dirty", 0);
    snprintf(records[0].mode, sizeof(records[0].mode), "fatal graphics retry");
    records[9] = Record(JOURNAL_CHOSEN, NULL, NULL, 0);
    snprintf(records[9].mode, sizeof(records[9].mode), "fatal retry");
    records[10] = Record(JOURNAL_STOPPED, NULL, NULL, 0);
    records[11] = Record(JOURNAL_TIMEOUT, NULL, NULL, 0);
    records[11].limit = 60u;
    records[11].quiet = 58u;
    records[12] = Record(JOURNAL_TIMED_OUT, "tex8k", "a 256x256 region of the 8192x8192 BC7 "
                                                     "texture copied out to a buffer holds its "
                                                     "bytes",
                         0);
    records[12].limit = 60u;
    records[12].quiet = 58u;
    records[1] = Record(JOURNAL_LAUNCH, NULL, NULL, 3);
    records[2] = Record(JOURNAL_STEP, "index", NULL, 0);
    records[2].offset = 84504u;
    records[2].counts = (JournalCounts){ { 507u, 1u, 29u, 2u } };
    records[3] = Record(JOURNAL_PASS, "index", NULL, 7);
    records[4] = Record(JOURNAL_START, "index", "an indexed draw with the library's open limit "
                                                "uses every index",
                        7);
    records[5] = Record(JOURNAL_END, "nifm", "a request nobody submitted says it was not "
                                             "submitted",
                        0);
    records[5].result = JOURNAL_CRASHED;
    records[6] = Record(JOURNAL_CRASH, "cpu", "sm4ekey and sm4e give the SM4 standard's worked "
                                              "example",
                        0);
    records[7] = Record(JOURNAL_DONE, "net", NULL, 0);
    records[8] = Record(JOURNAL_FINISHED, NULL, NULL, 0);
    records[8].counts = (JournalCounts){ { 600u, 0u, 30u, 1u } };
    uint32_t wrong = 0;
    char first_wrong[JOURNAL_LINE_MAX] = "";
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        char line[JOURNAL_LINE_MAX];
        JournalRecord back;
        const size_t length = JournalFormat(line, sizeof(line), &records[i]);
        if (length == 0u || line[length - 1u] != '\n' || !JournalParse(line, &back) ||
            !SameRecord(&records[i], &back)) {
            if (wrong == 0u) {
                snprintf(first_wrong, sizeof(first_wrong), "%s", line);
            }
            ++wrong;
        }
    }
    char start[JOURNAL_LINE_MAX];
    JournalFormat(start, sizeof(start), &records[4]);
    char timeout[JOURNAL_LINE_MAX];
    JournalFormat(timeout, sizeof(timeout), &records[11]);
    char timed_out[JOURNAL_LINE_MAX];
    JournalFormat(timed_out, sizeof(timed_out), &records[12]);
    CHECK_TRUE(SUITE, "every journal record reads back as it was written", CHECK_NONE,
               wrong == 0u &&
                   strcmp(start, "start index 7 an indexed draw with the library's open limit "
                                 "uses every index\t\n") == 0 &&
                   strcmp(timeout, "timeout 60 58\t\n") == 0 &&
                   strcmp(timed_out, "timedout tex8k 0 60 58 a 256x256 region of the 8192x8192 "
                                     "BC7 texture copied out to a buffer holds its bytes\t\n") ==
                       0,
               "%u of 13 records came back different, the first \"%.120s\"; a start reads "
               "\"%.120s\", a timeout \"%.40s\", a timed out case \"%.60s\"",
               (unsigned)wrong, first_wrong, start, timeout, timed_out);

    // Half a record would read as another one, so a buffer too small for it gets nothing; and a
    // name with a newline in it cannot end its line early.
    char small[24];
    memset(small, 'x', sizeof(small));
    const size_t cut = JournalFormat(small, sizeof(small), &records[4]);
    JournalRecord broken = Record(JOURNAL_START, "clear", "two\nlines", 0);
    char joined[JOURNAL_LINE_MAX];
    JournalFormat(joined, sizeof(joined), &broken);
    JournalRecord spaced = Record(JOURNAL_START, "two words", "a case", 0);
    char refused[JOURNAL_LINE_MAX];
    const size_t spaced_length = JournalFormat(refused, sizeof(refused), &spaced);
    CHECK_TRUE(SUITE, "a journal record too long for its buffer is not written in part",
               CHECK_NONE,
               cut == 0u && small[0] == '\0' &&
                   strcmp(joined, "start clear 0 two lines\t\n") == 0 &&
                   spaced_length == 0u && refused[0] == '\0',
               "cut %u \"%.20s\"; a newline in a name gave \"%.40s\"; a suite with a space %u",
               (unsigned)cut, small, joined, (unsigned)spaced_length);

    static const char *const NOT_RECORDS[] = {
        "",
        "\n",
        "start",
        "start clear",
        "start clear 1",
        "start clear x a case",
        "start clear -1 a case",
        "end clear 1 passed a case",
        "end clear 1 pass",
        "step clear 12 1 2 3",
        "step clear 12 1 2 3 4 5",
        "launch",
        "launch 1 2",
        "finished 1 2 3",
        "done",
        "done a b",
        "chosen",
        "stopped now",
        "timeout",
        "timeout 60",
        "timeout 60 58 more",
        "timeout sixty 58",
        "timedout clear 1 60 a case",
        "timedout clear 1 60 58",
        "begin clear 1 a case",
        "pass clear 99999999999",
        "start clear 1 a\tcase",
        "start clear 1 a case\t\t",
        "start clear 1 a case\t \n",
        "\t",
        "START clear 1 a case",
        " start clear 1 a case",
    };
    uint32_t read_as_records = 0;
    const char *first_read = "";
    for (size_t i = 0; i < sizeof(NOT_RECORDS) / sizeof(NOT_RECORDS[0]); ++i) {
        JournalRecord r;
        if (JournalParse(NOT_RECORDS[i], &r)) {
            if (read_as_records == 0u) {
                first_read = NOT_RECORDS[i];
            }
            ++read_as_records;
        }
    }
    CHECK_TRUE(SUITE, "a line that is not a whole journal record is not read as one", CHECK_NONE,
               read_as_records == 0u, "%u of %u were read as records, the first \"%s\"",
               (unsigned)read_as_records,
               (unsigned)(sizeof(NOT_RECORDS) / sizeof(NOT_RECORDS[0])), first_read);
}

// A run as the journal of a launch that died in the middle of it leaves it.
static const char RUN_HEAD[] = "run 0.2.0 v0.2.0-20-g2ddafce fatal\n"
                               "launch 1\n"
                               "step selfcheck 180 0 0 0 0\n"
                               "end selfcheck 0 pass integer arithmetic\n"
                               "done selfcheck\n"
                               "step cpu 300 1 0 0 0\n"
                               "start cpu 0 clz counts each of sixteen 8-bit lanes\n"
                               "end cpu 0 pass clz counts each of sixteen 8-bit lanes\n"
                               "start cpu 0 sm4ekey and sm4e give the SM4 standard's worked "
                               "example\n";

static void CheckReading(void) {
    // The program ended inside the SM4 case: the last record is its start.
    JournalState state;
    JournalStateInit(&state);
    JournalReadText(&state, RUN_HEAD, strlen(RUN_HEAD));
    const bool inside = state.has_run && !state.finished && state.in_step && state.in_case &&
                        strcmp(state.step.suite, "cpu") == 0 &&
                        strcmp(state.started.name,
                               "sm4ekey and sm4e give the SM4 standard's worked example") == 0 &&
                        JournalStepDone(&state, "selfcheck") && !JournalStepDone(&state, "cpu") &&
                        state.counts.counts[JOURNAL_PASSED] == 2u && state.launches == 1u;

    // A last line with no newline is a write the program did not live to finish: here the start
    // of a case that would otherwise be taken for the one it ended inside.
    static const char TORN[] = "start cpu 0 frintx rounds to nearest even";
    char text[sizeof(RUN_HEAD) + 200];
    snprintf(text, sizeof(text), "%send cpu 0 skip sm4ekey and sm4e give the SM4 standard's "
                                 "worked example\n%s",
             RUN_HEAD, TORN);
    JournalState torn;
    JournalStateInit(&torn);
    JournalReadText(&torn, text, strlen(text));
    // The same line complete is a start like any other.
    char whole[sizeof(text) + 2];
    snprintf(whole, sizeof(whole), "%s\n", text);
    JournalState complete;
    JournalStateInit(&complete);
    JournalReadText(&complete, whole, strlen(whole));
    CHECK_TRUE(SUITE, "a journal's last line with no newline is left unread", CHECK_NONE,
               inside && torn.torn && !torn.in_case && torn.in_step &&
                   torn.counts.counts[JOURNAL_SKIPPED] == 1u && complete.in_case &&
                   !complete.torn &&
                   strcmp(complete.started.name, "frintx rounds to nearest even") == 0,
               "whole journal: in a case %d, in step %d, done %u; torn: torn %d, in a case %d; "
               "completed: in a case %d",
               (int)state.in_case, (int)state.in_step, (unsigned)state.done_count,
               (int)torn.torn, (int)torn.in_case, (int)complete.in_case);

    // Eden keeps a file's last write in a host buffer and loses it in an abort, and the newline
    // is the last write of every record: the start is all there, ending in its tab, and that
    // is the case the program ended inside. A start cut inside its body has no tab, so it stays
    // out, as does one a build before the tab wrote.
    static const char ABORTED[] = "start cpu 0 frintx rounds to nearest even\t";
    static const char CUT[] = "start cpu 0 frintx rounds to ne";
    char aborted_text[sizeof(RUN_HEAD) + 200];
    snprintf(aborted_text, sizeof(aborted_text),
             "%send cpu 0 skip sm4ekey and sm4e give the SM4 standard's worked example\t\n%s",
             RUN_HEAD, ABORTED);
    JournalState aborted;
    JournalStateInit(&aborted);
    JournalReadText(&aborted, aborted_text, strlen(aborted_text));
    char cut_text[sizeof(RUN_HEAD) + 200];
    snprintf(cut_text, sizeof(cut_text),
             "%send cpu 0 skip sm4ekey and sm4e give the SM4 standard's worked example\t\n%s",
             RUN_HEAD, CUT);
    JournalState cut;
    JournalStateInit(&cut);
    JournalReadText(&cut, cut_text, strlen(cut_text));
    const JournalPlan aborted_plan = JournalPlanStart(&aborted, "0.2.0", "v0.2.0-20-g2ddafce",
                                                      "fatal", 100000);
    CHECK_TRUE(SUITE, "a start whose newline an abort took is the case the program ended inside",
               CHECK_NONE,
               aborted.in_case && !aborted.torn &&
                   strcmp(aborted.started.name, "frintx rounds to nearest even") == 0 &&
                   aborted_plan.crashed &&
                   strcmp(aborted_plan.crash.name, "frintx rounds to nearest even") == 0 &&
                   !cut.in_case && cut.torn && torn.torn && !torn.in_case,
               "aborted: in a case %d, torn %d, name \"%s\", crashed %d; cut: in a case %d, "
               "torn %d; with no tab: in a case %d",
               (int)aborted.in_case, (int)aborted.torn, aborted.started.name,
               (int)aborted_plan.crashed, (int)cut.in_case, (int)cut.torn, (int)torn.in_case);

    // Where each launch stopped, told apart: inside a case, after one inside a step, between
    // steps, and a run that finished.
    static const char AFTER[] = "run 0.2.0 b q\nlaunch 1\nstep clear 10 0 0 0 0\n"
                                "start clear 1 a\nend clear 1 muted a\n";
    static const char BETWEEN[] = "run 0.2.0 b q\nlaunch 1\nstep clear 10 0 0 0 0\n"
                                  "done clear\n";
    static const char FINISHED[] = "run 0.2.0 b q\nlaunch 1\nstep clear 10 0 0 0 0\n"
                                   "done clear\nfinished 5 0 0 0\n";
    // A step run again after a crash inside it is not done until its new done record, and the
    // case the launch before ended inside is remembered as crashed in this run.
    static const char AGAIN[] = "run 0.2.0 b q\nchosen fatal\nlaunch 1\nstep clear 10 0 0 0 0\n"
                                "done clear\n"
                                "step fill 20 1 0 0 0\nstart fill 1 x\nlaunch 2\n"
                                "crashed fill 1 x\nstep fill 20 1 0 0 0\nend fill 1 muted y\n";
    // A launch a runner stopped on purpose: its last start is no crash, and the run goes on after
    // the step it was in.
    static const char STOPPED[] = "run 0.2.0 b q\nlaunch 1\nstep clear 10 0 0 0 0\n"
                                  "start clear 1 a\nstopped\n";
    JournalState stopped;
    JournalStateInit(&stopped);
    JournalReadText(&stopped, STOPPED, strlen(STOPPED));
    const JournalPlan stopped_plan = JournalPlanStart(&stopped, "0.2.0", "b", "q", 100);
    JournalState after, between, finished, again;
    JournalStateInit(&after);
    JournalStateInit(&between);
    JournalStateInit(&finished);
    JournalStateInit(&again);
    JournalReadText(&after, AFTER, strlen(AFTER));
    JournalReadText(&between, BETWEEN, strlen(BETWEEN));
    JournalReadText(&finished, FINISHED, strlen(FINISHED));
    JournalReadText(&again, AGAIN, strlen(AGAIN));
    const bool told_apart =
        !after.in_case && after.in_step && strcmp(after.last_ended, "a") == 0 &&
        !between.in_step && !between.in_case && JournalStepDone(&between, "clear") &&
        finished.finished && !finished.in_step && finished.counts.counts[JOURNAL_PASSED] == 5u &&
        again.launches == 2u && again.in_step && !JournalStepDone(&again, "fill") &&
        !again.in_case && again.crash_count == 1u && strcmp(again.crashes[0].name, "x") == 0 &&
        strcmp(again.chosen, "fatal") == 0 && !stopped.in_case && stopped.in_step &&
        !stopped_plan.crashed && stopped_plan.start == JOURNAL_RESUME_STEP;

    // The note before the summary names the cases this run ended inside, and only those. A new
    // run reads the run before's journal, whose crashes it did not have: every case it skipped
    // as crashed came from the crash list.
    static const char ENDED_BEFORE[] = "run 0.2.0 b q\nlaunch 1\nlaunch 2\n"
                                       "crashed bcn 1 a case\nfinished 1 0 0 1\n";
    JournalState before;
    JournalStateInit(&before);
    JournalReadText(&before, ENDED_BEFORE, strlen(ENDED_BEFORE));
    const JournalPlan anew = JournalPlanStart(&before, "0.2.0", "b", "q", 100);
    const JournalPlan on_case = JournalPlanStart(&state, "0.2.0", "v0.2.0-20-g2ddafce", "fatal",
                                                 1000);
    const JournalPlan on_step = JournalPlanStart(&again, "0.2.0", "b", "q", 100);
    char run_new[REPORT_NOTE_MAX + 1];
    char run_case[REPORT_NOTE_MAX + 1];
    char run_step[REPORT_NOTE_MAX + 1];
    char run_plain[16];
    JournalDescribeRun(run_new, sizeof(run_new), &anew, &before, 3u);
    JournalDescribeRun(run_case, sizeof(run_case), &on_case, &state, 2u);
    JournalDescribeRun(run_step, sizeof(run_step), &on_step, &again, 1u);
    const size_t plain = JournalDescribeRun(run_plain, sizeof(run_plain), &anew, &before, 0u);
    const bool noted =
        anew.start == JOURNAL_FRESH && before.crash_count == 1u &&
        strcmp(run_new, "journal: this run took 1 launch; 3 cases skipped as crashed, 3 of them "
                        "from the crash list") == 0 &&
        on_case.start == JOURNAL_RESUME_CASE &&
        strcmp(run_case, "journal: this run took 2 launches; 2 cases skipped as crashed, 1 of "
                         "them from the crash list; the program ended inside cpu \"sm4ekey and "
                         "sm4e give the SM4 standard's worked example\"") == 0 &&
        on_step.start == JOURNAL_RESUME_STEP &&
        strcmp(run_step, "journal: this run took 3 launches; 1 case skipped as crashed; the "
                         "program ended inside fill \"x\"") == 0 &&
        plain == 0u && run_plain[0] == '\0';
    CHECK_TRUE(SUITE, "the journal says which case the program ended inside, and no other",
               CHECK_NONE, told_apart && noted,
               "after a case: in a case %d, step %d; between: step %d, done %d; finished %d; "
               "again: launches %u, fill done %d, crashes %u; the notes \"%.80s\", \"%.160s\", "
               "\"%.80s\"",
               (int)after.in_case, (int)after.in_step, (int)between.in_step,
               (int)JournalStepDone(&between, "clear"), (int)finished.finished,
               (unsigned)again.launches, (int)JournalStepDone(&again, "fill"),
               (unsigned)again.crash_count, run_new, run_case, run_step);

    // The counts a run had where it stopped: those its step began with, and every counted end
    // since. A rehearsal's end counts for nothing, a crashed skip apart from the rest.
    static const char COUNTED[] = "run 0.2.0 b q\nlaunch 1\nstep net 900 10 2 3 1\n"
                                  "end nifm 0 pass a\nend nifm 0 fail b\nend nifm 0 skip c\n"
                                  "end nifm 0 crashed d\nend nifm 3 muted e\n";
    JournalState counted;
    JournalStateInit(&counted);
    JournalReadText(&counted, COUNTED, strlen(COUNTED));
    const JournalCounts want = { { 11u, 3u, 4u, 2u } };
    CHECK_TRUE(SUITE, "a run's counts come back from the journal where it stopped", CHECK_NONE,
               memcmp(&counted.counts, &want, sizeof(want)) == 0 &&
                   counted.step.counts.counts[JOURNAL_PASSED] == 10u,
               "counts %u %u %u %u, want 11 3 4 2", (unsigned)counted.counts.counts[0],
               (unsigned)counted.counts.counts[1], (unsigned)counted.counts.counts[2],
               (unsigned)counted.counts.counts[3]);
}

/// A launch a runner stopped at its time limit: the case under way is skipped by the launch after
/// as timeout, with the limit and how long the case had run, never as crashed, and the note
/// before the summary keeps the two apart.
static void CheckTimeout(void) {
    static const char INSIDE[] = "run 0.2.0 b q\nlaunch 1\nstep tex8k 10 4 0 0 0\n"
                                 "start tex8k 0 a copy\ntimeout 60 58\t\n";
    static const char OUTSIDE[] = "run 0.2.0 b q\nlaunch 1\nstep tex8k 10 4 0 0 0\n"
                                  "start tex8k 0 a copy\nend tex8k 0 pass a copy\n"
                                  "timeout 60 3\n";
    // The newline of the start lost, as Eden loses it, and the runner's record on a line of its
    // own after it.
    static const char TORN[] = "run 0.2.0 b q\nlaunch 1\nstep tex8k 10 4 0 0 0\n"
                               "start tex8k 0 a copy\t\ntimeout 5 5\t\n";
    // The launches after: one that went on and ended inside another case, then one more.
    static const char LATER[] = "run 0.2.0 b q\nlaunch 1\nstep tex8k 10 4 0 0 0\n"
                                "start tex8k 0 a copy\ntimeout 60 58\nlaunch 2\n"
                                "timedout tex8k 0 60 58 a copy\nstep tex8k 10 4 0 0 0\n"
                                "end tex8k 0 skip a copy\nstart tex8k 0 a draw\nlaunch 3\n"
                                "crashed tex8k 0 a draw\nstep tex8k 10 4 0 0 0\n"
                                "end tex8k 0 skip a copy\nend tex8k 0 crashed a draw\n";
    JournalState inside, outside, torn, later;
    JournalStateInit(&inside);
    JournalStateInit(&outside);
    JournalStateInit(&torn);
    JournalStateInit(&later);
    JournalReadText(&inside, INSIDE, strlen(INSIDE));
    JournalReadText(&outside, OUTSIDE, strlen(OUTSIDE));
    JournalReadText(&torn, TORN, strlen(TORN));
    JournalReadText(&later, LATER, strlen(LATER));
    const JournalPlan plan = JournalPlanStart(&inside, "0.2.0", "b", "q", 100);
    const JournalPlan stop = JournalPlanStart(&outside, "0.2.0", "b", "q", 100);
    const JournalPlan torn_plan = JournalPlanStart(&torn, "0.2.0", "b", "q", 100);
    const JournalPlan other = JournalPlanStart(&inside, "0.2.0", "another", "q", 100);
    const JournalPlan on = JournalPlanStart(&later, "0.2.0", "b", "q", 100);
    const bool read_right =
        inside.in_case && inside.timed_out && plan.start == JOURNAL_RESUME_CASE && plan.crashed &&
        plan.timed_out && plan.crash.kind == JOURNAL_TIMED_OUT && plan.crash.limit == 60u &&
        plan.crash.quiet == 58u && strcmp(plan.crash.name, "a copy") == 0 &&
        // Outside any case it reads as a stop: the step keeps what it reported.
        !outside.in_case && !outside.timed_out && !stop.crashed && !stop.timed_out &&
        stop.start == JOURNAL_RESUME_STEP &&
        torn.in_case && torn.timed_out && torn_plan.timed_out && torn_plan.crash.limit == 5u &&
        // The launches after tell the two apart by their records.
        later.crash_count == 2u && later.crashes[0].kind == JOURNAL_TIMED_OUT &&
        later.crashes[0].limit == 60u && later.crashes[0].quiet == 58u &&
        later.crashes[1].kind == JOURNAL_CRASH && !later.in_case && !later.timed_out &&
        on.start == JOURNAL_RESUME_STEP && !on.timed_out;

    char resumed[REPORT_NOTE_MAX + 1];
    char anew[REPORT_NOTE_MAX + 1];
    char asked[JOURNAL_SUITE_MAX + JOURNAL_NAME_MAX + 160u];
    char run[REPORT_NOTE_MAX + 1];
    JournalDescribePlan(resumed, sizeof(resumed), &plan, &inside);
    JournalDescribePlan(anew, sizeof(anew), &other, &inside);
    JournalDescribeStop(asked, sizeof(asked), &plan, &inside);
    JournalDescribeRun(run, sizeof(run), &on, &later, 1u);
    const bool said_right =
        strcmp(resumed, "journal: launch 2 resumes the run; a runner stopped the program at its "
                        "limit of 60 s inside tex8k case \"a copy\", 58 s after the journal's "
                        "last record: that case is skipped as timeout and stays off the crash "
                        "list, and step tex8k runs again from its start") == 0 &&
        strcmp(anew, "journal: a new run (the journal is another build's); before it, a runner "
                     "stopped the program at its limit of 60 s inside tex8k case \"a copy\", 58 "
                     "s after the journal's last record, which stays off the crash list and runs "
                     "again") == 0 &&
        strcmp(asked, "a runner stopped the program at its limit of 60 s inside tex8k case \"a "
                      "copy\", 58 s after the journal's last record") == 0 &&
        strcmp(run, "journal: this run took 4 launches; 1 case skipped as crashed; the program "
                    "ended inside tex8k \"a draw\"; 1 case skipped as timeout, a runner having "
                    "stopped the program at its limit inside tex8k \"a copy\"") == 0;
    CHECK_TRUE(SUITE, "a launch a runner stopped at its limit leaves a timeout, not a crash",
               CHECK_NONE, read_right && said_right,
               "read %s (inside: in a case %d, timed out %d, plan %d crashed %d timed out %d "
               "limit %u quiet %u; outside: plan %d; later: %u records); said \"%.90s\"; "
               "\"%.60s\"; \"%.90s\"",
               read_right ? "right" : "wrong", (int)inside.in_case, (int)inside.timed_out,
               (int)plan.start, (int)plan.crashed, (int)plan.timed_out,
               (unsigned)plan.crash.limit, (unsigned)plan.crash.quiet, (int)stop.start,
               (unsigned)later.crash_count, resumed, anew, run);

    // What the launch after decides for the case: skipped as timeout with both numbers, retry or
    // not, and counted with the skips. The run's own settings and crashes are handed back, its
    // skip= suites among them: a run with skip=tex8k left "a draw" out as asked.
    const uint32_t crashes = ReportCrashedMark();
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    char run_skips[REPORT_SKIP_SUITES_MAX][JOURNAL_SUITE_MAX];
    const char *run_skip[REPORT_SKIP_SUITES_MAX];
    const size_t run_skip_count = ReportSkipSuiteCount();
    for (size_t i = 0; i < run_skip_count && i < REPORT_SKIP_SUITES_MAX; ++i) {
        snprintf(run_skips[i], sizeof(run_skips[i]), "%s", ReportSkipSuite(i));
        run_skip[i] = run_skips[i];
    }
    ReportSetSkipSuites(NULL, 0u);
    ReportSetCrashList(NULL, NULL, true);
    ReportAddCrashed(&plan.crash);
    ReportReason reason = REPORT_REASON_CRASHED;
    char detail[REPORT_KEPT_DETAIL];
    const bool skipped = !ReportWouldRun("tex8k", "a copy", &reason, detail, sizeof(detail));
    const bool next_runs = ReportWouldRun("tex8k", "a draw", NULL, NULL, 0);
    ReportCrashedRewind(crashes);
    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetSkipSuites(run_skip, run_skip_count);
    CHECK_TRUE(SUITE, "a case a runner stopped the program inside is skipped as timeout, with "
                      "the limit and how long it ran",
               CHECK_NONE,
               skipped && next_runs && reason == REPORT_REASON_TIMEOUT &&
                   strcmp(ReportReasonWord(REPORT_REASON_TIMEOUT), "timeout") == 0 &&
                   strcmp(detail, "a runner stopped the program inside it earlier in this run, at "
                                  "the launch's limit of 60 s and 58 s after the journal's last "
                                  "record, which is how long the case had run; it is not on the "
                                  "crash list, so a later run starts it again") == 0 &&
                   ReportCrashedMark() == crashes,
               "skipped %d as %s, the next case runs %d; detail \"%.280s\"", (int)skipped,
               ReportReasonWord(reason), (int)next_runs, detail);
}

static void CheckPlans(void) {
    JournalState empty;
    JournalStateInit(&empty);
    JournalState inside;
    JournalStateInit(&inside);
    JournalReadText(&inside, RUN_HEAD, strlen(RUN_HEAD));
    static const char STEPPED[] = "run 0.2.0 v0.2.0-20-g2ddafce fatal\nlaunch 1\n"
                                  "step clear 400 3 0 0 0\nstart clear 1 a\nend clear 1 muted a\n";
    JournalState stepped;
    JournalStateInit(&stepped);
    JournalReadText(&stepped, STEPPED, strlen(STEPPED));
    static const char BETWEEN[] = "run 0.2.0 v0.2.0-20-g2ddafce fatal\nlaunch 1\nlaunch 2\n"
                                  "step clear 400 3 0 0 0\ndone clear\n";
    JournalState between;
    JournalStateInit(&between);
    JournalReadText(&between, BETWEEN, strlen(BETWEEN));
    static const char FINISHED[] = "run 0.2.0 v0.2.0-20-g2ddafce fatal\nlaunch 1\n"
                                   "finished 1 0 0 0\n";
    JournalState finished;
    JournalStateInit(&finished);
    JournalReadText(&finished, FINISHED, strlen(FINISHED));

    const char *const version = "0.2.0";
    const char *const build = "v0.2.0-20-g2ddafce";
    const JournalPlan none = JournalPlanStart(&empty, version, build, "fatal", -1);
    const JournalPlan done = JournalPlanStart(&finished, version, build, "fatal", 1000);
    const JournalPlan case_ = JournalPlanStart(&inside, version, build, "fatal", 1000);
    const JournalPlan other_build = JournalPlanStart(&inside, version, "v0.2.0-21-gabc", "fatal",
                                                     1000);
    const JournalPlan other_mode = JournalPlanStart(&inside, version, build, "", 1000);
    const JournalPlan gone = JournalPlanStart(&inside, version, build, "fatal", -1);
    const JournalPlan shorter = JournalPlanStart(&inside, version, build, "fatal", 299);
    const JournalPlan step = JournalPlanStart(&stepped, version, build, "fatal", 1000);
    const JournalPlan after = JournalPlanStart(&between, version, build, "fatal", 1000);
    const bool fresh_right = none.start == JOURNAL_FRESH && !none.crashed &&
                             none.why_new == JOURNAL_NEW_NO_JOURNAL &&
                             done.start == JOURNAL_FRESH && done.why_new == JOURNAL_NEW_FINISHED &&
                             !done.crashed && other_build.start == JOURNAL_FRESH &&
                             other_build.why_new == JOURNAL_NEW_OTHER_BUILD &&
                             other_build.crashed && other_mode.start == JOURNAL_FRESH &&
                             other_mode.why_new == JOURNAL_NEW_OTHER_MODE &&
                             gone.why_new == JOURNAL_NEW_NO_REPORT &&
                             shorter.why_new == JOURNAL_NEW_NO_REPORT && shorter.crashed;
    const bool resume_right =
        case_.start == JOURNAL_RESUME_CASE && case_.crashed && case_.launch == 2u &&
        case_.offset == 300u && case_.counts.counts[JOURNAL_PASSED] == 1u &&
        strcmp(case_.crash.suite, "cpu") == 0 && step.start == JOURNAL_RESUME_STEP &&
        !step.crashed && step.offset == 1000u && after.start == JOURNAL_RESUME_BETWEEN &&
        after.launch == 3u && after.offset == 1000u;
    char said[REPORT_NOTE_MAX + 1];
    JournalDescribePlan(said, sizeof(said), &case_, &inside);
    CHECK_TRUE(SUITE, "a launch goes on with a run only for the same build, the same words and its "
                      "report",
               CHECK_NONE,
               fresh_right && resume_right &&
                   strstr(said, "the program ended inside cpu case \"sm4ekey") != NULL,
               "new runs %s; resumes %s (case %d at %llu, step %d, between %d launch %u); said "
               "\"%.160s\"",
               fresh_right ? "right" : "wrong", resume_right ? "right" : "wrong",
               (int)case_.start, (unsigned long long)case_.offset, (int)step.start,
               (int)after.start, (unsigned)after.launch, said);
}

/// A launch that could go on asks first only in a detailed run; starting over makes the plan a
/// new run's and keeps the case the program ended inside for the crash list; and the screen and
/// the report say where it stopped and why the run is new.
static void CheckAsking(void) {
    JournalState inside;
    JournalStateInit(&inside);
    JournalReadText(&inside, RUN_HEAD, strlen(RUN_HEAD));
    static const char STEPPED[] = "run 0.2.0 v0.2.0-20-g2ddafce fatal\nlaunch 1\n"
                                  "step clear 400 3 0 0 0\nstart clear 1 a\nend clear 1 muted a\n";
    JournalState stepped;
    JournalStateInit(&stepped);
    JournalReadText(&stepped, STEPPED, strlen(STEPPED));
    static const char BETWEEN[] = "run 0.2.0 v0.2.0-20-g2ddafce fatal\nlaunch 1\n"
                                  "step clear 400 3 0 0 0\ndone clear\n";
    JournalState between;
    JournalStateInit(&between);
    JournalReadText(&between, BETWEEN, strlen(BETWEEN));
    const char *const version = "0.2.0";
    const char *const build = "v0.2.0-20-g2ddafce";
    const JournalPlan case_ = JournalPlanStart(&inside, version, build, "fatal", 1000);
    const JournalPlan step = JournalPlanStart(&stepped, version, build, "fatal", 1000);
    const JournalPlan after = JournalPlanStart(&between, version, build, "fatal", 1000);
    const JournalPlan fresh = JournalPlanStart(&inside, version, build, "", 1000);

    const bool asks = JournalPlanAsks(&case_, true) && !JournalPlanAsks(&case_, false) &&
                      JournalPlanAsks(&step, true) && JournalPlanAsks(&after, true) &&
                      !JournalPlanAsks(&fresh, true) && !JournalPlanAsks(NULL, true);

    JournalPlan over = case_;
    JournalPlanRestart(&over);
    const bool restarted = over.start == JOURNAL_FRESH && over.why_new == JOURNAL_NEW_CHOSEN &&
                           over.launch == 1u && over.offset == 0u &&
                           over.counts.counts[JOURNAL_PASSED] == 0u && over.crashed &&
                           strcmp(over.crash.suite, "cpu") == 0 && !JournalPlanAsks(&over, true);
    JournalPlan left = fresh;
    JournalPlanRestart(&left);
    const bool fresh_kept = memcmp(&left, &fresh, sizeof(left)) == 0;

    char stop_case[256];
    char stop_step[256];
    char stop_between[256];
    char stop_fresh[64] = "x";
    char said[REPORT_NOTE_MAX + 1];
    JournalDescribeStop(stop_case, sizeof(stop_case), &case_, &inside);
    JournalDescribeStop(stop_step, sizeof(stop_step), &step, &stepped);
    JournalDescribeStop(stop_between, sizeof(stop_between), &after, &between);
    JournalDescribeStop(stop_fresh, sizeof(stop_fresh), &fresh, &inside);
    JournalDescribePlan(said, sizeof(said), &over, &inside);
    const bool stops =
        strcmp(stop_case, "the program ended inside cpu case \"sm4ekey and sm4e give the SM4 "
                          "standard's worked example\"") == 0 &&
        strcmp(stop_step, "the program ended in step clear after \"a\", outside any case") == 0 &&
        strcmp(stop_between, "the program ended between two steps, after 1 done") == 0 &&
        stop_fresh[0] == '\0' &&
        strncmp(said, "journal: a new run (the screen was asked to start over rather than go on); "
                      "before it, the program ended inside cpu case",
                strlen("journal: a new run (the screen was asked to start over rather than go "
                       "on); before it, the program ended inside cpu case")) == 0;
    CHECK_TRUE(SUITE, "a detailed launch asks before going on, and starting over keeps the crash",
               CHECK_NONE, asks && restarted && fresh_kept && stops,
               "asks %d, restarted %d, a new run left alone %d; said \"%.70s\" \"%.70s\" "
               "\"%.60s\"; report \"%.120s\"",
               (int)asks, (int)restarted, (int)fresh_kept, stop_case, stop_step, stop_between,
               said);
}

/// Appends @p record's line to @p text.
static void Append(char *text, size_t size, const JournalRecord *record) {
    char line[JOURNAL_LINE_MAX];
    JournalFormat(line, sizeof(line), record);
    const size_t used = strlen(text);
    snprintf(text + used, size - used, "%s", line);
}

/// A run the start screen chose, ended by a crash, is gone on with in the mode it chose: with the
/// choice kept in the journal when the mode file still says what it did, and with no chosen
/// record when the screen wrote the choice into the file. A choice that changed nothing writes
/// none, and one that turned every word off still writes one.
static void CheckChosenRun(void) {
    const char *const version = "0.3.0";
    const char *const build = "v0.3.0-4-gabcdef0";
    const RunMode file = ModeFromText("net");
    uint32_t wrong = 0;
    char said[160] = "";
    // Graphics and notime added and quick turned on, not saved and saved; quick alone, which
    // leaves the words as they were and still has to be kept.
    for (int which = 0; which < 3; ++which) {
        const int saved = which == 1;
        RunMode chosen = file;
        if (which < 2) {
            chosen.families_named = MODE_FAMILY_CPU | MODE_FAMILY_GRAPHICS | MODE_FAMILY_NET;
            chosen.no_timing = true;
        }
        chosen.quick = true;
        chosen.detailed = false;
        chosen.chosen_on_screen = true;
        chosen.saved_by_screen = saved != 0;
        char want[JOURNAL_MODE_MAX];
        ModeKey(want, sizeof(want), &chosen);
        char run_key[JOURNAL_MODE_MAX];
        char words[JOURNAL_MODE_MAX];
        ModeJournalWords(run_key, sizeof(run_key), words, sizeof(words), &file, &chosen);
        // The journal as a new run begins it, and as a launch that ends inside a case leaves it.
        char text[1024] = "";
        JournalRecord record;
        memset(&record, 0, sizeof(record));
        record.kind = JOURNAL_RUN;
        snprintf(record.suite, sizeof(record.suite), "%s", version);
        snprintf(record.name, sizeof(record.name), "%s", build);
        snprintf(record.mode, sizeof(record.mode), "%s", run_key);
        Append(text, sizeof(text), &record);
        if (words[0] != '\0') {
            memset(&record, 0, sizeof(record));
            record.kind = JOURNAL_CHOSEN;
            snprintf(record.mode, sizeof(record.mode), "%s", words);
            Append(text, sizeof(text), &record);
        }
        static const char REST[] = "launch 1\nstep clear 400 0 0 0 0\nstart clear 1 a case\n";
        snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s", REST);
        JournalState state;
        JournalStateInit(&state);
        JournalReadText(&state, text, strlen(text));
        // The next launch reads the mode file as the screen left it.
        char file_words[256];
        ModeFormat(file_words, sizeof(file_words), &chosen);
        const RunMode next_file = saved != 0 ? ModeFromText(file_words) : file;
        char next_key[JOURNAL_MODE_MAX];
        ModeKey(next_key, sizeof(next_key), &next_file);
        const JournalPlan plan = JournalPlanStart(&state, version, build, next_key, 100000);
        const RunMode resumed = ModeWithChosen(next_file, state.chosen);
        char got[JOURNAL_MODE_MAX];
        ModeKey(got, sizeof(got), &resumed);
        const bool right = plan.start == JOURNAL_RESUME_CASE && strcmp(got, want) == 0 &&
                           resumed.quick && !resumed.detailed &&
                           (saved != 0 ? words[0] == '\0' : words[0] != '\0');
        if (!right && wrong++ == 0u) {
            snprintf(said, sizeof(said), "choice %d: plan %d, key \"%.40s\", chosen \"%.40s\"",
                     which, (int)plan.start, got, words);
        }
    }
    char run_key[JOURNAL_MODE_MAX];
    char words[JOURNAL_MODE_MAX] = "x";
    ModeJournalWords(run_key, sizeof(run_key), words, sizeof(words), &file, &file);
    const bool unchanged = strcmp(run_key, "net") == 0 && words[0] == '\0';
    const RunMode fatal_file = ModeFromText("fatal");
    RunMode unfatal = fatal_file;
    unfatal.fatal = false;
    ModeJournalWords(run_key, sizeof(run_key), words, sizeof(words), &fatal_file, &unfatal);
    JournalRecord record;
    memset(&record, 0, sizeof(record));
    record.kind = JOURNAL_CHOSEN;
    snprintf(record.mode, sizeof(record.mode), "%s", words);
    char line[JOURNAL_LINE_MAX];
    const bool kept_empty = strcmp(run_key, "fatal") == 0 && strcmp(words, "screen") == 0 &&
                            JournalFormat(line, sizeof(line), &record) > 0u;
    CHECK_TRUE(SUITE, "a run the start screen chose goes on after a crash in the mode it chose",
               CHECK_NONE, wrong == 0u && unchanged && kept_empty,
               "%u of 3 wrong (%s); unchanged %d; every word off kept %d", (unsigned)wrong, said,
               (int)unchanged, (int)kept_empty);
}

static void CheckCrashList(void) {
    static const char TEXT[] = "# a comment\n"
                               "\n"
                               "bcn every layer of a six-layer BC4 array keeps its own levels\n"
                               "   \n"
                               "cpu sm4ekey and sm4e give the SM4 standard's worked example  \n"
                               "bcn every layer of a six-layer BC4 array keeps its own levels\n"
                               "lonely\n"
                               "  # an indented comment\n"
                               "geometry a geometry shader turns each of four points into its";
    CrashList list;
    CrashListInit(&list);
    CrashListParse(&list, TEXT, strlen(TEXT));
    const bool read_right =
        list.count == 2u && list.unreadable == 1u && list.torn &&
        CrashListHas(&list, "bcn", "every layer of a six-layer BC4 array keeps its own levels") &&
        CrashListHas(&list, "cpu", "sm4ekey and sm4e give the SM4 standard's worked example") &&
        !CrashListHas(&list, "geometry", "a geometry shader turns each of four points into its");
    CHECK_TRUE(SUITE, "the crash list reads the cases it names and nothing else", CHECK_NONE,
               read_right,
               "%u entries, %u unreadable, torn %d; the torn line %s", (unsigned)list.count,
               (unsigned)list.unreadable, (int)list.torn,
               CrashListHas(&list, "geometry",
                            "a geometry shader turns each of four points into its")
                   ? "was read as an entry"
                   : "was left out");

    static char text[CRASH_LIST_MAX * (JOURNAL_SUITE_MAX + JOURNAL_NAME_MAX + 2u) + 512u];
    const bool added = CrashListAdd(&list, "geometry", "a patch covers its rectangle") &&
                       !CrashListAdd(&list, "geometry", "a patch covers its rectangle") &&
                       !CrashListAdd(&list, "two words", "a case") &&
                       CrashListRemove(&list, "bcn", "every layer of a six-layer BC4 array keeps "
                                                     "its own levels") &&
                       !CrashListRemove(&list, "bcn", "not there");
    const size_t length = CrashListFormat(text, sizeof(text), &list);
    CrashList back;
    CrashListInit(&back);
    CrashListParse(&back, text, length);
    const bool same = back.count == list.count && back.unreadable == 0u && !back.torn &&
                      CrashListHas(&back, "geometry", "a patch covers its rectangle") &&
                      CrashListHas(&back, "cpu", "sm4ekey and sm4e give the SM4 standard's "
                                                 "worked example") &&
                      length > 0u && text[length - 1u] == '\n';
    char tiny[16];
    CHECK_TRUE(SUITE, "the crash list writes back what it holds", CHECK_NONE,
               added && same && CrashListFormat(tiny, sizeof(tiny), &list) == 0u &&
                   tiny[0] == '\0',
               "added and removed %s; %u entries back of %u, %u unreadable",
               added ? "as asked" : "wrongly", (unsigned)back.count, (unsigned)list.count,
               (unsigned)back.unreadable);
}

/// The launch after an abort ends the line the abort left open before it writes, so its first
/// record starts a line of its own; a journal whose last line is whole is left as it was.
static void CheckEndLine(const char *directory) {
    char path[400];
    snprintf(path, sizeof(path), "%.300s/journal-end-line", directory);
    static const char OPEN[] = "run 0.2.0 b fatal\t\nlaunch 1\t\nstep cpu 10 0 0 0 0\t\n"
                               "start cpu 0 frintx rounds to nearest even\t";
    FILE *file = fopen(path, "w");
    bool wrote = file != NULL && fwrite(OPEN, 1, sizeof(OPEN) - 1u, file) == sizeof(OPEN) - 1u;
    if (file != NULL) {
        fclose(file);
    }
    file = wrote ? fopen(path, "r+") : NULL;
    wrote = file != NULL && JournalEndLine(file) && JournalDurableAppend(file, "launch 2\t\n") &&
            JournalEndLine(file);
    const long after = file != NULL ? ftell(file) : -1;
    if (file != NULL) {
        fclose(file);
    }
    JournalState state;
    JournalStateInit(&state);
    const bool read_back = JournalReadPath(&state, path);
    char text[512];
    bool longer = false;
    const int64_t length = JournalReadFile(path, text, sizeof(text), &longer);
    const bool removed = remove(path) == 0;
    const bool joined = length > 0 && strstr(text, "even\tlaunch") != NULL;
    CHECK_TRUE(SUITE, "the launch after an abort ends the open line before its own records",
               CHECK_NONE,
               wrote && read_back && removed && !joined && state.launches == 2u &&
                   !state.in_case && !state.torn && state.unreadable == 0u &&
                   after == (long)(sizeof(OPEN) - 1u + 1u + 10u),
               "wrote %d, read %d, removed %d, joined %d, launches %u, in a case %d, torn %d, "
               "unreadable %u, at %ld",
               (int)wrote, (int)read_back, (int)removed, (int)joined, (unsigned)state.launches,
               (int)state.in_case, (int)state.torn, (unsigned)state.unreadable, after);
}

static void CheckDurableFile(const char *directory) {
    char path[400];
    snprintf(path, sizeof(path), "%.300s/journal-check", directory);
    FILE *file = fopen(path, "w");
    const bool wrote = file != NULL && JournalDurableAppend(file, "launch 1\n") &&
                       JournalDurableAppend(file, "start clear 1 a case\n") &&
                       !JournalDurableAppend(file, "no newline");
    const long end = file != NULL ? ftell(file) : -1;
    if (file != NULL) {
        fclose(file);
    }
    char back[128];
    bool longer = false;
    const int64_t length = JournalReadFile(path, back, sizeof(back), &longer);
    JournalState state;
    JournalStateInit(&state);
    const bool read_back = JournalReadPath(&state, path);
    const bool removed = remove(path) == 0;
    CHECK_TRUE(SUITE, "a line written to the card is whole in the file before the next starts",
               CHECK_NONE,
               wrote && end == 30 && length == 30 && !longer &&
                   strcmp(back, "launch 1\nstart clear 1 a case\n") == 0 && read_back &&
                   state.launches == 1u && state.in_case && removed &&
                   JournalFileSize(path) == -1,
               "written %d, at %ld; read back %lld bytes \"%.60s\"; journal in a case %d; "
               "removed %d",
               (int)wrote, end, (long long)length, back, (int)state.in_case, (int)removed);
}

/// Several lines, each longer than a word, through a real file. The two-line check above is
/// the contract; this one is the same write repeated, which is what a suite looks like.
static void CheckDurableLines(const char *directory) {
    char path[400];
    snprintf(path, sizeof(path), "%.300s/journal-lines", directory);
    FILE *file = fopen(path, "w");
    char expect[4096];
    size_t expect_len = 0;
    bool wrote = file != NULL;
    for (int i = 0; i < 8 && wrote; ++i) {
        char line[180];
        const int len = snprintf(line, sizeof(line),
                                 "start support %d a line long enough to leave the byte %02d\n",
                                 i, i);
        wrote = len > 0 && (size_t)len < sizeof(line) &&
                expect_len + (size_t)len < sizeof(expect) && JournalDurableAppend(file, line);
        if (wrote) {
            memcpy(expect + expect_len, line, (size_t)len);
            expect_len += (size_t)len;
        }
    }
    const long end = file != NULL ? ftell(file) : -1L;
    if (file != NULL) {
        fclose(file);
    }
    char back[4096];
    memset(back, 0, sizeof(back));
    bool longer = false;
    const int64_t length = JournalReadFile(path, back, sizeof(back), &longer);
    const bool removed = remove(path) == 0;
    CHECK_TRUE(SUITE, "several lines written forward are the file, with nothing missing between",
               CHECK_NONE,
               wrote && end == (long)expect_len && length == (int64_t)expect_len && !longer &&
                   memcmp(back, expect, expect_len) == 0 && removed,
               "written %d, at %ld against %u; read back %lld; removed %d", (int)wrote, end,
               (unsigned)expect_len, (long long)length, (int)removed);
}

#if !defined(__SWITCH__) && defined(__GLIBC__)
// The Android host library drops every buffered byte past a backward seek. This cookie does
// that and nothing else: a write only waits, a backward seek throws the wait away, a tell
// (seek of zero from here) keeps it, and close is what finally keeps the bytes. The old
// append sought back one byte to rewrite a newline, so a file of lines came out as newlines.

#define COOKIE_CAP (16u * 1024u)

typedef struct {
    char committed[COOKIE_CAP];
    size_t committed_len;
    char pending[COOKIE_CAP];
    size_t pending_len;
    __off64_t pos;
    int backward;
} DropCookie;

static ssize_t CookieWrite(void *cookie, const char *buf, size_t size) {
    DropCookie *const c = cookie;
    if (size > COOKIE_CAP - c->pending_len) {
        return 0;
    }
    memcpy(c->pending + c->pending_len, buf, size);
    c->pending_len += size;
    c->pos += (__off64_t)size;
    return (ssize_t)size;
}

static int CookieSeek(void *cookie, __off64_t *offset, int whence) {
    DropCookie *const c = cookie;
    __off64_t next = c->pos;
    if (whence == SEEK_SET) {
        next = *offset;
    } else if (whence == SEEK_CUR) {
        next = c->pos + *offset;
    } else if (whence == SEEK_END) {
        next = (__off64_t)(c->committed_len + c->pending_len) + *offset;
    } else {
        return -1;
    }
    if (next < 0) {
        return -1;
    }
    if (next < c->pos) {
        c->pending_len = 0;
        c->backward += 1;
    }
    c->pos = next;
    *offset = next;
    return 0;
}

static int CookieClose(void *cookie) {
    DropCookie *const c = cookie;
    if (c->committed_len + c->pending_len > COOKIE_CAP) {
        return -1;
    }
    memcpy(c->committed + c->committed_len, c->pending, c->pending_len);
    c->committed_len += c->pending_len;
    c->pending_len = 0;
    return 0;
}

static cookie_io_functions_t CookieIo(void) {
    cookie_io_functions_t io;
    io.read = NULL;
    io.write = CookieWrite;
    io.seek = CookieSeek;
    io.close = CookieClose;
    return io;
}

static void CheckDroppingCookie(void) {
    cookie_io_functions_t io = CookieIo();

    DropCookie told;
    memset(&told, 0, sizeof(told));
    FILE *tell = fopencookie(&told, "w", io);
    const bool tell_open = tell != NULL;
    if (tell != NULL) {
        fputs("keep\n", tell);
        fflush(tell);
    }
    const long told_at = tell != NULL ? ftell(tell) : -1L;
    if (tell != NULL) {
        fclose(tell);
    }
    const bool tell_kept = tell_open && told_at == 5 && told.backward == 0 &&
                           told.committed_len == 5u && memcmp(told.committed, "keep\n", 5) == 0;

    DropCookie dropped;
    memset(&dropped, 0, sizeof(dropped));
    FILE *probe = fopencookie(&dropped, "w", io);
    const bool probe_open = probe != NULL;
    if (probe != NULL) {
        fputs("abcdefghij\n", probe);
        fflush(probe);
        // Back to the start: the host throws away the line it had not committed.
        (void)fseek(probe, 0, SEEK_SET);
        fputc('Z', probe);
        fclose(probe);
    }
    const bool probe_dropped = probe_open && dropped.backward == 1 &&
                               dropped.committed_len == 1u && dropped.committed[0] == 'Z';

    DropCookie kept;
    memset(&kept, 0, sizeof(kept));
    FILE *file = fopencookie(&kept, "w", io);
    char expect[COOKIE_CAP];
    size_t expect_len = 0;
    bool wrote = file != NULL;
    static const char PAD[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
                              "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    for (int i = 0; i < 40 && wrote; ++i) {
        char line[200];
        const int len = snprintf(line, sizeof(line), "case %02d %s\n", i, PAD);
        wrote = len > 0 && (size_t)len < sizeof(line) &&
                expect_len + (size_t)len < sizeof(expect) && JournalDurableAppend(file, line);
        if (wrote) {
            memcpy(expect + expect_len, line, (size_t)len);
            expect_len += (size_t)len;
        }
    }
    if (wrote) {
        wrote = JournalDurableAppend(file, "\n");
        if (wrote && expect_len + 1u < sizeof(expect)) {
            expect[expect_len++] = '\n';
        } else {
            wrote = false;
        }
    }
    if (file != NULL) {
        fclose(file);
    }
    const bool whole = wrote && kept.backward == 0 && kept.committed_len == expect_len &&
                       memcmp(kept.committed, expect, expect_len) == 0;
    CHECK_TRUE(SUITE, "a backward seek drops a buffered line, and a report line never seeks back",
               CHECK_NONE, tell_kept && probe_dropped && whole,
               "tell kept the line %d; a seek back left %u byte(s) after %d seek(s); "
               "forty lines %s, %d backward seek(s), %u of %u bytes",
               (int)tell_kept, (unsigned)dropped.committed_len, dropped.backward,
               whole ? "kept" : "lost", kept.backward, (unsigned)kept.committed_len,
               (unsigned)expect_len);
}
#endif

/// A crash list for the decision cases, with the one case on it.
static void ListOne(CrashList *list) {
    CrashListInit(list);
    CrashListAdd(list, SUITE, "a listed case");
}

static void CheckDecisions(void) {
    // Muted like a rehearsal, so these cases leave the counts and the report as they were, and
    // what they check is only whether each case was let run. The run's own list, its path and
    // whether it retries are handed back as they were found: a run with retry that lost it here
    // skipped every case its list names. So is the run's set of crashes.
    const uint32_t crashes = ReportCrashedMark();
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    CrashList listed;
    ListOne(&listed);
    ReportRehearse(SUITE);
    ReportSetCrashList(&listed, NULL, false);
    const bool listed_runs = ReportStart(SUITE, "a listed case");
    const bool other_runs = ReportStart(SUITE, "a case nobody listed");
    ReportSkip(SUITE, "a case nobody listed", REPORT_REASON_DEPENDS, "done with it");
    ReportSetCrashList(&listed, NULL, true);
    const bool retried = ReportStart(SUITE, "a listed case");
    ReportSkip(SUITE, "a listed case", REPORT_REASON_DEPENDS, "done with it");
    ReportRehearse(NULL);
    CHECK_TRUE(SUITE, "a case on the crash list is skipped as crashed until retry asks for it",
               CHECK_NONE, !listed_runs && other_runs && retried,
               "listed ran %d, another ran %d, listed under retry ran %d", (int)listed_runs,
               (int)other_runs, (int)retried);

    // A case the program ended inside in this very run is not run again, retry or not: running
    // it is what ended the launch before.
    JournalRecord crash = Record(JOURNAL_CRASH, SUITE, "a case that ended this run", 1);
    ReportAddCrashed(&crash);
    ReportRehearse(SUITE);
    const bool crashed_runs = ReportStart(SUITE, "a case that ended this run");
    ReportRehearse(NULL);
    ReportCrashedRewind(crashes);
    ReportSetCrashList(&saved, saved_path, saved_retry);
    const bool handed_back = ReportRetrying() == saved_retry &&
                             strcmp(ReportCrashListPath(), saved_path) == 0 &&
                             ReportCrashList()->count == saved.count;
    char line[REPORT_LINE_MAX];
    ReportBuildResultLine(line, sizeof(line), "bcn", "a case", "none", "skip",
                          ReportReasonWord(REPORT_REASON_CRASHED), "it ended the program", NULL);
    bool words_right = strcmp(ReportReasonWord(REPORT_REASON_CRASHED), "crashed") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_NOT_CONFIGURED),
                              "not configured") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_UNAVAILABLE), "unavailable") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_DEPENDS), "depends") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_HANG), "hangs") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_UNANSWERED), "unanswered") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_BANNED), "banned") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_COUNT), "unavailable") == 0;
    CHECK_TRUE(SUITE, "a case that ended this run is skipped even under retry", CHECK_NONE,
               !crashed_runs && handed_back,
               "it was %s; the run's crash list, path and retry were %s",
               crashed_runs ? "let run" : "skipped", handed_back ? "handed back" : "lost");
    CHECK_TRUE(SUITE, "a skip says why in one word of a few, beside the sentence", CHECK_NONE,
               words_right &&
                   strcmp(line, "{\"type\":\"case\",\"suite\":\"bcn\",\"name\":\"a case\","
                                "\"tier\":\"none\",\"result\":\"skip\",\"reason\":\"crashed\","
                                "\"detail\":\"it ended the program\"}") == 0,
               "words %s; line %.200s", words_right ? "right" : "wrong", line);
}

static void CheckBookkeeping(void) {
    // A case that ends without saying it started, where every case has to, and one that starts
    // while another has not ended: each is counted, and in a report said in a note.
    const size_t before = ReportBookkeepingProblems();
    const bool required = ReportStartsRequired();
    ReportRehearse(SUITE);
    ReportRequireStarts(true);
    ReportCase(SUITE, "a case that never said it started", CHECK_NONE, true, NULL);
    const size_t unstarted = ReportBookkeepingProblems();
    (void)ReportStart(SUITE, "a case that starts");
    (void)ReportStart(SUITE, "another that starts before it ended");
    ReportCase(SUITE, "another that starts before it ended", CHECK_NONE, true, NULL);
    const size_t overlapped = ReportBookkeepingProblems();
    (void)ReportStart(SUITE, "a case that starts and ends");
    ReportCase(SUITE, "a case that starts and ends", CHECK_NONE, true, NULL);
    ReportSkip(SUITE, "a skip that asked nothing", REPORT_REASON_NOT_CONFIGURED, "nothing");
    const size_t after = ReportBookkeepingProblems();
    ReportRequireStarts(required);
    ReportRehearse(NULL);
    CHECK_TRUE(SUITE, "a case that ended without starting, or started over another, is counted",
               CHECK_NONE,
               unstarted == before + 1u && overlapped == unstarted + 1u && after == overlapped,
               "problems %u, then %u, %u and %u", (unsigned)before, (unsigned)unstarted,
               (unsigned)overlapped, (unsigned)after);
}

// What the watcher below was told, in order.
static uint32_t watched_starts;
static uint32_t watched_ends;
static char watched_last[64];

static void Watcher(const char *suite, const char *name, bool started) {
    if (started) {
        ++watched_starts;
    } else {
        ++watched_ends;
    }
    snprintf(watched_last, sizeof(watched_last), "%s/%s", suite, name);
}

/// The report tells its watcher each start and each end, a skip and a rehearsal's included, and
/// nothing once there is no watcher. The running screen's watcher, on the console, is put back.
static void CheckWatcher(void) {
    ReportRehearse(SUITE);
    watched_starts = watched_ends = 0u;
    watched_last[0] = '\0';
    const ReportWatcher running = ReportSetWatcher(Watcher);
    const bool started = ReportStart(SUITE, "a watched case");
    const bool told_start = watched_starts == 1u && watched_ends == 0u &&
                            strcmp(watched_last, "support/a watched case") == 0;
    ReportCase(SUITE, "a watched case", CHECK_NONE, true, NULL);
    ReportCase(SUITE, "a watched case that never said it started", CHECK_NONE, true, NULL);
    ReportSkip(SUITE, "a watched skip", REPORT_REASON_NOT_CONFIGURED, "nothing");
    const bool told_ends = watched_starts == 1u && watched_ends == 3u &&
                           strcmp(watched_last, "support/a watched skip") == 0;
    const bool handed_back = ReportSetWatcher(NULL) == Watcher;
    (void)ReportStart(SUITE, "an unwatched case");
    ReportCase(SUITE, "an unwatched case", CHECK_NONE, true, NULL);
    const bool quiet = watched_starts == 1u && watched_ends == 3u && handed_back;
    ReportSetWatcher(running);
    ReportRehearse(NULL);
    CHECK_TRUE(SUITE, "the report tells its watcher each case's start and end, and no one else",
               CHECK_NONE, started && told_start && told_ends && quiet,
               "started %d; told the start %d, the ends %d, nothing after %d (%u starts, %u ends, "
               "last %s)",
               (int)started, (int)told_start, (int)told_ends, (int)quiet,
               (unsigned)watched_starts, (unsigned)watched_ends, watched_last);
}

static void CheckPrecedence(void) {
    // No word stands between a case and its run but the hang table's: SM4, which ends the
    // program on the console and on a translator that cannot compile it, runs with no word at
    // all. Only this machine's evidence skips it: a crash in this run, whatever retry says, then
    // the crash list unless retry; the case after it runs either way. The run's own settings
    // and its set of crashes are handed back afterwards.
    static const char *const NEXT = "mrs cntvct_el0 reads a virtual count that moves with the "
                                    "physical one";
    const uint32_t crashes = ReportCrashedMark();
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    // No machine for the check: a run on one whose lines ban SM4 (ScarletNX, Eden) would skip it
    // as banned, which is CheckBans's to judge.
    char saved_machine[BAN_MACHINE_MAX + 1u];
    char saved_version[BAN_VERSION_MAX + 1u];
    snprintf(saved_machine, sizeof(saved_machine), "%s", ReportMachine());
    snprintf(saved_version, sizeof(saved_version), "%s", ReportMachineVersion());
    ReportSetMachine(NULL, NULL);
    static CrashList listed;
    CrashListInit(&listed);
    CrashListAdd(&listed, "cpu", CPU_SM4_CASE);

    ReportReason reason[6];
    bool runs[6];
    char detail[REPORT_KEPT_DETAIL];
    char listed_detail[REPORT_KEPT_DETAIL];
    ReportSetCrashList(NULL, NULL, false);
    runs[0] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[0], detail, sizeof(detail));
    ReportSetCrashList(&listed, NULL, false);
    runs[1] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[1], listed_detail,
                             sizeof(listed_detail));
    runs[2] = ReportWouldRun("cpu", NEXT, &reason[2], NULL, 0);
    ReportSetRetry(true);
    runs[3] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[3], NULL, 0);
    JournalRecord crash = Record(JOURNAL_CRASH, "cpu", CPU_SM4_CASE, 0);
    ReportAddCrashed(&crash);
    runs[4] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[4], NULL, 0);
    runs[5] = ReportWouldRun("cpu", NEXT, &reason[5], NULL, 0);
    ReportCrashedRewind(crashes);
    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetMachine(saved_machine, saved_version);
    const bool handed_back = ReportRetrying() == saved_retry &&
                             strcmp(ReportCrashListPath(), saved_path) == 0 &&
                             ReportCrashedMark() == crashes &&
                             strcmp(ReportMachine(), saved_machine) == 0;
    const bool listed_says = strstr(listed_detail, "on the crash list") != NULL &&
                             strstr(listed_detail, "the mode word retry runs it") != NULL;
    CHECK_TRUE(SUITE, "a case that ends the program runs with no word, and only a crash skips it",
               CHECK_NONE,
               runs[0] && detail[0] == '\0' && !runs[1] && reason[1] == REPORT_REASON_CRASHED &&
                   listed_says && runs[2] && runs[3] && !runs[4] &&
                   reason[4] == REPORT_REASON_CRASHED && runs[5] && handed_back,
               "unlisted %d (\"%.60s\"), listed %d (%s, \"%.120s\"), the next case %d, listed "
               "with retry %d, crashed this run with retry %d (%s), the next case %d; settings "
               "%s",
               (int)runs[0], detail, (int)runs[1], ReportReasonWord(reason[1]), listed_detail,
               (int)runs[2], (int)runs[3], (int)runs[4], ReportReasonWord(reason[4]),
               (int)runs[5], handed_back ? "handed back" : "lost");
}

static void CheckHang(void) {
    // The bsd receive-timeout hangs instead of ending, so the next launch is the one after
    // something else kills the process. Until then the hang table is what skips it. Retry
    // would start it again. The run's own settings and its set of crashes are handed back
    // afterwards.
    const uint32_t crashes = ReportCrashedMark();
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    const bool saved_hang = ReportHangAsked();
    char saved_machine[BAN_MACHINE_MAX + 1u];
    char saved_version[BAN_VERSION_MAX + 1u];
    snprintf(saved_machine, sizeof(saved_machine), "%s", ReportMachine());
    snprintf(saved_version, sizeof(saved_version), "%s", ReportMachineVersion());
    ReportSetMachine(NULL, NULL);
    ReportSetCrashList(NULL, NULL, false);
    ReportSetHang(false);

    ReportReason reason = REPORT_REASON_CRASHED;
    char detail[REPORT_KEPT_DETAIL];
    const bool skipped = !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &reason, detail, sizeof(detail));
    const bool says = reason == REPORT_REASON_HANG && strstr(detail, "mode word hang") != NULL;
    ReportSetRetry(true);
    ReportReason retry_reason = REPORT_REASON_CRASHED;
    const bool retry_skips =
        !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &retry_reason, NULL, 0) &&
        retry_reason == REPORT_REASON_HANG;

    static CrashList listed;
    CrashListInit(&listed);
    CrashListAdd(&listed, "bsd", HANG_BSD_TIMEOUT);
    ReportSetCrashList(&listed, NULL, true);
    ReportSetHang(true);
    const bool asked_runs = ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &reason, NULL, 0);

    // The hang table is the only table: a case that ends the program runs beside it.
    ReportSetCrashList(NULL, NULL, false);
    ReportSetHang(false);
    const bool sm4_runs = ReportWouldRun("cpu", CPU_SM4_CASE, NULL, NULL, 0);

    // Last, and taken back below: kept, it skipped the real bsd case as crashed for the rest of
    // the run.
    JournalRecord crash = Record(JOURNAL_CRASH, "bsd", HANG_BSD_TIMEOUT, 1);
    ReportAddCrashed(&crash);
    ReportSetHang(true);
    ReportSetRetry(true);
    ReportReason crash_reason = REPORT_REASON_HANG;
    const bool crash_wins =
        !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &crash_reason, NULL, 0) &&
        crash_reason == REPORT_REASON_CRASHED;

    ReportCrashedRewind(crashes);
    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetHang(saved_hang);
    ReportSetMachine(saved_machine, saved_version);
    const bool handed_back = ReportRetrying() == saved_retry && ReportHangAsked() == saved_hang &&
                             strcmp(ReportCrashListPath(), saved_path) == 0 &&
                             strcmp(ReportMachine(), saved_machine) == 0;
    CHECK_TRUE(SUITE, "a case known to hang is skipped until hang asks, and retry does not run it",
               CHECK_NONE,
               skipped && says && retry_skips && asked_runs && sm4_runs && crash_wins &&
                   handed_back,
               "skipped %d (%s), retry %d (%s), hang and retry %d, SM4 with no word runs %d, "
               "a crash this run %d (%s); settings %s",
               (int)skipped, ReportReasonWord(reason), (int)retry_skips,
               ReportReasonWord(retry_reason), (int)asked_runs, (int)sm4_runs, (int)crash_wins,
               ReportReasonWord(crash_reason), handed_back ? "handed back" : "lost");
}

/// The decision and hang checks each record a crash for this run, to see it win over everything
/// else. Left behind, that record skips a real case for the rest of the run: in a run of one
/// launch the bsd receive timeout came out crashed instead of hangs, and the summary counted it.
static void CheckCrashesHandedBack(uint32_t before) {
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    const bool saved_hang = ReportHangAsked();
    ReportSetCrashList(NULL, NULL, false);
    ReportSetHang(false);

    const uint32_t after = ReportCrashedMark();
    ReportReason bsd_reason = REPORT_REASON_CRASHED;
    const bool bsd_hangs = !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &bsd_reason, NULL, 0) &&
                           bsd_reason == REPORT_REASON_HANG;
    ReportReason decision_reason = REPORT_REASON_COUNT;
    const bool decision_runs =
        ReportWouldRun(SUITE, "a case that ended this run", &decision_reason, NULL, 0);

    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetHang(saved_hang);
    CHECK_TRUE(SUITE, "the checks that record a crash for this run take it back", CHECK_NONE,
               after == before && bsd_hangs && decision_runs,
               "crashes this run %u before them, %u after; bsd %s; the decision check's case %s",
               (unsigned)before, (unsigned)after, ReportReasonWord(bsd_reason),
               decision_runs ? "runs" : ReportReasonWord(decision_reason));
}

static void CheckModeWords(void) {
    const RunMode plain = ModeFromText("quick");
    const RunMode both = ModeFromText("quick RETRY nojournal");
    char said[REPORT_NOTE_MAX + 1];
    ModeDescribe(said, sizeof(said), &both, MODE_FAMILY_CPU);
    CHECK_TRUE(SUITE, "the words retry and nojournal ask for what they say and nothing else",
               CHECK_NONE,
               !plain.retry && !plain.no_journal && both.retry && both.no_journal &&
                   both.unknown_count == 0u && !ModeFromText("retry").no_journal &&
                   !ModeFromText("nojournal").retry && !ModeFromText("retry").fatal &&
                   strcmp(said, "mode: quick, from the mode file; pictures of failures only; "
                                "families cpu; the crash list's cases again; no journal") == 0,
               "retry %d, nojournal %d, unknown %u; said \"%.200s\"", (int)both.retry,
               (int)both.no_journal, (unsigned)both.unknown_count, said);

    // The words that change what a run reports, in one order whatever order the file has them
    // in; quick and detailed are not among them, since they change nothing a report holds.
    char key[JOURNAL_MODE_MAX];
    char same[JOURNAL_MODE_MAX];
    char bare[JOURNAL_MODE_MAX];
    const RunMode listed =
        ModeFromText("stress retry net quick listen fatal fs graphics audio notime images video "
                     "kernel display service");
    const RunMode shuffled = ModeFromText("images kernel audio notime detailed graphics display "
                                          "service video fs fatal net retry listen stress");
    const RunMode nothing = ModeFromText("quick detailed nojournal");
    ModeKey(key, sizeof(key), &listed);
    ModeKey(same, sizeof(same), &shuffled);
    ModeKey(bare, sizeof(bare), &nothing);
    CHECK_TRUE(SUITE, "the journal keeps the words that change a report, in one order",
               CHECK_NONE,
               strcmp(key, "audio display fatal fs graphics images kernel listen net notime retry "
                           "service stress video") ==
                       0 &&
                   strcmp(key, same) == 0 && bare[0] == '\0',
               "\"%.150s\" and \"%.150s\"; with none of them \"%.150s\"", key, same, bare);

    const RunMode hanging = ModeFromText("hang retry");
    char hang_key[JOURNAL_MODE_MAX];
    char hang_said[REPORT_NOTE_MAX + 1];
    ModeKey(hang_key, sizeof(hang_key), &hanging);
    ModeDescribe(hang_said, sizeof(hang_said), &hanging, MODE_FAMILY_CPU);
    CHECK_TRUE(SUITE, "the word hang asks for the cases that hang, and only beside retry for both",
               CHECK_NONE,
               hanging.hang && hanging.retry && !hanging.fatal &&
                   strcmp(hang_key, "hang retry") == 0 &&
                   strcmp(hang_said,
                          "mode: detailed, the mode file has no quick; pictures of failures "
                          "only; families cpu; the cases that hang; the crash list's cases "
                          "again") == 0 &&
                   ModeFromText("hang").hang && !ModeFromText("hang").retry &&
                   !ModeFromText("hang").fatal,
               "key \"%.60s\"; said \"%.240s\"", hang_key, hang_said);
}

// The judge's own ban lines: one on eden for every version, one at two versions, one inside bounds,
// and the first case again on scarletnx.
static const BanLine TEST_BANS[] = {
    { "support", "a banned case", "eden", "*", "ends the program with SIGABRT", "a run", "quick unban",
      "2026-11-07 ends on 1.2", 7 },
    { "support", "a case banned at two versions", "eden", "1.2, 6a9235c22d", "hangs", "a run",
      "quick unban", "2026-11-07 hangs on 1.2", 8 },
    { "support", "a case banned between two versions", "eden", ">= 1.2 < 1.4", "ends the program",
      "a run", "quick unban", "2026-11-07 ends on 1.3", 9 },
    { "support", "a banned case", "scarletnx", "*", "ends the program with exit 1", "a run",
      "quick unban", "2026-11-07 ends on 10ed344", 10 },
};

/// Whether ReportWouldRun lets @p name run, and when not why, under the machine set now.
static bool Runs(const char *name, ReportReason *reason, char *detail, size_t detail_size) {
    *reason = REPORT_REASON_COUNT;
    return ReportWouldRun(SUITE, name, reason, detail, detail_size);
}

static void CheckBans(void) {
    // A ban holds on its machine, at the versions its line names, and only there; the skip says
    // the line and what breaks; unban runs the case; a crash in this run and the hang table come
    // first, the crash list after. The run's own settings and crashes are handed back afterwards.
    const uint32_t crashes = ReportCrashedMark();
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    const bool saved_unban = ReportUnbanAsked();
    char saved_machine[BAN_MACHINE_MAX + 1u];
    char saved_version[BAN_VERSION_MAX + 1u];
    snprintf(saved_machine, sizeof(saved_machine), "%s", ReportMachine());
    snprintf(saved_version, sizeof(saved_version), "%s", ReportMachineVersion());
    BanUseTable(TEST_BANS, sizeof(TEST_BANS) / sizeof(TEST_BANS[0]));
    ReportSetCrashList(NULL, NULL, false);
    ReportSetUnban(false);

    ReportReason why;
    char detail[REPORT_KEPT_DETAIL];
    ReportSetMachine(NULL, NULL);
    const bool no_machine = Runs("a banned case", &why, NULL, 0);
    ReportSetMachine("eden", "");
    const bool banned = !Runs("a banned case", &why, detail, sizeof(detail)) &&
                        why == REPORT_REASON_BANNED;
    const bool says = strcmp(detail, "banned on eden, no version given, by source/bans.txt line 7 "
                                     "(versions *): ends the program with SIGABRT; checked "
                                     "2026-11-07 ends on 1.2; the mode word unban runs it") == 0;
    const bool listed_needs_version = Runs("a case banned at two versions", &why, NULL, 0) &&
                                      Runs("a case banned between two versions", &why, NULL, 0);
    ReportSetMachine("eden", "6a9235c22d");
    char at_detail[REPORT_KEPT_DETAIL];
    const bool at_listed =
        !Runs("a case banned at two versions", &why, at_detail, sizeof(at_detail)) &&
        why == REPORT_REASON_BANNED &&
        strncmp(at_detail, "banned on eden at 6a9235c22d by source/bans.txt line 8 (versions 1.2, "
                           "6a9235c22d): hangs;",
                89u) == 0 &&
        Runs("a case banned between two versions", &why, NULL, 0);
    ReportSetMachine("eden", "1.3");
    const bool inside = !Runs("a case banned between two versions", &why, NULL, 0) &&
                        Runs("a case banned at two versions", &why, NULL, 0);
    ReportSetMachine("eden", "1.4");
    const bool above = Runs("a case banned between two versions", &why, NULL, 0);
    ReportSetMachine("eden", "v1.2");
    const bool at_bound = !Runs("a case banned between two versions", &why, NULL, 0);
    ReportSetMachine("scarletnx", "10ed344");
    const bool other_machine = !Runs("a banned case", &why, detail, sizeof(detail)) &&
                               strstr(detail, "line 10 ") != NULL &&
                               Runs("a case banned at two versions", &why, NULL, 0);
    ReportSetMachine("console", "");
    const bool no_lines = Runs("a banned case", &why, NULL, 0);
    ReportSetMachine("eden", "");
    ReportSetUnban(true);
    const bool unbanned = Runs("a banned case", &why, NULL, 0);
    // The crash list after the ban: listed and banned is banned; listed and unbanned is crashed
    // until retry; a crash in this run wins over both.
    static CrashList listed;
    CrashListInit(&listed);
    CrashListAdd(&listed, SUITE, "a banned case");
    ReportSetCrashList(&listed, NULL, false);
    ReportSetUnban(false);
    const bool listed_banned = !Runs("a banned case", &why, NULL, 0) && why == REPORT_REASON_BANNED;
    ReportSetUnban(true);
    const bool listed_unbanned = !Runs("a banned case", &why, NULL, 0) &&
                                 why == REPORT_REASON_CRASHED;
    ReportSetCrashList(&listed, NULL, true);
    const bool both_words = Runs("a banned case", &why, NULL, 0);
    ReportSetUnban(false);
    JournalRecord crash = Record(JOURNAL_CRASH, SUITE, "a banned case", 0);
    ReportAddCrashed(&crash);
    const bool crash_first = !Runs("a banned case", &why, NULL, 0) && why == REPORT_REASON_CRASHED;

    ReportCrashedRewind(crashes);
    BanUseTable(NULL, 0);
    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetUnban(saved_unban);
    ReportSetMachine(saved_machine, saved_version);
    const bool handed_back = ReportRetrying() == saved_retry && ReportUnbanAsked() == saved_unban &&
                             strcmp(ReportMachine(), saved_machine) == 0 &&
                             strcmp(ReportCrashListPath(), saved_path) == 0 &&
                             ReportCrashedMark() == crashes && BanCount() == BAN_LINE_COUNT;
    CHECK_TRUE(SUITE, "a case banned on this machine is skipped as banned, its line said, until unban",
               CHECK_NONE,
               no_machine && banned && says && listed_needs_version && at_listed && inside && above &&
                   at_bound && other_machine && no_lines && unbanned && handed_back,
               "no machine runs it %d; eden banned %d, said %d (\"%.150s\"); a list or bounds "
               "need a version %d; at a listed version %d (\"%.90s\"); inside the bounds %d, above "
               "%d, at the lower one %d; another machine's line %d; a machine with no lines %d; "
               "unban %d; settings %s",
               (int)no_machine, (int)banned, (int)says, detail, (int)listed_needs_version,
               (int)at_listed, at_detail, (int)inside, (int)above, (int)at_bound,
               (int)other_machine, (int)no_lines, (int)unbanned,
               handed_back ? "handed back" : "lost");
    CHECK_TRUE(SUITE, "a ban comes after a crash in this run and before the crash list", CHECK_NONE,
               listed_banned && listed_unbanned && both_words && crash_first,
               "listed and banned %d, listed and unbanned %d, unban and retry %d, a crash in this "
               "run first %d",
               (int)listed_banned, (int)listed_unbanned, (int)both_words, (int)crash_first);
}

static void CheckBanVersions(void) {
    // What a versions field may be, and where each holds.
    static const char *const GOOD[] = { "*", "1.2", "1.2, 6a9235c22d", "6a9235c22d,10ed344",
                                        "< 2", "<= 1.2.3", ">= v1.2", "> 0", ">= 1.2 < 1.4",
                                        "> 1 <= 2" };
    static const char *const BAD[] = { "", "a b", "1.2,,3", "1.2,", "< x", "<", ">= 1 >= 2",
                                       "< 2 > 1", ">= 1 < 2 < 3", "<= 1.", "has/slash",
                                       "123456789012345678901234567890123" };
    uint32_t wrong = 0;
    const char *first = "";
    for (size_t i = 0; i < sizeof(GOOD) / sizeof(GOOD[0]); ++i) {
        if (!BanVersionsValid(GOOD[i])) {
            first = wrong++ == 0u ? GOOD[i] : first;
        }
    }
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
        if (BanVersionsValid(BAD[i]) || BanVersionsHold(BAD[i], "1")) {
            first = wrong++ == 0u ? BAD[i] : first;
        }
    }
    const bool holds = BanVersionsHold("*", "") && BanVersionsHold("*", "x") &&
                       !BanVersionsHold("1.2", "") && BanVersionsHold("1.2, 6a9235c22d", "6a9235c22d") &&
                       !BanVersionsHold("1.2, 6a9235c22d", "6a9235c") && BanVersionsHold("< 2", "1.9.9") &&
                       !BanVersionsHold("< 2", "2.0") && BanVersionsHold("<= 1.2.3", "1.2.3") &&
                       BanVersionsHold(">= v1.2", "1.2") && !BanVersionsHold("> 0", "0") &&
                       BanVersionsHold(">= 1.2 < 1.4", "1.3.7") && !BanVersionsHold(">= 1.2 < 1.4", "1.4") &&
                       !BanVersionsHold("< 2", "6a9235c22d") && !BanVersionsHold("< 2", NULL);
    const bool versions = BanVersionValid("6a9235c22d") && BanVersionValid("10ed344-dirty") &&
                          !BanVersionValid("") && !BanVersionValid("a b") && !BanVersionValid("a@b") &&
                          !BanVersionValid("123456789012345678901234567890123");
    CHECK_TRUE(SUITE, "a ban's versions are every one, a list, or bounds on dotted numbers",
               CHECK_NONE, wrong == 0u && holds && versions,
               "%u fields read wrong, the first \"%s\"; holds %d; a machine's version %d",
               (unsigned)wrong, first, (int)holds, (int)versions);
}

static void CheckBanTable(void) {
    // Every line of source/bans.txt names a machine the list knows, versions the program reads, a
    // case it can match and what was seen; and the lines the two runs proved are there.
    uint32_t malformed = 0;
    uint32_t first = 0;
    for (size_t i = 0; i < BAN_LINE_COUNT; ++i) {
        const BanLine *const b = &BAN_LINES[i];
        const bool shaped = b->suite[0] != '\0' && strchr(b->suite, ' ') == NULL &&
                            strlen(b->suite) < JOURNAL_SUITE_MAX && b->name[0] != '\0' &&
                            strlen(b->name) < JOURNAL_NAME_MAX && BanMachineKnown(b->machine) &&
                            BanVersionsValid(b->versions) && b->breaks[0] != '\0' &&
                            b->source[0] != '\0' && b->retest[0] != '\0' &&
                            strlen(b->checked) > 11u && b->checked[4] == '-' && b->line > 0u;
        if (!shaped) {
            first = malformed++ == 0u ? b->line : first;
        }
    }
    size_t eden = 0;
    size_t scarletnx = 0;
    size_t console = 0;
    const size_t eden_lines = BanCountFor("eden", "6a9235c22d", &eden);
    (void)BanCountFor("scarletnx", "10ed344", &scarletnx);
    (void)BanCountFor("console", "", &console);
    const bool proven =
        eden == 8u && eden_lines == 8u && scarletnx == 2u && console == 0u &&
        BanFind("cpu", CPU_SM4_CASE, "eden", "6a9235c22d") != NULL &&
        BanFind("cpu", CPU_SM4_CASE, "scarletnx", "10ed344") != NULL &&
        BanFind("cpu", "mrs daif reads nothing outside the four interrupt mask bits", "scarletnx",
                "10ed344") != NULL &&
        BanFind("cpu", "mrs cntvct_el0 reads a virtual count that moves with the physical one",
                "scarletnx", "10ed344") == NULL &&
        BanFind("geometry", "a geometry shader turns each of four points into its 8 by 8 square",
                "eden", "") != NULL &&
        BanFind("cpu", CPU_SM4_CASE, "", "") == NULL && !BanMachineKnown("yuzu") &&
        BanMachineKnown("scarletnx-android") &&
        strcmp(BanMachineList(), "console eden ryujinx scarletnx scarletnx-android android") == 0;
    CHECK_TRUE(SUITE, "source/bans.txt's lines are whole, and the two runs' lines are there",
               CHECK_NONE, malformed == 0u && BAN_LINE_COUNT > 0u && proven,
               "%u of %u lines malformed, the first line %u; eden %u of %u lines hold, scarletnx "
               "%u, console %u; the machines \"%s\"",
               (unsigned)malformed, (unsigned)BAN_LINE_COUNT, (unsigned)first, (unsigned)eden,
               (unsigned)eden_lines, (unsigned)scarletnx, (unsigned)console, BanMachineList());

    // The note a new run carries: how many lines hold, at what version, and what unban does.
    char said[4][REPORT_NOTE_MAX];
    BanUseTable(TEST_BANS, sizeof(TEST_BANS) / sizeof(TEST_BANS[0]));
    BanDescribe(said[0], sizeof(said[0]), "eden", "6a9235c22d", false);
    BanDescribe(said[1], sizeof(said[1]), "eden", "", true);
    BanDescribe(said[2], sizeof(said[2]), "console", "", false);
    BanDescribe(said[3], sizeof(said[3]), "", "", true);
    char none[8] = "x";
    const size_t quiet = BanDescribe(none, sizeof(none), NULL, NULL, false);
    BanUseTable(NULL, 0);
    const bool notes =
        strcmp(said[0], "2 of the 4 lines of source/bans.txt hold for eden at 6a9235c22d: their "
                        "cases are skipped as banned, and the mode word unban runs them; 1 more "
                        "name eden and hold only at other versions") == 0 &&
        strcmp(said[1], "1 of the 4 lines of source/bans.txt hold for eden, no version given, and "
                        "the word unban runs their cases; 2 more name eden and hold only at other "
                        "versions") == 0 &&
        strcmp(said[2], "no line of source/bans.txt names console, so every case runs") == 0 &&
        strcmp(said[3], "the word unban, with no machine= word: no line of source/bans.txt holds, "
                        "so it runs nothing more") == 0 &&
        quiet == 0u && none[0] == '\0';
    CHECK_TRUE(SUITE, "a new run says how many ban lines hold for its machine, with the version",
               CHECK_NONE, notes, "\"%.150s\"; \"%.150s\"; \"%.80s\"; \"%.80s\"; quiet %u",
               said[0], said[1], said[2], said[3], (unsigned)quiet);
}

#ifdef EDENPROBE_HOST
// A run of two steps, launched as main launches one, in a process of its own each time: the first
// step has a case that ends the program between two that do not, the second step comes after.
static const char *const RELAUNCH_ENDS = "a case that ends the program";

static void RelaunchCase(const char *suite, const char *name) {
    if (ReportStart(suite, name)) {
        ReportCase(suite, name, CHECK_NONE, true, NULL);
    }
}

static void RelaunchFirstStep(void) {
    RelaunchCase("ending", "the case before it");
    if (ReportStart("ending", RELAUNCH_ENDS)) {
        // The process ends with the case's start on the card, as a translator's ends on an
        // instruction it cannot run. SIGKILL, which leaves no core behind on the build machine.
        raise(SIGKILL);
    }
    RelaunchCase("ending", "the case after it");
}

static void RelaunchSecondStep(void) {
    RelaunchCase("after", "the next step's case");
}

/// The frontend's note after each step that ran to its end.
static void RelaunchStepDone(const char *name) {
    ReportNote("step done: %s", name);
}

static const RunnerStep RELAUNCH_STEPS[] = {
    { "ending", RelaunchFirstStep, 0, false, true },
    { "after", RelaunchSecondStep, 0, false, true },
};

/// One launch of that run under @p root: the status waitpid gives, or -1 when there was none.
static int RelaunchOnce(const char *root) {
    static RunnerFrontend frontend;
    frontend.root = root;
    frontend.version = "0.0.0";
    frontend.build = "relaunch-check";
    frontend.hash = "relaunch";
    frontend.steps = RELAUNCH_STEPS;
    frontend.step_count = sizeof(RELAUNCH_STEPS) / sizeof(RELAUNCH_STEPS[0]);
    frontend.step_done = RelaunchStepDone;
    // Nothing of this run's own may be left in a buffer for the launch to write a second time.
    fflush(NULL);
    const pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // The host run's report is this process's too until it lets go: no line of the launch's
        // may land in it. The launch's lines also go to the log, which here is the host run's
        // standard output, so they go nowhere instead.
        ReportClose();
        const int quiet = open("/dev/null", O_WRONLY);
        if (quiet >= 0) {
            dup2(quiet, STDOUT_FILENO);
            dup2(quiet, STDERR_FILENO);
        }
        static Runner runner;
        if (!RunnerInit(&runner, &frontend)) {
            _exit(2);
        }
        const RunMode from_file = RunnerModeFromFile(runner.mode_path);
        RunnerPlan(&runner, &from_file);
        RunMode mode = RunnerModeAtStart(&runner, &from_file);
        RunnerOpen(&runner, &mode);
        if (runner.plan.start == JOURNAL_FRESH) {
            RunnerBeginJournal(&runner, &from_file, &mode);
        }
        RunnerApplyMode(&runner, &mode);
        RunnerSteps(&runner, RunnerFamilies(&runner, &mode), false, NULL);
        RunnerFinish(&runner, &mode, false);
        _exit(0);
    }
    int status = 0;
    return waitpid(pid, &status, 0) == pid ? status : -1;
}

/// How many times @p needle is in @p text.
static uint32_t Occurrences(const char *text, const char *needle) {
    uint32_t count = 0;
    for (const char *at = strstr(text, needle); at != NULL; at = strstr(at + 1, needle)) {
        ++count;
    }
    return count;
}

/// The files a run of RELAUNCH_STEPS leaves under @p root, by name, then their folders: anything
/// else left in them keeps a folder there, and so says false.
static bool RelaunchSweep(const char *root) {
    char path[320];
    static const char *const FILES[] = { "runs/relaunch/report.jsonl", "runs/relaunch/journal",
                                         "crashes", "last-run", "mode" };
    bool gone = true;
    for (size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", root, FILES[i]);
        gone = remove(path) == 0 && gone;
    }
    static const char *const FOLDERS[] = { "runs/relaunch", "runs", "" };
    for (size_t i = 0; i < sizeof(FOLDERS) / sizeof(FOLDERS[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", root, FOLDERS[i]);
        gone = rmdir(path) == 0 && gone;
    }
    return gone;
}

/// A case that ends the program, with no word asking for it: the launch it ends leaves its start
/// in the journal, and the next launch skips it as crashed, puts it on the crash list and runs
/// the case after it and the next step; a new run skips it from the list. The word fatal is read,
/// changes none of it, and the report says so once. Every file the launches wrote goes again.
static void CheckRelaunch(const char *scratch) {
    char root[256];
    char path[320];
    snprintf(root, sizeof(root), "%.200s/relaunch", scratch);
    mkdir(root, 0777);
    snprintf(path, sizeof(path), "%s/mode", root);
    const bool moded = JournalWriteFile(path, "quick fatal\n");

    static char report[16384];
    static char again[16384];
    char listed[512] = "";
    char report_path[320];
    snprintf(report_path, sizeof(report_path), "%s/runs/relaunch/report.jsonl", root);
    const int first = RelaunchOnce(root);
    const int second = RelaunchOnce(root);
    const bool read = JournalReadFile(report_path, report, sizeof(report), NULL) > 0;
    snprintf(path, sizeof(path), "%s/crashes", root);
    (void)JournalReadFile(path, listed, sizeof(listed), NULL);
    const int third = RelaunchOnce(root);
    const bool reread = JournalReadFile(report_path, again, sizeof(again), NULL) > 0;

    const bool ended = first != -1 && WIFSIGNALED(first) && WTERMSIG(first) == SIGKILL;
    const bool finished = second != -1 && WIFEXITED(second) && WEXITSTATUS(second) == 0 &&
                          third != -1 && WIFEXITED(third) && WEXITSTATUS(third) == 0;
    static const char CRASHED[] = "{\"type\":\"case\",\"suite\":\"ending\",\"name\":\"a case that "
                                  "ends the program\",\"tier\":\"none\",\"result\":\"skip\","
                                  "\"reason\":\"crashed\",\"detail\":\"the program ended inside "
                                  "it earlier in this run";
    static const char LISTED[] = "{\"type\":\"case\",\"suite\":\"ending\",\"name\":\"a case that "
                                 "ends the program\",\"tier\":\"none\",\"result\":\"skip\","
                                 "\"reason\":\"crashed\",\"detail\":\"on the crash list";
    static const char AFTER[] = "\"suite\":\"ending\",\"name\":\"the case after it\",\"tier\":"
                                "\"none\",\"result\":\"pass\"";
    static const char NEXT[] = "\"suite\":\"after\",\"name\":\"the next step's case\",\"tier\":"
                               "\"none\",\"result\":\"pass\"";
    static const char BEFORE[] = "\"name\":\"the case before it\"";
    static const char SUMMARY[] =
        "{\"type\":\"summary\",\"passed\":3,\"failed\":0,\"skipped\":0,\"crashed\":1}";
    const bool resumed =
        read && Occurrences(report, CRASHED) == 1u && Occurrences(report, AFTER) == 1u &&
        Occurrences(report, NEXT) == 1u && Occurrences(report, BEFORE) == 1u &&
        strstr(report, "journal: launch 2 resumes the run") != NULL &&
        strstr(report, SUMMARY) != NULL &&
        strstr(listed, "ending a case that ends the program\n") != NULL &&
        Occurrences(report, "step done: ending") == 1u &&
        Occurrences(report, "step done: after") == 1u;
    const bool new_run = reread && Occurrences(again, LISTED) == 1u &&
                         Occurrences(again, AFTER) == 1u && Occurrences(again, NEXT) == 1u &&
                         strstr(again, SUMMARY) != NULL;
    const uint32_t said = read ? Occurrences(report, MODE_FATAL_SAID) : 0u;
    const uint32_t said_again = reread ? Occurrences(again, MODE_FATAL_SAID) : 0u;

    const bool gone = RelaunchSweep(root);

    CHECK_TRUE(SUITE, "a case that ends the program costs it alone: the next launch skips it and "
                      "runs on",
               CHECK_NONE, moded && ended && finished && resumed && new_run && gone,
               "mode file %d; launch 1 %s (status %d), launches 2 and 3 %s; resumed %d, the new "
               "run %d, files gone %d; crash list \"%.80s\"; report ends \"...%.200s\"",
               (int)moded, ended ? "ended inside the case" : "did not end inside the case",
               first, finished ? "finished" : "did not finish", (int)resumed, (int)new_run,
               (int)gone, listed, read && strlen(report) > 200u ? report + strlen(report) - 200u
                                                                 : report);
    CHECK_TRUE(SUITE, "the word fatal changes nothing, said once a run over its launches",
               CHECK_NONE, said == 1u && said_again == 1u && resumed && new_run,
               "said %u times in the run of two launches and %u in the next run; the runs %s",
               (unsigned)said, (unsigned)said_again,
               resumed && new_run ? "went on past the case" : "did not go on past the case");
}

/// The same run with its ending case banned on eden, and a line for a case the build lacks.
static const BanLine RELAUNCH_BANS[] = {
    { "ending", "a case that ends the program", "eden", "*", "ends the program with SIGKILL",
      "the judge's own", "quick unban", "2026-11-08 ends on t1", 99 },
    { "ending", "a case this build lacks", "eden", "*", "ends the program", "the judge's own",
      "quick unban", "2026-11-08 ends on t1", 98 },
};

/// The run with its ending case banned on the machine its mode file names: one launch, the case
/// skipped as banned with its line, and the line for a case it lacks said; with unban the case
/// runs, ends the launch and goes on the crash list; on a machine no line names, nothing changes.
static void CheckRelaunchBanned(const char *scratch) {
    char root[256];
    char path[320];
    char report_path[320];
    snprintf(root, sizeof(root), "%.200s/relaunch", scratch);
    snprintf(path, sizeof(path), "%s/mode", root);
    snprintf(report_path, sizeof(report_path), "%s/runs/relaunch/report.jsonl", root);
    mkdir(root, 0777);
    BanUseTable(RELAUNCH_BANS, sizeof(RELAUNCH_BANS) / sizeof(RELAUNCH_BANS[0]));
    static char banned[16384];
    static char unbanned[16384];
    static char console[16384];

    bool moded = JournalWriteFile(path, "quick machine=eden@t1\n");
    const int alone = RelaunchOnce(root);
    const bool read = JournalReadFile(report_path, banned, sizeof(banned), NULL) > 0;
    moded = JournalWriteFile(path, "quick machine=eden@t1 unban\n") && moded;
    const int ended = RelaunchOnce(root);
    const int after = RelaunchOnce(root);
    const bool reread = JournalReadFile(report_path, unbanned, sizeof(unbanned), NULL) > 0;
    moded = JournalWriteFile(path, "quick machine=console\n") && moded;
    const int plain = RelaunchOnce(root);
    const bool read_console = JournalReadFile(report_path, console, sizeof(console), NULL) > 0;
    BanUseTable(NULL, 0);
    const bool gone = RelaunchSweep(root);

    static const char BANNED[] = "\"suite\":\"ending\",\"name\":\"a case that ends the program\","
                                 "\"tier\":\"none\",\"result\":\"skip\",\"reason\":\"banned\","
                                 "\"detail\":\"banned on eden at t1 by source/bans.txt line 99 "
                                 "(versions *): ends the program with SIGKILL; checked 2026-11-08 "
                                 "ends on t1; the mode word unban runs it\"";
    static const char HOLD[] = "bans: 2 of the 2 lines of source/bans.txt hold for eden at t1: "
                               "their cases are skipped as banned, and the mode word unban runs "
                               "them";
    static const char LACKS[] = "bans: source/bans.txt bans line 98, ending \\\"a case this build "
                                "lacks\\\" on eden, which the suites that ran never reached";
    static const char AFTER[] = "\"name\":\"the case after it\",\"tier\":\"none\",\"result\":\"pass\"";
    const bool one_launch =
        moded && alone != -1 && WIFEXITED(alone) && WEXITSTATUS(alone) == 0 && read &&
        Occurrences(banned, BANNED) == 1u && Occurrences(banned, HOLD) == 1u &&
        Occurrences(banned, LACKS) == 1u && Occurrences(banned, AFTER) == 1u &&
        strstr(banned, "{\"type\":\"summary\",\"passed\":3,\"failed\":0,\"skipped\":1,\"crashed\":0}") !=
            NULL &&
        strstr(banned, "journal: launch 2") == NULL;
    const bool ran_it =
        ended != -1 && WIFSIGNALED(ended) && WTERMSIG(ended) == SIGKILL && after != -1 &&
        WIFEXITED(after) && WEXITSTATUS(after) == 0 && reread &&
        strstr(unbanned, "bans: 2 of the 2 lines of source/bans.txt hold for eden at t1, and the "
                         "word unban runs their cases") != NULL &&
        strstr(unbanned, "\"reason\":\"banned\"") == NULL &&
        strstr(unbanned, "{\"type\":\"summary\",\"passed\":3,\"failed\":0,\"skipped\":0,\"crashed\":1}") !=
            NULL;
    const bool unchanged =
        plain != -1 && WIFEXITED(plain) && WEXITSTATUS(plain) == 0 && read_console &&
        strstr(console, "bans: no line of source/bans.txt names console, so every case runs") !=
            NULL &&
        strstr(console, "\"reason\":\"banned\"") == NULL &&
        strstr(console, "\"reason\":\"crashed\",\"detail\":\"on the crash list") != NULL;
    CHECK_TRUE(SUITE, "a case banned on the run's machine costs no launch: skipped as banned, its "
                      "line said",
               CHECK_NONE, one_launch && gone,
               "mode files %d; one launch exit %d, report %d; banned line %u, bans note %u, the "
               "lacking line's note %u; files gone %d; report ends \"...%.160s\"",
               (int)moded, alone, (int)read, (unsigned)Occurrences(banned, BANNED),
               (unsigned)Occurrences(banned, HOLD), (unsigned)Occurrences(banned, LACKS),
               (int)gone, read && strlen(banned) > 160u ? banned + strlen(banned) - 160u : banned);
    CHECK_TRUE(SUITE, "unban runs a banned case, and a machine no line names changes nothing",
               CHECK_NONE, ran_it && unchanged,
               "with unban: launch 1 status %d, launch 2 status %d, report %d, ran %d; on console: "
               "status %d, unchanged %d",
               ended, after, (int)reread, (int)ran_it, plain, (int)unchanged);
}

/// The run with skip= naming its ending suite and a suite it does not have: one launch, every case
/// of the ending suite a skip with the reason asked and the case that ends the program not run,
/// the next step's case run, and the name nothing came up for said at the end.
static void CheckRelaunchSkipped(const char *scratch) {
    char root[256];
    char path[320];
    char report_path[320];
    snprintf(root, sizeof(root), "%.200s/relaunch", scratch);
    snprintf(path, sizeof(path), "%s/mode", root);
    snprintf(report_path, sizeof(report_path), "%s/runs/relaunch/report.jsonl", root);
    mkdir(root, 0777);
    static char report[16384];
    const bool moded = JournalWriteFile(path, "quick skip=Ending skip=nosuch\n");
    const int status = RelaunchOnce(root);
    const bool read = JournalReadFile(report_path, report, sizeof(report), NULL) > 0;
    // No case ended the program, so no crash list was written: the sweep takes one.
    snprintf(path, sizeof(path), "%s/crashes", root);
    char list[8];
    const bool no_list = JournalReadFile(path, list, sizeof(list), NULL) < 0 &&
                         JournalWriteFile(path, "");
    const bool gone = RelaunchSweep(root) && no_list;

    static const char ASKED[] = "\"suite\":\"ending\",\"name\":\"a case that ends the program\","
                                "\"tier\":\"none\",\"result\":\"skip\",\"reason\":\"asked\","
                                "\"detail\":\"the mode file's skip=ending leaves the ending suite "
                                "out; without the word it runs\"";
    static const char NEXT[] = "\"suite\":\"after\",\"name\":\"the next step's case\",\"tier\":"
                               "\"none\",\"result\":\"pass\"";
    static const char LEFT_OUT[] = "suites left out by skip=: ending nosuch";
    static const char NOTHING[] = "skip: no case of nosuch asked to start in this run, so skip= "
                                  "left nothing of it out";
    static const char SUMMARY[] =
        "{\"type\":\"summary\",\"passed\":1,\"failed\":0,\"skipped\":3,\"crashed\":0}";
    const bool one_launch = moded && status != -1 && WIFEXITED(status) &&
                            WEXITSTATUS(status) == 0 && read;
    const bool skipped = one_launch && Occurrences(report, "\"reason\":\"asked\"") == 3u &&
                         Occurrences(report, ASKED) == 1u && Occurrences(report, NEXT) == 1u &&
                         strstr(report, SUMMARY) != NULL;
    const bool said = one_launch && Occurrences(report, LEFT_OUT) == 1u &&
                      Occurrences(report, NOTHING) == 1u &&
                      strstr(report, "no case of ending") == NULL;
    CHECK_TRUE(SUITE, "skip= leaves its suite out of a run, each case a skip asked for, and says a "
                      "name nothing came up for",
               CHECK_NONE, skipped && said && gone,
               "mode file %d; launch status %d, report %d; skipped as asked %u, the ending case "
               "%u, the next step %u; mode line %u, note %u; files gone %d; report ends "
               "\"...%.200s\"",
               (int)moded, status, (int)read, (unsigned)Occurrences(report, "\"reason\":\"asked\""),
               (unsigned)Occurrences(report, ASKED), (unsigned)Occurrences(report, NEXT),
               (unsigned)Occurrences(report, LEFT_OUT), (unsigned)Occurrences(report, NOTHING),
               (int)gone, read && strlen(report) > 200u ? report + strlen(report) - 200u : report);
}

/// The same run with its first launch stopped by a runner at its limit, which the runner says in
/// the journal: the next launch skips the case as timeout with the runner's numbers, counts it
/// with the skips and leaves the crash list alone, so the run after starts the case again.
static void CheckRelaunchTimeout(const char *scratch) {
    char root[256];
    char path[320];
    char report_path[320];
    snprintf(root, sizeof(root), "%.200s/relaunch", scratch);
    snprintf(report_path, sizeof(report_path), "%s/runs/relaunch/report.jsonl", root);
    mkdir(root, 0777);
    snprintf(path, sizeof(path), "%s/mode", root);
    const bool moded = JournalWriteFile(path, "quick\n");

    static char report[16384];
    static char journal[4096];
    char listed[512] = "";
    const int first = RelaunchOnce(root);
    // What tools/run.py writes once it has stopped a launch at its limit.
    snprintf(path, sizeof(path), "%s/runs/relaunch/journal", root);
    FILE *const file = fopen(path, "a");
    const bool marked = file != NULL && fputs("timeout 60 58\t\n", file) >= 0;
    if (file != NULL) {
        fclose(file);
    }
    const int second = RelaunchOnce(root);
    const bool read = JournalReadFile(report_path, report, sizeof(report), NULL) > 0;
    (void)JournalReadFile(path, journal, sizeof(journal), NULL);
    snprintf(path, sizeof(path), "%s/crashes", root);
    const bool no_list = JournalReadFile(path, listed, sizeof(listed), NULL) <= 0;
    // A new run: the case is on no list, so it runs, and ends this launch too; with no word from
    // a runner, the launch after that takes it for the crash it is.
    const int third = RelaunchOnce(root);
    const int fourth = RelaunchOnce(root);
    (void)JournalReadFile(path, listed, sizeof(listed), NULL);

    static const char TIMEOUT[] =
        "{\"type\":\"case\",\"suite\":\"ending\",\"name\":\"a case that ends the program\","
        "\"tier\":\"none\",\"result\":\"skip\",\"reason\":\"timeout\",\"detail\":\"a runner "
        "stopped the program inside it earlier in this run, at the launch's limit of 60 s and 58 "
        "s after the journal's last record, which is how long the case had run; it is not on the "
        "crash list, so a later run starts it again\"}";
    static const char NOTE[] =
        "journal: this run took 2 launches; 0 cases skipped as crashed; 1 case skipped as "
        "timeout, a runner having stopped the program at its limit inside ending \\\"a case that "
        "ends the program\\\"";
    static const char SUMMARY[] =
        "{\"type\":\"summary\",\"passed\":3,\"failed\":0,\"skipped\":1,\"crashed\":0}";
    const bool skipped =
        moded && marked && first != -1 && WIFSIGNALED(first) && second != -1 &&
        WIFEXITED(second) && WEXITSTATUS(second) == 0 && read &&
        Occurrences(report, TIMEOUT) == 1u && Occurrences(report, NOTE) == 1u &&
        strstr(report, "\"reason\":\"crashed\"") == NULL && strstr(report, SUMMARY) != NULL &&
        strstr(journal, "timedout ending 0 60 58 a case that ends the program\t\n") != NULL &&
        strstr(journal, "crashed ending") == NULL && no_list;
    const bool again = third != -1 && WIFSIGNALED(third) && WTERMSIG(third) == SIGKILL &&
                       fourth != -1 && WIFEXITED(fourth) && WEXITSTATUS(fourth) == 0 &&
                       strstr(listed, "ending a case that ends the program\n") != NULL;
    const bool gone = RelaunchSweep(root);
    CHECK_TRUE(SUITE, "a launch a runner stopped at its limit costs the case as a timeout, and "
                      "the run after starts it again",
               CHECK_NONE, skipped && again && gone,
               "mode file %d, marked %d; launch 1 status %d, launch 2 status %d; the timeout line "
               "%u, the note %u, no crash list %d; the next run's launches %d and %d, files gone "
               "%d; report ends \"...%.200s\"",
               (int)moded, (int)marked, first, second, (unsigned)Occurrences(report, TIMEOUT),
               (unsigned)Occurrences(report, NOTE), (int)no_list, third, fourth, (int)gone,
               read && strlen(report) > 200u ? report + strlen(report) - 200u : report);
}
#endif

/// A suite skip= names: its cases are skipped as asked whatever hang, unban and retry say, after a
/// crash in this run, and only its own. The run's settings are handed back afterwards, its own
/// skip= suites among them: this case runs inside a run that may have some.
static void CheckSkipSuites(void) {
    const uint32_t crashes = ReportCrashedMark();
    char run_skips[REPORT_SKIP_SUITES_MAX][JOURNAL_SUITE_MAX];
    const char *run_skip[REPORT_SKIP_SUITES_MAX];
    const size_t run_skip_count = ReportSkipSuiteCount();
    for (size_t i = 0; i < run_skip_count && i < REPORT_SKIP_SUITES_MAX; ++i) {
        snprintf(run_skips[i], sizeof(run_skips[i]), "%s", ReportSkipSuite(i));
        run_skip[i] = run_skips[i];
    }
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    const bool saved_hang = ReportHangAsked();
    static CrashList listed;
    CrashListInit(&listed);
    CrashListAdd(&listed, "bsd", HANG_BSD_TIMEOUT);

    static const char *const SKIPS[] = { "bsd", "tex8k" };
    ReportSetSkipSuites(SKIPS, 2u);
    char unreached[64];
    ReportSkipsUnreached(unreached, sizeof(unreached));
    ReportReason why = REPORT_REASON_CRASHED;
    char detail[REPORT_KEPT_DETAIL];
    ReportSetCrashList(NULL, NULL, false);
    ReportSetHang(false);
    const bool asked = !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &why, detail, sizeof(detail)) &&
                       why == REPORT_REASON_ASKED &&
                       strcmp(detail, "the mode file's skip=bsd leaves the bsd suite out; "
                                      "without the word it runs") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_ASKED), "asked") == 0;
    ReportSetHang(true);
    ReportSetCrashList(&listed, NULL, true);
    const bool over_words = !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &why, NULL, 0) &&
                            why == REPORT_REASON_ASKED;
    const bool other_suite = ReportWouldRun("dns", HANG_BSD_TIMEOUT, &why, NULL, 0) &&
                             ReportWouldRun("tex8", "a case", &why, NULL, 0);
    JournalRecord crash = Record(JOURNAL_CRASH, "bsd", HANG_BSD_TIMEOUT, 1);
    ReportAddCrashed(&crash);
    const bool crash_first = !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &why, NULL, 0) &&
                             why == REPORT_REASON_CRASHED;
    ReportSetSkipSuites(NULL, 0u);
    char none[16] = "x";
    ReportSkipsUnreached(none, sizeof(none));
    const bool cleared = ReportWouldRun("tex8k", "a case", &why, NULL, 0) && none[0] == '\0';

    ReportCrashedRewind(crashes);
    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetHang(saved_hang);
    ReportSetSkipSuites(run_skip, run_skip_count);
    bool skips_back =
        ReportSkipSuiteCount() == run_skip_count && ReportSkipSuite(run_skip_count) == NULL;
    for (size_t i = 0; skips_back && i < run_skip_count; ++i) {
        skips_back = strcmp(ReportSkipSuite(i), run_skips[i]) == 0;
    }
    const bool handed_back = ReportRetrying() == saved_retry && ReportHangAsked() == saved_hang &&
                             strcmp(ReportCrashListPath(), saved_path) == 0 &&
                             ReportCrashedMark() == crashes && skips_back;
    CHECK_TRUE(SUITE, "a suite skip= names is skipped as asked, after a crash in this run and "
                      "before hang, unban and the crash list",
               CHECK_NONE,
               asked && over_words && other_suite && crash_first && cleared && handed_back &&
                   strcmp(unreached, "bsd tex8k") == 0,
               "asked %d (\"%.120s\"); over hang and retry %d; other suites run %d; a crash "
               "first %d; cleared %d; unreached before any case \"%s\"; settings %s",
               (int)asked, detail, (int)over_words, (int)other_suite, (int)crash_first,
               (int)cleared, unreached, handed_back ? "handed back" : "lost");
}

void RunSupportJournal(const char *scratch) {
#ifdef EDENPROBE_HOST
    // A suite skip= leaves out, as a run with the word has one, from the first check on: a check
    // that asks whether a case runs and does not set the suites aside fails here too, not only in
    // a run that named some.
    static const char *const HOST_SKIPS[] = { "tex8k" };
    ReportSetSkipSuites(HOST_SKIPS, 1u);
#endif
    CheckRecords();
    CheckReading();
    CheckTimeout();
    CheckPlans();
    CheckAsking();
    CheckChosenRun();
    CheckCrashList();
    CheckDurableFile(scratch);
    CheckEndLine(scratch);
    CheckDurableLines(scratch);
#if !defined(__SWITCH__) && defined(__GLIBC__)
    CheckDroppingCookie();
#endif
    const uint32_t crashes_before = ReportCrashedMark();
#ifdef EDENPROBE_HOST
    // As a run on ScarletNX has it: a machine whose lines ban SM4. A check that leans on SM4
    // running, or on no machine, then fails here too, and not only on the machine.
    char host_machine[BAN_MACHINE_MAX + 1u];
    char host_version[BAN_VERSION_MAX + 1u];
    snprintf(host_machine, sizeof(host_machine), "%s", ReportMachine());
    snprintf(host_version, sizeof(host_version), "%s", ReportMachineVersion());
    ReportSetMachine("scarletnx", "10ed344");
#endif
    CheckDecisions();
    CheckBookkeeping();
    CheckWatcher();
    CheckPrecedence();
    CheckHang();
    CheckBans();
    CheckSkipSuites();
    CheckBanVersions();
    CheckBanTable();
    CheckCrashesHandedBack(crashes_before);
#ifdef EDENPROBE_HOST
    ReportSetMachine(host_machine, host_version);
    ReportSetSkipSuites(NULL, 0u);
#endif
    CheckModeWords();
#ifdef EDENPROBE_HOST
    CheckRelaunch(scratch);
    CheckRelaunchBanned(scratch);
    CheckRelaunchSkipped(scratch);
    CheckRelaunchTimeout(scratch);
#endif
}
