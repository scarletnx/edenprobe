// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../baseline.h"
#include "../check.h"
#include "../mode.h"
#include "../report.h"
#include "tests.h"

// A baseline (baseline.h): the limit a case's samples allow it, the lines its file and the report
// carry, and a file made, added to and compared with. Support cases and the none tier, since no
// verdict reads any of it; what they pin is that a case is called slower exactly when its samples
// say it moved, and that the file reads back what was written.

static const char *const SUITE = "support";

static void CheckLimit(void) {
    uint32_t steady[] = { 1000, 1000, 1000 };
    const BaselineLimit flat = BaselineLimitOf(steady, 3);
    CHECK_TRUE(SUITE, "baseline: samples that agree get the threshold, around their median",
               CHECK_NONE, flat.reference_us == 1000u && flat.limit_percent == 20u,
               "reference %u limit %u", (unsigned)flat.reference_us, (unsigned)flat.limit_percent);

    // The order cases of the measurement: one of two speeds per process.
    uint32_t two_speeds[] = { 1390, 970, 980, 1385, 975 };
    const BaselineLimit wide = BaselineLimitOf(two_speeds, 5);
    CHECK_TRUE(SUITE, "baseline: a case that runs at two speeds gets one and a half its spread",
               CHECK_NONE,
               wide.reference_us == 980u && wide.limit_percent == 63u && two_speeds[0] == 970u &&
                   two_speeds[4] == 1390u,
               "reference %u limit %u, sorted %u..%u", (unsigned)wide.reference_us,
               (unsigned)wide.limit_percent, (unsigned)two_speeds[0], (unsigned)two_speeds[4]);
    CHECK_TRUE(SUITE, "baseline: the slow speed is no move, past it is", CHECK_NONE,
               BaselineJudge(1390, wide) == BASELINE_SAME &&
                   BaselineJudge(1700, wide) == BASELINE_SLOWER,
               "1390 %s, 1700 %s", BaselineMoveWord(BaselineJudge(1390, wide)),
               BaselineMoveWord(BaselineJudge(1700, wide)));

    uint32_t even[] = { 40, 10, 30, 20 };
    const BaselineLimit lower = BaselineLimitOf(even, 4);
    CHECK_TRUE(SUITE, "baseline: of two middle samples the reference is the lower", CHECK_NONE,
               lower.reference_us == 20u, "reference %u", (unsigned)lower.reference_us);

    const BaselineLimit small = { 200, 20 };
    const BaselineLimit large = { 10000, 20 };
    CHECK_TRUE(SUITE, "baseline: a move under the floor is none, whatever its percentage",
               CHECK_NONE,
               BaselineJudge(290, small) == BASELINE_SAME &&
                   BaselineJudge(320, small) == BASELINE_SLOWER &&
                   BaselineJudge(12000, large) == BASELINE_SAME &&
                   BaselineJudge(12001, large) == BASELINE_SLOWER &&
                   BaselineJudge(7000, large) == BASELINE_FASTER,
               "290 %s, 320 %s, 12000 %s, 12001 %s, 7000 %s",
               BaselineMoveWord(BaselineJudge(290, small)),
               BaselineMoveWord(BaselineJudge(320, small)),
               BaselineMoveWord(BaselineJudge(12000, large)),
               BaselineMoveWord(BaselineJudge(12001, large)),
               BaselineMoveWord(BaselineJudge(7000, large)));
}

