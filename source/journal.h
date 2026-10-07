// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Where a run is, kept on the memory card between every two things that could end it, so a
/// crash costs the case it happened in and not the rest of the run.
///
/// Two files. The journal (`journal`, beside the report in the run's folder under
/// `sdmc:/edenprobe/runs`) says what the run has done: a line before each case starts and one
/// after it ends, in every pass, each on the card before the program goes on. A launch that finds
/// a run whose last line is a case starting knows the program ended inside that case, and resumes
/// the run after it. One per folder, so each build and label goes on with its own run. The crash
/// list (`sdmc:/edenprobe/crashes`) names the cases that ended this machine's program, one per
/// line, and outlives the run and the build: they are skipped until the word `retry` asks for
/// them again.
///
/// Everything here but the two file helpers is text in and text out, so the host build checks
/// the reading, the writing and every decision a relaunch makes.

#define JOURNAL_FILE_NAME "journal"
#define CRASH_LIST_PATH "sdmc:/edenprobe/crashes"

/// The longest suite or step name, and the longest case name, kept whole. A report line carries
/// at most 127 characters of a name, so every name a line can hold fits.
#define JOURNAL_SUITE_MAX 32u
#define JOURNAL_NAME_MAX 160u
/// The longest record, and the longest line read: a longer one is not a record.
#define JOURNAL_LINE_MAX 256u
/// The mode words a run was started with, as the run record keeps them.
#define JOURNAL_MODE_MAX 96u
#define JOURNAL_BUILD_MAX 64u

/// What a case came to, as its end record says. A skip whose reason is `crashed` is counted
/// apart from every other skip, so it has a result of its own here; a rehearsal counts nothing.
typedef enum {
    JOURNAL_PASSED,
    JOURNAL_FAILED,
    JOURNAL_SKIPPED,
    JOURNAL_CRASHED,
    JOURNAL_MUTED,
    JOURNAL_RESULT_COUNT,
} JournalResult;

/// The four counts a summary carries, indexed by JournalResult up to JOURNAL_MUTED.
typedef struct {
    uint32_t counts[4];
} JournalCounts;

/// One line of the journal:
///
///   run <version> <build> <mode words>    the first line of a run
///   chosen <words>                        what the start screen added to the mode words
///   launch <n>                            each time the program starts on it, 1 the first
///   step <step> <offset> <p> <f> <s> <c>  before a step: the report's length and counts then
///   pass <step> <n>                       before each timed pass, 1 the warm-up
///   start <suite> <pass> <case>           before a case's work
///   end <suite> <pass> <result> <case>    after its line, result pass fail skip crashed muted
///   crashed <suite> <pass> <case>         the case the launch before ended inside
///   done <step>                           after a step
///   finished <p> <f> <s> <c>              after the summary
///   stopped                               by a runner that stopped the program on purpose: the
///                                         case under way did not end it
///
/// A step is what main runs as one: a suite, or the whole net family. A case name runs to the
/// end of its line; every other field is one word.
typedef enum {
    JOURNAL_NONE,
    JOURNAL_RUN,
    JOURNAL_CHOSEN,
    JOURNAL_LAUNCH,
    JOURNAL_STEP,
    JOURNAL_PASS,
    JOURNAL_START,
    JOURNAL_END,
    JOURNAL_CRASH,
    JOURNAL_DONE,
    JOURNAL_FINISHED,
    JOURNAL_STOPPED,
} JournalKind;

typedef struct {
    JournalKind kind;
    /// The step or the suite; for a run record, its version.
    char suite[JOURNAL_SUITE_MAX];
    /// The case; for a run record, its build.
    char name[JOURNAL_NAME_MAX];
    /// A run record's mode words, or the words a chosen record adds.
    char mode[JOURNAL_MODE_MAX];
    /// The pass, or a launch's number.
    uint32_t number;
    JournalResult result;
    /// The report's length in bytes when a step began.
    uint64_t offset;
    JournalCounts counts;
} JournalRecord;

/// Writes @p record as its line, newline included. Returns the length, or 0 when it does not fit,
/// in which case @p out holds an empty string: half a record would read as another one.
size_t JournalFormat(char *out, size_t out_size, const JournalRecord *record);

/// Reads one line, with or without its newline. False for anything that is not a whole record.
bool JournalParse(const char *line, JournalRecord *out);

/// The word a result is written as, and back.
const char *JournalResultWord(JournalResult result);

/// The most crashes one run remembers by name, and the most steps it remembers as done.
#define JOURNAL_CRASHES_MAX 32u
#define JOURNAL_STEPS_MAX 48u

/// A run, as its journal tells it. Fed one line at a time, since a full run's journal is a few
/// hundred kilobytes and the console has better uses for its memory.
typedef struct {
    /// Any record was read.
    bool present;
    /// The run record: the build it came from and its mode words.
    bool has_run;
    char version[JOURNAL_SUITE_MAX];
    char build[JOURNAL_NAME_MAX];
    char mode[JOURNAL_MODE_MAX];
    /// The words the start screen added, which a launch that goes on with the run keeps.
    char chosen[JOURNAL_MODE_MAX];
    /// The run reached its summary.
    bool finished;
    uint32_t launches;
    /// Steps with a done record after their last step record.
    uint32_t done_count;
    char done[JOURNAL_STEPS_MAX][JOURNAL_SUITE_MAX];
    /// The step under way: its last step record, and the counts since, for a step left where it
    /// stopped.
    bool in_step;
    JournalRecord step;
    JournalCounts counts;
    /// The last record was a case starting: the program ended inside that case.
    bool in_case;
    JournalRecord started;
    /// The last case of the step that ended, for saying where a step stopped.
    char last_ended[JOURNAL_NAME_MAX];
    /// Every case this run already skipped as crashed, the step under way's marked.
    uint32_t crash_count;
    JournalRecord crashes[JOURNAL_CRASHES_MAX];
    /// Whole lines that were not records, and a last line with no newline, which a write cut
    /// short leaves and which is never read as a record.
    uint32_t unreadable;
    bool torn;
} JournalState;

