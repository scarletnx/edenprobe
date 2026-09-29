// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../check.h"
#include "../cpuref.h"
#include "../hang.h"
#include "../journal.h"
#include "../maycrash.h"
#include "../mode.h"
#include "../report.h"
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
           memcmp(&a->counts, &b->counts, sizeof(a->counts)) == 0;
}

static void CheckRecords(void) {
    JournalRecord records[11];
    records[0] = Record(JOURNAL_RUN, "0.2.0", "v0.2.0-20-g2ddafce-dirty", 0);
    snprintf(records[0].mode, sizeof(records[0].mode), "fatal graphics retry");
    records[9] = Record(JOURNAL_CHOSEN, NULL, NULL, 0);
    snprintf(records[9].mode, sizeof(records[9].mode), "fatal retry");
    records[10] = Record(JOURNAL_STOPPED, NULL, NULL, 0);
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
    CHECK_TRUE(SUITE, "every journal record reads back as it was written", CHECK_NONE,
               wrong == 0u &&
                   strcmp(start, "start index 7 an indexed draw with the library's open limit "
                                 "uses every index\n") == 0,
               "%u of 11 records came back different, the first \"%.120s\"; a start reads "
               "\"%.120s\"",
               (unsigned)wrong, first_wrong, start);

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
               cut == 0u && small[0] == '\0' && strcmp(joined, "start clear 0 two lines\n") == 0 &&
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
        "begin clear 1 a case",
        "pass clear 99999999999",
        "start clear 1 a\tcase",
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

/// A crash list for the decision cases, with the one case on it.
static void ListOne(CrashList *list) {
    CrashListInit(list);
    CrashListAdd(list, SUITE, "a listed case");
}

static void CheckDecisions(void) {
    // Muted like a rehearsal, so these cases leave the counts and the report as they were, and
    // what they check is only whether each case was let run. The run's own list, its path and
    // whether it retries are handed back as they were found: a run with retry that lost it here
    // skipped every case its list names.
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
    ReportSetCrashList(&saved, saved_path, saved_retry);
    const bool handed_back = ReportRetrying() == saved_retry &&
                             strcmp(ReportCrashListPath(), saved_path) == 0 &&
                             ReportCrashList()->count == saved.count;
    char line[REPORT_LINE_MAX];
    ReportBuildResultLine(line, sizeof(line), "bcn", "a case", "none", "skip",
                          ReportReasonWord(REPORT_REASON_CRASHED), "it ended the program", NULL);
    bool words_right = strcmp(ReportReasonWord(REPORT_REASON_CRASHED), "crashed") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_MAY_CRASH), "may crash") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_NOT_CONFIGURED),
                              "not configured") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_UNAVAILABLE), "unavailable") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_DEPENDS), "depends") == 0 &&
                       strcmp(ReportReasonWord(REPORT_REASON_HANG), "hangs") == 0 &&
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

static void CheckMayCrashTable(void) {
    // Every entry names one case of one suite, once, with the family its suite is in and a
    // reason; and SM4's is the name the cpu suite gives it.
    uint32_t malformed = 0;
    const char *first = "";
    for (size_t i = 0; i < MayCrashCount(); ++i) {
        const MayCrash *const e = MayCrashAt(i);
        const bool shaped = e->suite[0] != '\0' && strchr(e->suite, ' ') == NULL &&
                            strlen(e->suite) < JOURNAL_SUITE_MAX && e->name[0] != '\0' &&
                            strlen(e->name) < JOURNAL_NAME_MAX && e->why[0] != '\0' &&
                            (e->family == MODE_FAMILY_CPU || e->family == MODE_FAMILY_GRAPHICS ||
                             e->family == MODE_FAMILY_NET) &&
                            MayCrashFind(e->suite, e->name) == e;
        if (!shaped) {
            if (malformed == 0u) {
                first = e->name;
            }
            ++malformed;
        }
    }
    const MayCrash *const sm4 = MayCrashFind("cpu", CPU_SM4_CASE);
    CHECK_TRUE(SUITE, "the may-crash table names each case once, with its family and why",
               CHECK_NONE,
               malformed == 0u && MayCrashCount() > 0u && sm4 != NULL &&
                   sm4->family == MODE_FAMILY_CPU && MayCrashFind("cpu", "no such case") == NULL &&
                   MayCrashAt(MayCrashCount()) == NULL,
               "%u of %u entries malformed, the first \"%.80s\"; SM4 %s",
               (unsigned)malformed, (unsigned)MayCrashCount(), first,
               sm4 != NULL ? "found" : "missing");
}

