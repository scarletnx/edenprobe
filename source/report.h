// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "journal.h"
#include "perf.h"
#include "picture.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Where a line goes once a test has produced it.
///
/// Two channels, always both, because the two machines this runs on read different ones: an
/// emulator gets the debug string for free, and a console run leaves the file on the memory
/// card. The format is one JSON object per line rather than an array, so a run that dies halfway
/// still leaves a readable file. The last line of a finished run is the summary.
void ReportOpen(const char *path);
void ReportClose(void);

/// Opens the report of a run that goes on after its program ended: cut back to @p offset bytes
/// when @p cut is set, which is where the step it runs again began, and counted from @p counts.
/// False when the file cannot be opened for update; the run then reports on the debug channel.
bool ReportResume(const char *path, uint64_t offset, bool cut, const JournalCounts *counts);

/// The journal at @p path, begun anew for a run or opened to go on with one. Every line of the
/// report is then on the card before the next thing starts, and so is every record. Without it
/// (the mode word `nojournal`, and the host build) nothing is written between cases.
bool ReportJournalOpen(const char *path, bool fresh);
void ReportJournalClose(void);
bool ReportJournalOn(void);

/// Writes one record, for the ones main keeps: the run, each launch, step, pass and done, and
/// the finish. Nothing when the journal is off.
void ReportJournalWrite(const JournalRecord *record);

/// The counts as the journal's step and finished records keep them.
JournalCounts ReportCounts(void);

/// How long the report is, every line so far written: where a step that runs again cuts it back
/// to. Nought when there is no file.
uint64_t ReportOffset(void);

/// What decides whether a case runs: the crash list (a copy is kept, and written back to
/// @p path when it changes), and whether the mode word `retry` asked for its cases again.
void ReportSetCrashList(const CrashList *list, const char *path, bool retry);

/// A case the program ended inside earlier in this run. Skipped for the rest of the run
/// whatever retry says, since running it again is what ended the launch before.
void ReportAddCrashed(const JournalRecord *crash);

/// Puts a case on the crash list and writes the file. False when it was there already, the list
/// is full or the file could not be written.
bool ReportCrashListAdd(const char *suite, const char *name);

/// The crash list as it stands, where it is written, and whether retry runs its cases: what a
/// case that borrows the settings for a moment hands back as it found them.
const CrashList *ReportCrashList(void);
const char *ReportCrashListPath(void);
bool ReportRetrying(void);
bool ReportStartsRequired(void);

/// Reopens a closed report to add to its end, with the counts left as they were. For the one
/// line a run can owe after its summary, which is why the detailed mode stopped by itself.
void ReportAppend(const char *path);

/// Whether a report is still being written. The detailed mode asks before it puts anything on
/// a screen, so that presentation can never reach a line.
bool ReportIsOpen(void);

/// The tolerance the comparison on one reported line was held to, and the claim about the
/// hardware that goes with it.
///
/// A property of the line, not an ambition of the case that wrote it: a case stopped before it
/// compared anything reports CHECK_NONE. It lives here rather than beside the comparisons in
/// check.h because it has to travel with the line, where whoever diffs two machines reads it.
typedef enum {
    /// Not a tier. For a line that reports no comparison of anything the hardware produced: a
    /// selfcheck case, a bring-up case, or a drawing case stopped before it saw a pixel.
    /// Claiming a tolerance on one of those would be a claim about a machine nobody asked.
    CHECK_NONE,
    /// Bit for bit. Legal only where the result is defined exactly: integer render targets,
    /// integer arithmetic, bit patterns copied around. A float shader never qualifies, whatever
    /// the numbers look like on one machine.
    CHECK_EXACT,
    /// Within a stated number of units in the last place. For float work, where two correct
    /// implementations legitimately differ: the graphics API only requires division within
    /// 2.5 ULP, and the transcendentals are looser still.
    CHECK_ULP,
    /// The right answer, possibly a pixel or two away. For rasterisation coverage, where sub
    /// pixel precision is an implementation-reported limit rather than a constant.
    CHECK_POSITION,
} CheckTier;