static void CheckLines(void) {
    char line[BASELINE_LINE_MAX];
    static BaselineFileLine read;
    // The name as a JSON string carries it: escaped once, by BaselineTake, and read back so.
    static const char ESCAPED[] = "a \\\"quoted\\\" \\\\ name";
    BaselineBuildCaseLine(line, sizeof(line), 4, "clear", ESCAPED, 231);
    const BaselineLineKind parsed = BaselineParseLine(line, &read);
    CHECK_TRUE(SUITE, "baseline: a case line reads back as written, quotes and all", CHECK_NONE,
               strcmp(line, "{\"sample\":4,\"suite\":\"clear\",\"name\":\"a \\\"quoted\\\" \\\\ "
                            "name\",\"us\":231}") == 0 &&
                   parsed == BASELINE_LINE_CASE && read.sample == 4u && read.us == 231u &&
                   strcmp(read.suite, "clear") == 0 && strcmp(read.name, ESCAPED) == 0,
               "%.200s read %d as %u %.32s %.120s %u", line, (int)parsed, (unsigned)read.sample,
               read.suite, read.name, (unsigned)read.us);

    BaselineBuildSampleLine(line, sizeof(line), 7, "v0.5.0-3-gabc", "eden", 412);
    const BaselineLineKind opener = BaselineParseLine(line, &read);
    CHECK_TRUE(SUITE,
               "baseline: the line that opens a sample reads back its number, build and cases",
               CHECK_NONE,
               strcmp(line, "{\"type\":\"sample\",\"sample\":7,\"build\":\"v0.5.0-3-gabc\","
                            "\"label\":\"eden\",\"cases\":412}") == 0 &&
                   opener == BASELINE_LINE_OPENS && read.sample == 7u && read.cases == 412u &&
                   strcmp(read.build, "v0.5.0-3-gabc") == 0 && strcmp(read.label, "eden") == 0,
               "%.300s read %d: sample %u cases %u build %.40s label %.40s", line, (int)opener,
               (unsigned)read.sample, (unsigned)read.cases, read.build, read.label);

    static const char *const NOT_OURS[] = {
        "",
        "{}",
        "{\"sample\":1,\"suite\":\"clear\"}",
        "{\"sample\":x,\"suite\":\"a\",\"name\":\"b\",\"us\":1}",
        // A file cut short while a sample was written: the name never ends, the number is cut
        // with no brace after it, or the opening line ends early.
        "{\"sample\":1,\"suite\":\"a\",\"name\":\"cut sh",
        "{\"sample\":4,\"suite\":\"s\",\"name\":\"X\",\"us\":1",
        "{\"sample\":4,\"suite\":\"s\",\"name\":\"X\",\"us\":1\n",
        "{\"type\":\"sample\",\"sample\":7,\"build\":\"b\",\"label\":\"\",\"cases\":4",
        // A cut line the next sample's opening line was written onto.
        "{\"sample\":6,\"suite\":\"s\",\"name\":\"X\",\"us\":10{\"type\":\"sample\",\"sample\":7,"
        "\"build\":\"b\",\"label\":\"\",\"cases\":1}\n",
        "{\"sample\":6,\"suite\":\"s\",\"name\":\"X\",\"us\":10} trailing",
        "{\"type\":\"samplezzz\",\"sample\":9}",
        "not json at all",
    };
    bool none = true;
    const char *taken = "";
    for (size_t i = 0; i < sizeof(NOT_OURS) / sizeof(NOT_OURS[0]); ++i) {
        if (BaselineParseLine(NOT_OURS[i], &read) != BASELINE_LINE_OTHER) {
            none = false;
            taken = NOT_OURS[i];
        }
    }
    CHECK_TRUE(SUITE, "baseline: a line cut short or not this program's is no line of a sample",
               CHECK_NONE, none, "read as one: %.300s", taken);

    const BaselineLimit limit = { 980, 63 };
    BaselineBuildHeldLine(line, sizeof(line), "console", "order", "the order case",
                          BASELINE_SLOWER, 1700, limit, 5);
    CHECK_TRUE(SUITE, "baseline: the report's line for a case held to its samples", CHECK_NONE,
               strcmp(line, "{\"type\":\"baseline\",\"baseline\":\"console\",\"suite\":\"order\","
                            "\"name\":\"the order case\",\"result\":\"slower\",\"us\":1700,"
                            "\"reference_us\":980,\"limit_percent\":63,\"floor_us\":100,"
                            "\"samples\":5}") == 0,
               "%.400s", line);
    BaselineTotals totals = { 5, 300, 2, 1, 12, 0, 0 };
    BaselineBuildSummaryLine(line, sizeof(line), "console", &totals);
    CHECK_TRUE(SUITE, "baseline: the report's line that sums a comparison up", CHECK_NONE,
               strcmp(line, "{\"type\":\"baseline_summary\",\"baseline\":\"console\",\"samples\":5,"
                            "\"compared\":300,\"slower\":2,\"faster\":1,\"too_few\":12}") == 0,
               "%.400s", line);
}

/// What a baseline wrote, read back here instead of going into the report this suite is part of.
#define CAPTURED_MAX 8u
typedef struct {
    size_t lines;
    size_t notes;
    char line[CAPTURED_MAX][REPORT_LINE_MAX];
    char note[CAPTURED_MAX][REPORT_NOTE_MAX + 1];
} Captured;

static Captured captured;

static void CaptureLine(void *context, const char *line) {
    Captured *const c = context;
    if (c->lines < CAPTURED_MAX) {
        snprintf(c->line[c->lines], sizeof(c->line[0]), "%s", line);
    }
    ++c->lines;
}