static void CheckPrecedence(void) {
    // The three reasons a case does not run, in their order: a crash in this run, whatever
    // retry says; the crash list, unless retry; the table, unless fatal. The run's own settings
    // are handed back afterwards.
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    const bool saved_asked = ReportMayCrashAsked();
    static CrashList listed;
    CrashListInit(&listed);
    CrashListAdd(&listed, "cpu", CPU_SM4_CASE);

    ReportReason reason[6];
    bool runs[6];
    char detail[REPORT_KEPT_DETAIL];
    ReportSetCrashList(NULL, NULL, false);
    ReportSetMayCrash(false);
    runs[0] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[0], detail, sizeof(detail));
    const bool says_why = strstr(detail, "the mode word fatal") != NULL &&
                          strstr(detail, "SM4") != NULL;
    ReportSetMayCrash(true);
    runs[1] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[1], NULL, 0);
    ReportSetCrashList(&listed, NULL, false);
    runs[2] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[2], NULL, 0);
    ReportSetMayCrash(false);
    ReportSetRetry(true);
    runs[3] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[3], NULL, 0);
    ReportSetMayCrash(true);
    runs[4] = ReportWouldRun("cpu", CPU_SM4_CASE, &reason[4], NULL, 0);
    runs[5] = ReportWouldRun("cpu", "clz counts each of sixteen 8-bit lanes", &reason[5], NULL, 0);
    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetMayCrash(saved_asked);
    const bool handed_back = ReportRetrying() == saved_retry &&
                             ReportMayCrashAsked() == saved_asked &&
                             strcmp(ReportCrashListPath(), saved_path) == 0;
    CHECK_TRUE(SUITE, "a case the table names is skipped as may crash until fatal asks for it",
               CHECK_NONE,
               !runs[0] && reason[0] == REPORT_REASON_MAY_CRASH && says_why && runs[1] &&
                   !runs[2] && reason[2] == REPORT_REASON_CRASHED && !runs[3] &&
                   reason[3] == REPORT_REASON_MAY_CRASH && runs[4] && runs[5] && handed_back,
               "unasked %d (%s), fatal %d, listed with fatal %d (%s), listed with retry %d (%s), "
               "both %d, a case in neither %d; settings %s",
               (int)runs[0], ReportReasonWord(reason[0]), (int)runs[1], (int)runs[2],
               ReportReasonWord(reason[2]), (int)runs[3], ReportReasonWord(reason[3]),
               (int)runs[4], (int)runs[5], handed_back ? "handed back" : "lost");
}

static void CheckHang(void) {
    // The bsd receive-timeout hangs instead of ending, so the next launch is the one after
    // something else kills the process. Until then the hang table is what skips it. Retry
    // would start it again. The run's own settings are handed back afterwards.
    static CrashList saved;
    saved = *ReportCrashList();
    char saved_path[64];
    snprintf(saved_path, sizeof(saved_path), "%s", ReportCrashListPath());
    const bool saved_retry = ReportRetrying();
    const bool saved_hang = ReportHangAsked();
    const bool saved_asked = ReportMayCrashAsked();
    ReportSetCrashList(NULL, NULL, false);
    ReportSetMayCrash(false);
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

    ReportSetCrashList(NULL, NULL, false);
    ReportSetHang(true);
    ReportSetMayCrash(false);
    ReportReason sm4_reason = REPORT_REASON_CRASHED;
    const bool sm4_still =
        !ReportWouldRun("cpu", CPU_SM4_CASE, &sm4_reason, NULL, 0) &&
        sm4_reason == REPORT_REASON_MAY_CRASH;

    // Last, because a crash recorded for this run stays recorded, as the decision check's does.
    JournalRecord crash = Record(JOURNAL_CRASH, "bsd", HANG_BSD_TIMEOUT, 1);
    ReportAddCrashed(&crash);
    ReportSetHang(true);
    ReportSetRetry(true);
    ReportReason crash_reason = REPORT_REASON_HANG;
    const bool crash_wins =
        !ReportWouldRun("bsd", HANG_BSD_TIMEOUT, &crash_reason, NULL, 0) &&
        crash_reason == REPORT_REASON_CRASHED;

    ReportSetCrashList(&saved, saved_path, saved_retry);
    ReportSetHang(saved_hang);
    ReportSetMayCrash(saved_asked);
    const bool handed_back = ReportRetrying() == saved_retry && ReportHangAsked() == saved_hang &&
                             ReportMayCrashAsked() == saved_asked &&
                             strcmp(ReportCrashListPath(), saved_path) == 0;
    CHECK_TRUE(SUITE, "a case known to hang is skipped until hang asks, and retry does not run it",
               CHECK_NONE,
               skipped && says && retry_skips && asked_runs && sm4_still && crash_wins &&
                   handed_back,
               "skipped %d (%s), retry %d (%s), hang and retry %d, SM4 still may-crash %d, "
               "a crash this run %d (%s); settings %s",
               (int)skipped, ReportReasonWord(reason), (int)retry_skips,
               ReportReasonWord(retry_reason), (int)asked_runs, (int)sm4_still, (int)crash_wins,
               ReportReasonWord(crash_reason), handed_back ? "handed back" : "lost");
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
    const RunMode listed = ModeFromText("stress retry net quick fatal graphics notime images");
    const RunMode shuffled = ModeFromText("images notime detailed graphics fatal net retry stress");
    const RunMode nothing = ModeFromText("quick detailed nojournal");
    ModeKey(key, sizeof(key), &listed);
    ModeKey(same, sizeof(same), &shuffled);
    ModeKey(bare, sizeof(bare), &nothing);
    CHECK_TRUE(SUITE, "the journal keeps the words that change a report, in one order",
               CHECK_NONE,
               strcmp(key, "fatal graphics images net notime retry stress") == 0 &&
                   strcmp(key, same) == 0 && bare[0] == '\0',
               "\"%s\" and \"%s\"; with none of them \"%s\"", key, same, bare);

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
               "key \"%s\"; said \"%.240s\"", hang_key, hang_said);
}

void RunSupportJournal(const char *scratch) {
    CheckRecords();
    CheckReading();
    CheckPlans();
    CheckCrashList();
    CheckDurableFile(scratch);
    CheckDecisions();
    CheckBookkeeping();
    CheckMayCrashTable();
    CheckPrecedence();
    CheckHang();
    CheckModeWords();
}