/// One finished case. @p detail is free text explaining a failure, and is ignored when passing.
///
/// This is also where a case's picture is closed, written and named on the line, so a picture
/// belongs to exactly one case and cannot outlive it.
void ReportCase(const char *suite, const char *name, CheckTier tier, bool passed,
                const char *detail);

/// Why a case was skipped, which its line carries as `reason`, one of a few words, beside the
/// sentence in `detail`.
typedef enum {
    /// The program ended inside it on this machine: in this run, which the journal says, or in
    /// an earlier one, which the crash list says. Counted apart from every other skip.
    REPORT_REASON_CRASHED,
    /// Known to end a machine's program, and not asked for.
    REPORT_REASON_MAY_CRASH,
    /// The net file does not name what it needs.
    REPORT_REASON_NOT_CONFIGURED,
    /// A service or a resource this program cannot have here.
    REPORT_REASON_UNAVAILABLE,
    /// A case before it did not leave what it needs.
    REPORT_REASON_DEPENDS,
    REPORT_REASON_COUNT,
} ReportReason;

/// The word a reason goes onto the line as.
const char *ReportReasonWord(ReportReason reason);

/// A case that could not ask its question here, with the reason and a sentence. Counted apart
/// from passes and failures, because a machine that was never asked has not agreed with
/// anything: a run where every net case skipped is not a green net run. The line carries the
/// result "skip" and the none tier, since it compares nothing.
void ReportSkip(const char *suite, const char *name, ReportReason reason, const char *detail);

/// Says the case @p name is about to start, before any of its work, and whether it may.
///
/// False when it must not run: it ended this machine's program in this run or an earlier one.
/// Its skip line is then already written, and the caller does none of the case's work and
/// reports nothing more for it. True, and the journal says on the card that the case started,
/// so a launch after the program ends inside it knows which case to skip. Every case of the
/// cpu, graphics and net families starts this way; the judge's cases ask the machine nothing
/// but ordinary arithmetic, and are only journaled when they end.
bool ReportStart(const char *suite, const char *name);

/// Whether a case that reports a verdict must have started. On for the families that ask the
/// machine something, where one that did not is a case a crash could not skip, and says so in a
/// note.
void ReportRequireStarts(bool on);

/// How many times a case started while another had not ended, or ended without starting where
/// it had to: each is a case whose crash the journal could not pin on it, and says so in a note.
size_t ReportBookkeepingProblems(void);

/// The longest line a case can produce: the fields below plus four paths to its pictures.
#define REPORT_LINE_MAX 2048

/// Builds the line a case reports, without emitting it.
///
/// Exposed so that the shape of a line, and the fields a picture adds to it, can be checked on
/// the host with no device and no file. @p paths may be NULL. Returns the length written.
///
/// What it writes always parses. A buffer too small for the whole line gets an empty object
/// rather than the front of a real one, because a truncated line ends inside a string and takes
/// the rest of the file down with it.
size_t ReportBuildCaseLine(char *out, size_t out_size, const char *suite, const char *name,
                           const char *tier, bool passed, const char *detail,
                           const PicturePaths *paths);

/// The same line with the result spelled out, which is how a skip is written, and its reason
/// after it when @p reason is not NULL.
size_t ReportBuildResultLine(char *out, size_t out_size, const char *suite, const char *name,
                             const char *tier, const char *result, const char *reason,
                             const char *detail, const PicturePaths *paths);

/// The summary line for the given counts, exposed so its shape can be checked on the host.
/// @p skipped counts the skips of every reason but crashed, which @p crashed counts.
size_t ReportBuildSummaryLine(char *out, size_t out_size, size_t passed, size_t failed,
                              size_t skipped, size_t crashed);

/// The same line with the case's time in microseconds last, as `"us":N`, when @p us is not NULL.
/// Whole or not at all, like a picture's path: a line with no room for it goes without.
size_t ReportBuildTimedCaseLine(char *out, size_t out_size, const char *suite, const char *name,
                                const char *tier, bool passed, const char *detail,
                                const PicturePaths *paths, const uint32_t *us);