static void CaptureNote(void *context, const char *text) {
    Captured *const c = context;
    if (c->notes < CAPTURED_MAX) {
        snprintf(c->note[c->notes], sizeof(c->note[0]), "%s", text);
    }
    ++c->notes;
}

static const BaselineSink CAPTURE = { CaptureLine, CaptureNote, &captured };

static void CaptureReset(void) {
    memset(&captured, 0, sizeof(captured));
}

/// The first note captured that holds @p text, or NULL.
static const char *Noted(const char *text) {
    for (size_t i = 0; i < captured.notes && i < CAPTURED_MAX; ++i) {
        if (strstr(captured.note[i], text) != NULL) {
            return captured.note[i];
        }
    }
    return NULL;
}

/// The notes captured, one after the other, for a failing case's detail.
static const char *Notes(void) {
    static char all[512];
    all[0] = '\0';
    for (size_t i = 0; i < captured.notes && i < CAPTURED_MAX; ++i) {
        const size_t used = strlen(all);
        snprintf(all + used, sizeof(all) - used, "%s[%s]", i > 0u ? " " : "", captured.note[i]);
    }
    return all;
}

/// One made-up run of three timed cases, the second @p second microseconds.
static void TakeRun(uint32_t second) {
    BaselineReset();
    BaselineTake("clear", "clear to cyan", 230);
    BaselineTake("order", "indices before a wait", second);
    BaselineTake("fs", "reads a large file", 5000);
}

static void CheckFile(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/baseline-check.jsonl", scratch);
    (void)remove(path);

    BaselineTotals totals;
    const bool missing = BaselineCompareFile(path, "check", NULL, &totals) == BASELINE_NO_FILE;
    // Three runs of one machine, the order case at one of its two speeds each time.
    static const uint32_t SPEEDS[] = { 970, 1390, 980 };
    bool added = true;
    uint32_t numbers[3] = { 0 };
    uint32_t after = 0;
    for (size_t i = 0; i < 3; ++i) {
        TakeRun(SPEEDS[i]);
        added = BaselineAddFile(path, "build", "label", &numbers[i], &after) && added;
    }
    CHECK_TRUE(SUITE, "baseline: a missing file is made, and each run adds the next sample",
               CHECK_NONE,
               missing && added && numbers[0] == 1u && numbers[1] == 2u && numbers[2] == 3u &&
                   after == 3u,
               "missing read %d, added %d, samples %u %u %u, after %u", (int)missing, (int)added,
               (unsigned)numbers[0], (unsigned)numbers[1], (unsigned)numbers[2], (unsigned)after);

    TakeRun(1385);
    const bool same = BaselineCompareFile(path, "check", NULL, &totals) == BASELINE_READ;
    CHECK_TRUE(SUITE, "baseline: a run within its samples moves nothing", CHECK_NONE,
               same && totals.samples == 3u && totals.compared == 3u && totals.slower == 0u &&
                   totals.faster == 0u && totals.too_few == 0u,
               "read %d samples %u compared %zu slower %zu faster %zu too few %zu", (int)same,
               (unsigned)totals.samples, totals.compared, totals.slower, totals.faster,
               totals.too_few);

    BaselineReset();
    BaselineTake("clear", "clear to cyan", 230);
    BaselineTake("order", "indices before a wait", 2600);
    BaselineTake("fs", "reads a large file", 3000);
    BaselineTake("fill", "a case no sample has", 400);
    CaptureReset();
    const bool moved =
        BaselineCompareFile(path, "check", &CAPTURE, &totals) == BASELINE_READ;
    CHECK_TRUE(SUITE,
               "baseline: a slower case, a faster one and one with no samples are told apart",
               CHECK_NONE,
               moved && totals.compared == 3u && totals.slower == 1u && totals.faster == 1u &&
                   totals.too_few == 1u,
               "read %d compared %zu slower %zu faster %zu too few %zu", (int)moved,
               totals.compared, totals.slower, totals.faster, totals.too_few);
    // Every case held to its samples says how far it is, the ones that did not move too: a
    // console's baseline is read for the distance of each case, not only the ones past a limit.
    // The case with no samples has no line; the summary's too_few counts it.
    CHECK_TRUE(SUITE, "baseline: every case compared gets its line, a same one too", CHECK_NONE,
               captured.lines == 3u &&
                   strcmp(captured.line[0],
                          "{\"type\":\"baseline\",\"baseline\":\"check\",\"suite\":\"clear\","
                          "\"name\":\"clear to cyan\",\"result\":\"same\",\"us\":230,"
                          "\"reference_us\":230,\"limit_percent\":20,\"floor_us\":100,"
                          "\"samples\":3}") == 0 &&
                   strstr(captured.line[1], "\"result\":\"slower\",\"us\":2600,") != NULL &&
                   strstr(captured.line[2], "\"result\":\"faster\",\"us\":3000,") != NULL,
               "%zu lines: %.300s / %.150s", captured.lines, captured.line[0], captured.line[1]);

    BaselineReset();
    const bool gone = remove(path) == 0;
    CHECK_TRUE(SUITE, "baseline: this check takes its file away again", CHECK_NONE, gone,
               "%.400s is still there", path);
}