void JournalStateInit(JournalState *state);

/// Takes one line in. @p complete says it ended in a newline: a last line without one is a
/// write the program did not live to finish, and counts for nothing.
void JournalFeed(JournalState *state, const char *line, bool complete);

/// The whole text of a journal, one line at a time.
void JournalReadText(JournalState *state, const char *text, size_t length);

/// The journal at @p path, read the same way from the file. False when there is none.
bool JournalReadPath(JournalState *state, const char *path);

/// Whether the step called @p step is done in this run.
bool JournalStepDone(const JournalState *state, const char *step);

/// How a launch starts, which is what the journal, this build and the mode file decide together.
typedef enum {
    /// A new run: no journal, a finished one, or one this launch cannot continue.
    JOURNAL_FRESH,
    /// The program ended inside a case of the step under way: the report goes back to where the
    /// step began, and the step runs again without that case.
    JOURNAL_RESUME_CASE,
    /// The program ended in the step but outside any case it can skip: the report keeps what
    /// the step wrote, the step is not run again, and the run goes on after it.
    JOURNAL_RESUME_STEP,
    /// The program ended between two steps: the run goes on after the last one done.
    JOURNAL_RESUME_BETWEEN,
} JournalStart;

/// Why a launch started a new run rather than going on with the journal's.
typedef enum {
    JOURNAL_NEW_NO_JOURNAL,
    JOURNAL_NEW_FINISHED,
    JOURNAL_NEW_OTHER_BUILD,
    JOURNAL_NEW_OTHER_MODE,
    JOURNAL_NEW_NO_REPORT,
    JOURNAL_NEW_NO_RUN,
} JournalWhyNew;

typedef struct {
    JournalStart start;
    JournalWhyNew why_new;
    /// This launch's number.
    uint32_t launch;
    /// A case that began and never ended, whether or not the run goes on: it goes on the crash
    /// list either way, since the machine ended inside it.
    bool crashed;
    JournalRecord crash;
    /// Where the report goes back to, and the counts it had there, for the three resumes.
    uint64_t offset;
    JournalCounts counts;
} JournalPlan;

/// Decides how this launch starts. @p report_size is the report's length on the card, or -1
/// when there is none: a run cannot go on in a report that is gone or shorter than the journal
/// says it was.
JournalPlan JournalPlanStart(const JournalState *state, const char *version, const char *build,
                             const char *mode, int64_t report_size);

/// The sentence the report carries about a plan: why a new run, or where the last launch ended.
size_t JournalDescribePlan(char *out, size_t out_size, const JournalPlan *plan,
                           const JournalState *state);

/// The note before the summary: how many launches the run took, how many cases were skipped as
/// crashed (@p crashed, the report's count), how many of those only because the crash list named
/// them, and which cases this run's program ended inside. Empty for a run of one launch that
/// skipped nothing as crashed.
size_t JournalDescribeRun(char *out, size_t out_size, const JournalPlan *plan,
                          const JournalState *state, uint32_t crashed);

/// The crash list: the cases that ended this machine's program, `<suite> <case>` a line, in the
/// order they were found. `#` starts a comment line and blank lines are nothing; the file is
/// written whole when it changes, so a comment typed into it does not survive that.
#define CRASH_LIST_MAX 128u

typedef struct {
    char suite[JOURNAL_SUITE_MAX];
    char name[JOURNAL_NAME_MAX];
} CrashEntry;

typedef struct {
    uint32_t count;
    CrashEntry entries[CRASH_LIST_MAX];
    /// Entries past CRASH_LIST_MAX, lines that were not an entry, and a last line with no
    /// newline, which is read as an entry nobody finished writing and left out.
    uint32_t dropped;
    uint32_t unreadable;
    bool torn;
} CrashList;

void CrashListInit(CrashList *list);
void CrashListParse(CrashList *list, const char *text, size_t length);
/// The file's whole text. Returns the length, or 0 with an empty string when it does not fit.
size_t CrashListFormat(char *out, size_t out_size, const CrashList *list);
bool CrashListHas(const CrashList *list, const char *suite, const char *name);
/// False when it was there already or there is no room.
bool CrashListAdd(CrashList *list, const char *suite, const char *name);
/// False when it was not there.
bool CrashListRemove(CrashList *list, const char *suite, const char *name);

/// Appends @p line, which ends in its newline, so that it is on the card before this returns,
/// on an emulator that holds a file's last write back in a buffer of its own as well as on one
/// that does not. The line except its newline goes out first; the newline is the next write,
/// at the end. Eden seeks to the offset of each host write, and that seek empties the host
/// buffer onto disk, so what a crash can still hold is the newline and not the line. A seek
/// back onto the line to rewrite a byte already there drops, on the Android host library,
/// every buffered byte past the seek, which is how a finished journal lost whole suites of
/// the report. @p file is open for update, never for append: append would put the newline
/// after the line even when a seek asked for the end.
bool JournalDurableAppend(FILE *file, const char *line);

/// Reads a whole small file into @p out, terminated. Returns its length, or -1 when there is no
/// such file; a file longer than @p out_size - 1 is read that far and @p longer set.
int64_t JournalReadFile(const char *path, char *out, size_t out_size, bool *longer);

/// Writes a whole small file, closing it, which is what every machine owes a file's contents.
bool JournalWriteFile(const char *path, const char *text);

/// The length of the file at @p path, or -1 when there is none.
int64_t JournalFileSize(const char *path);

#ifdef __cplusplus
}
#endif