/// Mutes the report for the passes a timed suite makes before its verdict pass: no line, no
/// count, no picture, only the time each case took. Off, everything is as it was.
///
/// While on, the debug channel alone gets a line before each case of @p suite starts, and one
/// now for the first, so that a hang in a rehearsal is named in the emulator's log down to the
/// case; see ReportBuildBreadcrumb. NULL turns it off.
void ReportRehearse(const char *suite);

/// Builds the line that says which case of a rehearsal is about to run:
/// `EDENPROBE TIMING <suite> pass <n> case <name>`, with the name the case had in the pass before.
/// The warm-up, which has no pass before it, says `case number <k>` instead, and the name of the
/// case before it when there is one: `case number 3 after <name>`.
size_t ReportBuildBreadcrumb(char *out, size_t out_size, const char *suite, uint32_t pass,
                             uint32_t index, const char *name, const char *previous);

/// Builds the line that sums up a timed suite's cases, without emitting it. @p samples is how many
/// timed runs each case's number is the smallest of, the verdict pass included.
size_t ReportBuildPerfLine(char *out, size_t out_size, const char *suite,
                           const PerfSummary *summary, unsigned samples);

/// Emits the perf line for the suite just run.
void ReportPerf(const char *suite);

/// Both of the above at once, the one every other line builder here ends in.
size_t ReportBuildTimedResultLine(char *out, size_t out_size, const char *suite,
                                  const char *name, const char *tier, const char *result,
                                  const char *reason, const char *detail,
                                  const PicturePaths *paths, const uint32_t *us);


/// The longest note ReportNote carries whole. Stated here rather than buried in the buffer, so a
/// caller with a long note can be checked against it before anything is emitted: a note cut
/// short leaves the reader half a sentence and no sign that there was more.
#define REPORT_NOTE_MAX 767

/// A line that is not a case: progress, a note, an environment fact worth recording.
void ReportNote(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/// The last line, carrying the counts a runner checks without parsing everything above it.
void ReportSummary(void);

/// How many cases have passed, failed, been skipped and been skipped as crashed so far, in
/// this run: a launch that resumes one starts from the counts the journal kept.
size_t ReportPassed(void);
size_t ReportFailed(void);
size_t ReportSkipped(void);
size_t ReportCrashed(void);

typedef enum {
    REPORT_PASS,
    REPORT_FAIL,
    REPORT_SKIP,
    /// A skip whose reason is crashed, kept apart the way the summary counts it.
    REPORT_CRASHED,
} ReportResult;

/// How much of a case's suite, name and detail its line carries, escaped; a kept case holds the
/// same, unescaped.
#define REPORT_KEPT_SUITE 64
#define REPORT_KEPT_NAME 128
#define REPORT_KEPT_DETAIL 512

/// One case as its line went out, kept for the detailed mode's screens. Copied where the line is
/// emitted and from the same values, so a screen shows what the run recorded and nothing else:
/// each text field is what a reader of the line gets back once it undoes the escaping.
typedef struct {
    char suite[REPORT_KEPT_SUITE];
    char name[REPORT_KEPT_NAME];
    char tier[12];
    char detail[REPORT_KEPT_DETAIL];
    ReportResult result;
    /// Its picture's index for PictureAt, or -1 when none was kept.
    int32_t picture;
} ReportRecord;

/// Fills @p record the way the store keeps a case, which is what the host build checks.
void ReportRecordFill(ReportRecord *record, const char *suite, const char *name,
                      const char *tier, ReportResult result, const char *detail,
                      int32_t picture);

/// The most cases kept. A full run is under five hundred; past this the screens say how many
/// they are missing, and the report itself says nothing about it.
#define REPORT_MAX_RECORDS 4096

/// Whether every emitted case is also kept for the screens. Off by default, so the automatic run
/// allocates nothing; the store grows as cases arrive and stops, silently, when it cannot.
void ReportRetain(bool on);
bool ReportIsRetaining(void);

/// The cases kept so far, in the order they were reported, as one array, or NULL with a count of
/// nought. The array moves when a case is added, so it is read once the report is closed.
const ReportRecord *ReportRecords(size_t *count);

#ifdef __cplusplus
}
#endif