/// Names as suites write them: a tab, which a report line carries as a space, quotes and a
/// backslash, a line end, and one longer than a file keeps, which is cut. Each is held to its
/// samples only if the name read back from the file is the one this run kept.
static void CheckNames(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/names-check.jsonl", scratch);
    (void)remove(path);
    static char longest[300];
    memset(longest, 'n', sizeof(longest) - 1u);
    longest[sizeof(longest) - 1u] = '\0';
    static const char *const NAMES[] = {
        "a tab\there",
        "a \"quoted\" \\ name",
        "two\nlines",
        longest,
    };
    const size_t count = sizeof(NAMES) / sizeof(NAMES[0]);
    for (size_t sample = 0; sample < 3u; ++sample) {
        BaselineReset();
        for (size_t i = 0; i < count; ++i) {
            BaselineTake("order", NAMES[i], 1000u + (uint32_t)i);
        }
        uint32_t number = 0;
        uint32_t after = 0;
        (void)BaselineAddFile(path, "build", "", &number, &after);
    }
    BaselineReset();
    for (size_t i = 0; i < count; ++i) {
        BaselineTake("order", NAMES[i], 1000u + (uint32_t)i);
    }
    BaselineTotals totals;
    const bool read = BaselineCompareFile(path, "check", NULL, &totals) == BASELINE_READ;
    CHECK_TRUE(SUITE, "baseline: a name with a tab, quotes, a line end or past the cut is found",
               CHECK_NONE, read && totals.compared == count && totals.too_few == 0u,
               "read %d: compared %zu of %zu, too few %zu", (int)read, totals.compared, count,
               totals.too_few);
    BaselineReset();
    (void)remove(path);
}

/// A file at @p path of @p count samples, each of one case at @p us microseconds.
static void Seed(const char *path, size_t count, uint32_t us) {
    (void)remove(path);
    for (size_t i = 0; i < count; ++i) {
        BaselineReset();
        BaselineTake("order", "the case that slows", us);
        uint32_t sample = 0;
        uint32_t after = 0;
        (void)BaselineAddFile(path, "build", "", &sample, &after);
    }
}

/// The samples the file at @p path has, as a comparison reads them.
static uint32_t SamplesIn(const char *path) {
    BaselineTotals totals;
    return BaselineCompareFile(path, "check", NULL, &totals) == BASELINE_READ ? totals.samples
                                                                              : 0u;
}

/// Appends @p text to the file at @p path as it is, for a file a write was cut short in.
static void Append(const char *path, const char *text) {
    FILE *const file = fopen(path, "a");
    if (file != NULL) {
        fputs(text, file);
        fclose(file);
    }
}

/// A file a write was cut short in, the program ending inside a sample: the cut line's number
/// is a fraction of the case's time, and the next sample's first line lands on the same line.
/// Five samples at ten milliseconds, a sixth cut at "us":10, a seventh added after it, and a run
/// at two and a half times: read as it was, the 10 widened the case's limit past the run.
static void CheckCut(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/cut-check.jsonl", scratch);
    Seed(path, 5, 10000);
    Append(path, "{\"type\":\"sample\",\"sample\":6,\"build\":\"build\",\"label\":\"\","
                 "\"cases\":1}\n{\"sample\":6,\"suite\":\"order\",\"name\":\"the case that "
                 "slows\",\"us\":10");
    BaselineReset();
    BaselineTake("order", "the case that slows", 10000);
    uint32_t number = 0;
    uint32_t after = 0;
    (void)BaselineAddFile(path, "build", "", &number, &after);
    BaselineReset();
    BaselineTake("order", "the case that slows", 25000);
    BaselineTotals totals;
    const bool read = BaselineCompareFile(path, "check", NULL, &totals) == BASELINE_READ;
    CHECK_TRUE(SUITE, "baseline: a line cut short is no case, and hides no slower run",
               CHECK_NONE, read && totals.compared == 1u && totals.slower == 1u,
               "read %d compared %zu slower %zu samples %u", (int)read, totals.compared,
               totals.slower, (unsigned)totals.samples);
    BaselineReset();
    (void)remove(path);
}

