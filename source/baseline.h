// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A machine's times kept on its memory card, and a run held to them.
///
/// A baseline is a file, sdmc:/edenprobe/baseline/<name>.jsonl, of samples: each sample is the
/// time every timed case of one run took (the `us` its line carries, the smallest of nine), so a
/// file of five samples is five runs of a machine. `baseline=<name>` in the mode file adds this
/// run as a sample once the suites end; `compare=<name>` holds this run's times to that file's
/// samples and says, case by case, which moved past what the samples themselves spread over. The
/// file is the machine's, not the build's: one recorded on a console and copied to an emulator's
/// card makes `compare=` say how far each case is from the console.
///
/// A case is compared once its file has BASELINE_MIN_SAMPLES samples of it. Its reference is the
/// samples' median; its limit the larger of BASELINE_THRESHOLD_PERCENT and one and a half times
/// the farthest a sample lies from that median, and a move must also pass BASELINE_FLOOR_US. That
/// is the method measured on one build against itself (twelve runs, 0 of 400 comparisons called
/// slower, every planted slowdown caught; docs/Baseline.md), which a fixed percentage alone was
/// not: two cases there run at one of two speeds per process. No verdict moves: a slower case is
/// a line of its own and a count in the baseline's summary, never a failure in the run's.

/// Samples a case needs before it is compared.
#define BASELINE_MIN_SAMPLES 3u
/// The smallest limit, in percent of the reference.
#define BASELINE_THRESHOLD_PERCENT 20u
/// The limit from a case's own spread is this many tenths of it: one and a half.
#define BASELINE_SPREAD_TENTHS 15u
/// A move smaller than this many microseconds is never one, whatever percentage it is.
#define BASELINE_FLOOR_US 100u
/// The most samples of one case a comparison holds it to: past it, the newest, and a note.
#define BASELINE_SAMPLES_MAX 64u
/// The most timed cases one run keeps for a baseline. Past it a case is counted, not kept.
#define BASELINE_CASES_MAX 2048u
/// The longest suite and case names kept, as a file's line carries them (escaped for a JSON
/// string), with the terminator. A longer name is cut, the same way when kept and read back.
#define BASELINE_SUITE_MAX 32u
#define BASELINE_NAME_MAX 192u
/// The longest line a baseline file holds or this module writes.
#define BASELINE_LINE_MAX 512u

/// How far a case is from its baseline.
typedef enum {
    BASELINE_SAME,
    BASELINE_SLOWER,
    BASELINE_FASTER,
} BaselineMove;

/// What a case's samples allow it: the reference and the limit, in percent of it.
typedef struct {
    uint32_t reference_us;
    uint32_t limit_percent;
} BaselineLimit;

/// Forgets every case kept so far.
void BaselineReset(void);

/// Keeps a timed case's verdict time, as its line goes out. Rehearsals never come here.
void BaselineTake(const char *suite, const char *name, uint32_t us);

/// How many cases were kept, and how many came past BASELINE_CASES_MAX.
size_t BaselineKept(void);
size_t BaselineDropped(void);

/// The limit of a case whose @p count samples are @p samples, which are sorted in place. @p count
/// must be at least one.
BaselineLimit BaselineLimitOf(uint32_t *samples, size_t count);

/// Where @p us lies against @p limit.
BaselineMove BaselineJudge(uint32_t us, BaselineLimit limit);

/// The word a move is written as: "same", "slower" or "faster".
const char *BaselineMoveWord(BaselineMove move);

/// The line that opens sample @p sample in a baseline file: the build that ran it, its label, how
/// many cases follow.
size_t BaselineBuildSampleLine(char *out, size_t out_size, uint32_t sample, const char *build,
                               const char *label, size_t cases);

/// One case of sample @p sample in a baseline file. @p suite and @p name are JSON string text,
/// already escaped, as BaselineTake keeps them and BaselineParseLine reads them back.
size_t BaselineBuildCaseLine(char *out, size_t out_size, uint32_t sample, const char *suite,
                             const char *name, uint32_t us);

/// What a baseline file's line is, read whole: the line that opens a sample, a case's, or
/// anything else, which in a file this module wrote is a line a write was cut short in.
typedef enum {
    BASELINE_LINE_OTHER,
    BASELINE_LINE_OPENS,
    BASELINE_LINE_CASE,
} BaselineLineKind;

/// The longest build and label a sample's opening line keeps, with the terminator.
#define BASELINE_BUILD_MAX 96u