/// A sample is all its cases or none: one short of the cases its first line said, one that
/// opened with none, and one whose opening line went onto a cut line are left out, each said
/// with its number and counts. Here each would have hidden a run two and a half times slower:
/// the short one with the cut sample's 10, the one with no opening line with a run of its own.
static void CheckShort(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/short-check.jsonl", scratch);
    Seed(path, 5, 10000);
    Append(path, "{\"type\":\"sample\",\"sample\":6,\"build\":\"build\",\"label\":\"\","
                 "\"cases\":2}\n{\"sample\":6,\"suite\":\"order\",\"name\":\"the case that "
                 "slows\",\"us\":10}\n{\"sample\":6,\"suite\":\"order\",\"name\":\"the next "
                 "on\"\n"
                 "{\"type\":\"sample\",\"sample\":7,\"build\":\"build\",\"label\":\"\","
                 "\"cases\":0}\n"
                 "{\"type\":\"sample\",\"sample\":8,\"build\":\"build\",\"label\":\"\","
                 "\"cases\":1}\n{\"sample\":8,\"suite\":\"order\",\"name\":\"the case that "
                 "slows\",\"us\":10{\"type\":\"sample\",\"sample\":9,\"build\":\"build\","
                 "\"label\":\"\",\"cases\":1}\n{\"sample\":9,\"suite\":\"order\",\"name\":"
                 "\"the case that slows\",\"us\":25000}\n");
    BaselineReset();
    BaselineTake("order", "the case that slows", 25000);
    BaselineTotals totals;
    CaptureReset();
    const bool read =
        BaselineCompareFile(path, "short-check", &CAPTURE, &totals) == BASELINE_READ;
    CHECK_TRUE(SUITE, "baseline: a sample short of the cases it opened with is left out, and said",
               CHECK_NONE,
               read && totals.samples == 5u && totals.compared == 1u && totals.slower == 1u &&
                   Noted("compare=short-check: sample 6 of the file is left out: it opened with 2 "
                         "cases and 1 whole line of it was read") != NULL &&
                   Noted("sample 7 of the file is left out: it opened with 0 cases") != NULL &&
                   Noted("sample 8 of the file is left out: it opened with 1 case and 0 whole "
                         "lines of it were read") != NULL &&
                   Noted("sample 9 of the file is left out: no line opening it was read, and 1 "
                         "whole line of it was") != NULL,
               "read %d samples %u compared %zu slower %zu: %.300s", (int)read,
               (unsigned)totals.samples, totals.compared, totals.slower, Notes());
    BaselineReset();
    (void)remove(path);
}

/// More samples of a case than BASELINE_SAMPLES_MAX: it is held to its newest, and the note says
/// how many there were and how many count. Six old samples at a tenth of a second, then the 64
/// newest at a millisecond, and a run back at a tenth: held to the newest it is slower; held to
/// the oldest, the six would have stretched its limit past it.
static void CheckNewest(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/newest-check.jsonl", scratch);
    FILE *const file = fopen(path, "w");
    const uint32_t total = BASELINE_SAMPLES_MAX + 6u;
    char line[BASELINE_LINE_MAX];
    for (uint32_t sample = 1; file != NULL && sample <= total; ++sample) {
        BaselineBuildSampleLine(line, sizeof(line), sample, "build", "", 1);
        fprintf(file, "%s\n", line);
        BaselineBuildCaseLine(line, sizeof(line), sample, "order", "the case that slows",
                              sample <= 6u ? 100000u : 1000u);
        fprintf(file, "%s\n", line);
    }
    if (file != NULL) {
        fclose(file);
    }
    BaselineReset();
    BaselineTake("order", "the case that slows", 100000);
    BaselineTotals totals;
    CaptureReset();
    const bool read =
        BaselineCompareFile(path, "newest-check", &CAPTURE, &totals) == BASELINE_READ;
    CHECK_TRUE(SUITE, "baseline: a case with more than 64 samples is held to its newest, and said",
               CHECK_NONE,
               read && totals.samples == total && totals.compared == 1u && totals.slower == 1u &&
                   Noted("compare=newest-check: 1 case has more samples in the file than the 64 a "
                         "case is held to (70 the most), so each is held to its newest 64") != NULL,
               "read %d samples %u compared %zu slower %zu: %.300s", (int)read,
               (unsigned)totals.samples, totals.compared, totals.slower, Notes());
    BaselineReset();
    (void)remove(path);
}

/// Samples of more than one edenprobe build: a case's time moves with the probe's own code as well
/// as with the machine, so the comparison says how many builds and the first two.
static void CheckBuilds(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/builds-check.jsonl", scratch);
    (void)remove(path);
    static const char *const BUILDS[] = { "v0.5.0", "v0.5.0", "v0.5.0-3-gabc", "v0.6.0" };
    for (size_t i = 0; i < sizeof(BUILDS) / sizeof(BUILDS[0]); ++i) {
        BaselineReset();
        BaselineTake("order", "the case that slows", 1000);
        uint32_t number = 0;
        uint32_t after = 0;
        (void)BaselineAddFile(path, BUILDS[i], "", &number, &after);
    }
    BaselineReset();
    BaselineTake("order", "the case that slows", 1000);
    BaselineTotals totals;
    CaptureReset();
    const bool read =
        BaselineCompareFile(path, "builds-check", &CAPTURE, &totals) == BASELINE_READ;
    const bool several =
        Noted("compare=builds-check: the file's 4 samples come from 3 edenprobe builds, v0.5.0 "
              "first and v0.5.0-3-gabc second") != NULL;
    // One build, the usual file, says nothing about it.
    Seed(path, 3, 1000);
    BaselineReset();
    BaselineTake("order", "the case that slows", 1000);
    CaptureReset();
    const bool read_one =
        BaselineCompareFile(path, "builds-check", &CAPTURE, &totals) == BASELINE_READ;
    const bool quiet = Noted("edenprobe build") == NULL;
    CHECK_TRUE(SUITE, "baseline: samples of more than one build say how many and the first two",
               CHECK_NONE, read && several && read_one && quiet, "several %d, one quiet %d: %.300s",
               (int)several, (int)quiet, Notes());
    BaselineReset();
    (void)remove(path);
}

#if !defined(__SWITCH__) && defined(__GLIBC__)
/// A file that is there and cannot be read is not a missing one: a directory where the file
/// should be (its read fails) and a path through a file (its open fails) are each said with
/// the error, compared with nothing, and not added to, since a sample written after a file it
/// could not read could repeat a number or land on a cut line. On a computer only: what a
/// console's memory card answers for either is not known here.
static void CheckUnreadable(const char *scratch) {
    char directory[512];
    snprintf(directory, sizeof(directory), "%.400s/dir-check.jsonl", scratch);
    const bool made = mkdir(directory, 0777) == 0;
    BaselineReset();
    BaselineTake("order", "the case that slows", 1000);
    CaptureReset();
    const BaselineOutcome in_directory = BaselineFinish(scratch, "dir-check", "dir-check", "build",
                                                        "", true, false, &CAPTURE);
    char said_directory[160];
    snprintf(said_directory, sizeof(said_directory), "could not be read (%s), so nothing was "
             "compared", strerror(EISDIR));
    char refused_directory[160];
    snprintf(refused_directory, sizeof(refused_directory), "could not be read (%s), so no "
             "sample was added", strerror(EISDIR));
    const bool directory_said = Noted(said_directory) != NULL && Noted(refused_directory) != NULL;
    const bool removed = rmdir(directory) == 0;

    char file[512];
    snprintf(file, sizeof(file), "%.400s/file-check", scratch);
    FILE *const plain = fopen(file, "w");
    if (plain != NULL) {
        fclose(plain);
    }
    CaptureReset();
    const BaselineOutcome through = BaselineFinish(file, "x", "x", "build", "", true, false,
                                                   &CAPTURE);
    char said_through[160];
    snprintf(said_through, sizeof(said_through), "could not be read (%s), so nothing was "
             "compared", strerror(ENOTDIR));
    const bool through_said = Noted(said_through) != NULL;
    const bool file_removed = remove(file) == 0;
    BaselineReset();
    CHECK_TRUE(SUITE,
               "baseline: a file that is there and cannot be read is said, not taken as none",
               CHECK_NONE,
               made && removed && plain != NULL && file_removed && !in_directory.compared &&
                   !in_directory.added && directory_said && !through.compared && !through.added &&
                   through_said,
               "directory: compared %d added %d said %d; through a file: compared %d said %d: "
               "%.300s",
               (int)in_directory.compared, (int)in_directory.added, (int)directory_said,
               (int)through.compared, (int)through_said, Notes());
}
#endif

/// Whether a line of the file at @p path opens sample @p sample, read whole.
static bool Opens(const char *path, uint32_t sample) {
    FILE *const file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    static char line[BASELINE_LINE_MAX + 2u];
    static BaselineFileLine read;
    bool found = false;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        found = BaselineParseLine(line, &read) == BASELINE_LINE_OPENS && read.sample == sample;
    }
    fclose(file);
    return found;
}