/// The fields of a baseline file's line: the sample it opens or belongs to; the opening line's
/// build, label and count of cases; a case line's names (as the line carries them, escapes and
/// all, cut as BaselineTake cuts them) and time.
typedef struct {
    uint32_t sample;
    char build[BASELINE_BUILD_MAX];
    char label[BASELINE_BUILD_MAX];
    uint32_t cases;
    char suite[BASELINE_SUITE_MAX];
    char name[BASELINE_NAME_MAX];
    uint32_t us;
} BaselineFileLine;

/// Reads a baseline file's line into @p out. Only a whole line is one: every field in the order
/// the builders above write them, then the closing brace and the line's end. A line cut short,
/// inside a number included, or one the next line was written onto, is BASELINE_LINE_OTHER.
BaselineLineKind BaselineParseLine(const char *line, BaselineFileLine *out);

/// The report's line for a case held to its samples: where it lies (same, slower or faster), its
/// time, the reference, its limit, the floor and how many samples it was held to, so a reader
/// can say how far it is without the file. @p suite and @p name are JSON string text, as
/// BaselineTake keeps them.
size_t BaselineBuildHeldLine(char *out, size_t out_size, const char *baseline, const char *suite,
                             const char *name, BaselineMove move, uint32_t us, BaselineLimit limit,
                             size_t samples);

/// What a comparison came to, for the report's line that sums it up.
typedef struct {
    /// Samples in the file, whole: a sample short of the cases its first line said is left out.
    uint32_t samples;
    /// Cases of this run held to their samples, and what they came to.
    size_t compared;
    size_t slower;
    size_t faster;
    /// Cases with fewer than BASELINE_MIN_SAMPLES samples in the file, none included.
    size_t too_few;
    /// The notes the comparison wrote about its file.
    size_t notes;
    /// errno when the file could not be opened or read.
    int error;
} BaselineTotals;

/// The report's line that sums a comparison up.
size_t BaselineBuildSummaryLine(char *out, size_t out_size, const char *baseline,
                                const BaselineTotals *totals);

/// Where a baseline's report lines and notes go: the report's (BaselineToReport), or a check's,
/// which reads them back instead of writing them into the run it is part of. A NULL member, or a
/// NULL sink, writes nothing of that kind.
typedef struct {
    void (*line)(void *context, const char *line);
    void (*note)(void *context, const char *text);
    void *context;
} BaselineSink;

/// The sink that writes lines with ReportLine and notes with ReportNote.
const BaselineSink *BaselineToReport(void);

/// Whether a baseline file was read: whole, not there, or there and not readable (a directory
/// where the file should be, a path through a file, a read that failed).
typedef enum {
    BASELINE_READ,
    BASELINE_NO_FILE,
    BASELINE_UNREADABLE,
} BaselineRead;

/// Holds the kept cases to the baseline file at @p path, the totals into @p totals: each case held
/// writes its line, named after the baseline @p name, to @p sink. Anything but BASELINE_READ
/// compares nothing, the errno in the totals.
BaselineRead BaselineCompareFile(const char *path, const char *name, const BaselineSink *sink,
                                 BaselineTotals *totals);

/// Adds the kept cases to the baseline file at @p path as a new sample, made if missing: its number
/// into @p sample and the file's samples after it into @p samples_after. False when the file
/// cannot be written, errno saying why.
bool BaselineAddFile(const char *path, const char *build, const char *label, uint32_t *sample,
                     uint32_t *samples_after);

/// What BaselineFinish did, for its caller and its checks.
typedef struct {
    /// The file `compare=` names was read and this run held to it, with what that came to.
    bool compared;
    BaselineTotals totals;
    /// This run went into the file `baseline=` names: as which sample, and the file's samples
    /// after it.
    bool added;
    uint32_t sample;
    uint32_t samples_after;
    /// The notes it wrote.
    size_t notes;
} BaselineOutcome;

/// Compares this run's kept cases with @p directory/<compare>.jsonl, when @p compare names one,
/// then adds them as a sample to @p directory/<add_to>.jsonl, when @p add_to names one, its lines
/// and notes to @p sink. Comparing first is what keeps a run with both words from being held to
/// itself. @p timed false is a run with no times (`notime`), which compares and adds nothing and
/// says so; @p resumed is a run that went on after the program ended inside it, whose earlier
/// launches' times this one does not have: it compares what it has and adds no sample. Call it
/// once the suites have ended and before the summary.
BaselineOutcome BaselineFinish(const char *directory, const char *add_to, const char *compare,
                               const char *build, const char *label, bool timed, bool resumed,
                               const BaselineSink *sink);

#ifdef __cplusplus
}
#endif