/// A file whose last line a write was cut short in has no line end: the next sample's first line
/// would land on it and be lost with it, so one is written first, and said.
static void CheckLineEnd(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/end-check.jsonl", scratch);
    Seed(path, 3, 1000);
    Append(path, "{\"type\":\"sample\",\"sample\":4,\"build\":\"build\",\"label\":\"\","
                 "\"cases\":1}\n{\"sample\":4,\"suite\":\"order\",\"name\":\"the case that "
                 "slows\",\"us\":10");
    BaselineReset();
    BaselineTake("order", "the case that slows", 1000);
    CaptureReset();
    const BaselineOutcome outcome =
        BaselineFinish(scratch, "end-check", NULL, "build", "", true, false, &CAPTURE);
    CHECK_TRUE(SUITE, "baseline: a file cut short gets its line end before the next sample",
               CHECK_NONE,
               outcome.added && outcome.sample == 5u && Opens(path, 5u) &&
                   Noted("baseline=end-check: the last line of ") != NULL &&
                   Noted("had no line end, so one was written before sample 5") != NULL &&
                   Noted("which now has 4 whole and 1 left out") != NULL,
               "added %d as %u, sample 5 opens %d: %.300s", (int)outcome.added,
               (unsigned)outcome.sample, (int)Opens(path, 5u), Notes());
    BaselineReset();
    (void)remove(path);
}

/// What BaselineFinish does with both words, with a run that went on after a crash, and with
/// notime: the order that keeps a run from being held to itself, and the two runs that add nothing.
static void CheckFinish(const char *scratch) {
    char path[512];
    snprintf(path, sizeof(path), "%.400s/finish-check.jsonl", scratch);

    // Three samples at a millisecond and a run at five: held to the three, it is slower; held to
    // four that include itself, its own sample would have widened the limit past it.
    Seed(path, 3, 1000);
    BaselineReset();
    BaselineTake("order", "the case that slows", 5000);
    CaptureReset();
    const BaselineOutcome both = BaselineFinish(scratch, "finish-check", "finish-check", "build",
                                                "", true, false, &CAPTURE);
    CHECK_TRUE(SUITE,
               "baseline: a run with both words is held to the samples before it, then added",
               CHECK_NONE,
               both.compared && both.totals.samples == 3u && both.totals.slower == 1u &&
                   both.added && both.sample == 4u && both.samples_after == 4u &&
                   SamplesIn(path) == 4u,
               "compared %d samples %u slower %zu, added %d as %u, after %u: %.300s",
               (int)both.compared, (unsigned)both.totals.samples, both.totals.slower,
               (int)both.added, (unsigned)both.sample, (unsigned)both.samples_after, Notes());

    Seed(path, 3, 1000);
    BaselineReset();
    BaselineTake("order", "the case that slows", 5000);
    CaptureReset();
    const BaselineOutcome resumed = BaselineFinish(scratch, "finish-check", "finish-check",
                                                   "build", "", true, true, &CAPTURE);
    CHECK_TRUE(SUITE, "baseline: a run that went on after the program ended is compared, not added",
               CHECK_NONE,
               resumed.compared && resumed.totals.slower == 1u && !resumed.added &&
                   SamplesIn(path) == 3u &&
                   Noted("this launch has only 1 of its timed cases, so no sample was added") !=
                       NULL,
               "compared %d slower %zu added %d, file %u samples: %.300s",
               (int)resumed.compared, resumed.totals.slower, (int)resumed.added,
               (unsigned)SamplesIn(path), Notes());

    BaselineReset();
    BaselineTake("order", "the case that slows", 5000);
    CaptureReset();
    const BaselineOutcome untimed = BaselineFinish(scratch, "finish-check", "finish-check",
                                                   "build", "", false, false, &CAPTURE);
    CHECK_TRUE(SUITE, "baseline: a run with notime compares and adds nothing, and says both",
               CHECK_NONE,
               !untimed.compared && !untimed.added && untimed.notes == 2u &&
                   captured.lines == 0u && SamplesIn(path) == 3u &&
                   Noted("baseline=finish-check: this run has notime") != NULL &&
                   Noted("compare=finish-check: this run has notime") != NULL,
               "compared %d added %d, %zu notes %zu lines, file %u samples: %.300s",
               (int)untimed.compared, (int)untimed.added, untimed.notes, captured.lines,
               (unsigned)SamplesIn(path), Notes());

    // A run of families that time nothing, cpu alone say, has no sample to give: an empty one
    // would only make the file's count of samples say more runs than any case has.
    char empty[512];
    snprintf(empty, sizeof(empty), "%.400s/empty-check.jsonl", scratch);
    (void)remove(empty);
    BaselineReset();
    CaptureReset();
    const BaselineOutcome none = BaselineFinish(scratch, "empty-check", NULL, "build", "", true,
                                                false, &CAPTURE);
    struct stat made;
    const bool no_file = stat(empty, &made) != 0;
    CHECK_TRUE(SUITE, "baseline: a run that timed no case adds no sample, and says so", CHECK_NONE,
               !none.added && no_file &&
                   Noted("baseline=empty-check: this run timed no case") != NULL,
               "added %d as %u, file made %d: %.300s", (int)none.added, (unsigned)none.sample,
               (int)!no_file, Notes());
    (void)remove(empty);

    BaselineReset();
    const bool gone = remove(path) == 0;
    CHECK_TRUE(SUITE, "baseline: the run checks take their file away again", CHECK_NONE, gone,
               "%.400s is still there", path);
}

/// `baseline=` and `compare=` take a name by the label's rules, are said in the mode line, kept
/// when the start screen saves the file, and stay out of the key.
static void CheckMode(void) {
    char said[REPORT_NOTE_MAX + 1];
    const RunMode both = ModeFromText("quick baseline=console compare=Eden_1.2");
    ModeDescribe(said, sizeof(said), &both, MODE_FAMILIES_PRESENT);
    CHECK_TRUE(SUITE, "baseline: the two names among the words, and the mode line", CHECK_NONE,
               strcmp(both.baseline.name, "console") == 0 &&
                   strcmp(both.compare.name, "Eden_1.2") == 0 && both.quick &&
                   both.unknown_count == 0u &&
                   strstr(said, "; baseline console, this run's times added as a sample; "
                                "compare Eden_1.2, this run's times held to its samples") != NULL,
               "%.400s", said);

    const RunMode bad = ModeFromText("baseline=../x compare=");
    ModeDescribe(said, sizeof(said), &bad, MODE_FAMILIES_PRESENT);
    CHECK_TRUE(SUITE, "baseline: a name a file cannot carry is refused and said", CHECK_NONE,
               bad.baseline.name[0] == '\0' && bad.baseline.was_refused &&
                   bad.compare.name[0] == '\0' && bad.compare.was_refused &&
                   strstr(said, "; baseline refused: \"../x\", a file name takes") != NULL &&
                   strstr(said, "; compare refused: \"\", a file name takes") != NULL,
               "%.400s", said);

    char key_plain[128];
    char key_both[128];
    const RunMode plain = ModeFromText("quick");
    ModeKey(key_plain, sizeof(key_plain), &plain);
    ModeKey(key_both, sizeof(key_both), &both);
    char saved[256];
    ModeFormat(saved, sizeof(saved), &both);
    CHECK_TRUE(SUITE, "baseline: the names stay out of the key, and the start screen keeps them",
               CHECK_NONE,
               strcmp(key_plain, key_both) == 0 &&
                   strcmp(saved, "quick baseline=console compare=Eden_1.2") == 0,
               "key \"%.100s\" against \"%.100s\", saved \"%.200s\"", key_both, key_plain,
               saved);

    // The limit falling inside a name drops it, as it does a label's.
    static const char LONG[] = "quick compare=abcdef more";
    const RunMode cut = ModeFromFile(MODE_SOURCE_FILE, LONG, 17u, true);
    const RunMode whole = ModeFromFile(MODE_SOURCE_FILE, LONG, 20u, true);
    CHECK_TRUE(SUITE, "baseline: a name cut by the file's limit is dropped", CHECK_NONE,
               cut.quick && cut.compare.name[0] == '\0' && !cut.compare.was_refused &&
                   cut.unknown_count == 0u && strcmp(whole.compare.name, "abcdef") == 0,
               "cut \"%s\" whole \"%s\"", cut.compare.name, whole.compare.name);
}

void RunSupportBaseline(const char *scratch) {
    CheckLimit();
    CheckLines();
    CheckMode();
    CheckFile(scratch);
    CheckFinish(scratch);
    CheckNames(scratch);
    CheckCut(scratch);
    CheckLineEnd(scratch);
    CheckShort(scratch);
    CheckBuilds(scratch);
    CheckNewest(scratch);
#if !defined(__SWITCH__) && defined(__GLIBC__)
    CheckUnreadable(scratch);
#endif
}
