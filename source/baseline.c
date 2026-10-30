// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "baseline.h"

#include "report.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/// One timed case of this run, and, while a comparison reads its file, the samples it has there.
typedef struct {
    char suite[BASELINE_SUITE_MAX];
    char name[BASELINE_NAME_MAX];
    uint32_t us;
    uint32_t samples[BASELINE_SAMPLES_MAX];
    uint32_t sample_count;
} KeptCase;

/// Static, as every buffer here: a run's cases fit, and nothing is allocated between suites.
static KeptCase kept[BASELINE_CASES_MAX];
static size_t kept_count;
static size_t dropped_count;

/// Where each kept case is found by its names: an open-addressed table of indices into kept,
/// twice its size so a search stops soon. Zero is empty, so an index is stored plus one.
#define BASELINE_SLOTS (BASELINE_CASES_MAX * 2u)
static uint32_t slots[BASELINE_SLOTS];

static uint32_t HashOf(const char *suite, const char *name) {
    // FNV-1a over both names, a zero byte between so "ab"+"c" and "a"+"bc" differ.
    uint32_t h = 2166136261u;
    for (const char *p = suite; *p != '\0'; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    h = (h ^ 0u) * 16777619u;
    for (const char *p = name; *p != '\0'; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h;
}

/// The kept case named @p suite and @p name, both already cut to fit, or NULL.
static KeptCase *Find(const char *suite, const char *name) {
    uint32_t i = HashOf(suite, name) % BASELINE_SLOTS;
    for (uint32_t probes = 0; probes < BASELINE_SLOTS && slots[i] != 0u; ++probes) {
        KeptCase *const c = &kept[slots[i] - 1u];
        if (strcmp(c->suite, suite) == 0 && strcmp(c->name, name) == 0) {
            return c;
        }
        i = (i + 1u) % BASELINE_SLOTS;
    }
    return NULL;
}

/// Copies @p text into @p out, cut to fit: a name too long for the file is cut the same way when
/// it is kept and when it is read back, so the two still meet.
static void CopyCut(char *out, size_t out_size, const char *text) {
    snprintf(out, out_size, "%s", text != NULL ? text : "");
}

void BaselineReset(void) {
    kept_count = 0;
    dropped_count = 0;
    memset(slots, 0, sizeof(slots));
}

void BaselineTake(const char *suite, const char *name, uint32_t us) {
    char s[BASELINE_SUITE_MAX];
    char n[BASELINE_NAME_MAX];
    CopyCut(s, sizeof(s), suite);
    CopyCut(n, sizeof(n), name);
    KeptCase *const known = Find(s, n);
    if (known != NULL) {
        // Two lines of one case in one run (a suite that reports a name twice): the last one is
        // the run's, as it is the last a reader of the report sees.
        known->us = us;
        return;
    }
    if (kept_count >= BASELINE_CASES_MAX) {
        ++dropped_count;
        return;
    }
    KeptCase *const c = &kept[kept_count];
    memcpy(c->suite, s, sizeof(s));
    memcpy(c->name, n, sizeof(n));
    c->us = us;
    c->sample_count = 0;
    uint32_t i = HashOf(s, n) % BASELINE_SLOTS;
    while (slots[i] != 0u) {
        i = (i + 1u) % BASELINE_SLOTS;
    }
    slots[i] = (uint32_t)(kept_count + 1u);
    ++kept_count;
}

size_t BaselineKept(void) {
    return kept_count;
}

size_t BaselineDropped(void) {
    return dropped_count;
}

BaselineLimit BaselineLimitOf(uint32_t *samples, size_t count) {
    // Insertion sort: a case has at most BASELINE_SAMPLES_MAX of them.
    for (size_t i = 1; i < count; ++i) {
        const uint32_t v = samples[i];
        size_t j = i;
        for (; j > 0u && samples[j - 1u] > v; --j) {
            samples[j] = samples[j - 1u];
        }
        samples[j] = v;
    }
    BaselineLimit limit;
    // The middle one, or the lower of the two middle ones, as the perf line's median is.
    limit.reference_us = samples[(count - 1u) / 2u];
    const uint32_t lowest = samples[0];
    const uint32_t highest = samples[count - 1u];
    const uint32_t reference = limit.reference_us;
    const uint32_t farthest =
        highest - reference > reference - lowest ? highest - reference : reference - lowest;
    // Percent of the reference, rounded up, so a spread never reads smaller than it was.
    const uint64_t spread_percent =
        reference == 0u ? 0u : ((uint64_t)farthest * 100u + reference - 1u) / reference;
    const uint64_t from_spread = (spread_percent * BASELINE_SPREAD_TENTHS + 9u) / 10u;
    // Capped where a sample a thousand times another's has already said the case is noise.
    limit.limit_percent = from_spread > BASELINE_THRESHOLD_PERCENT
                              ? (uint32_t)(from_spread > 100000u ? 100000u : from_spread)
                              : BASELINE_THRESHOLD_PERCENT;
    return limit;
}

BaselineMove BaselineJudge(uint32_t us, BaselineLimit limit) {
    const uint32_t reference = limit.reference_us;
    const uint32_t distance = us > reference ? us - reference : reference - us;
    const uint64_t allowed = (uint64_t)reference * limit.limit_percent / 100u;
    if (distance <= BASELINE_FLOOR_US || (uint64_t)distance <= allowed) {
        return BASELINE_SAME;
    }
    return us > reference ? BASELINE_SLOWER : BASELINE_FASTER;
}

const char *BaselineMoveWord(BaselineMove move) {
    switch (move) {
    case BASELINE_SLOWER:
        return "slower";
    case BASELINE_FASTER:
        return "faster";
    default:
        return "same";
    }
}

/// A whole line or an empty one, as the report's builders do: a line cut short would end inside
/// a string and take the rest of the file down with it.
static size_t Finish(char *out, size_t out_size, int wrote) {
    if (wrote < 0 || (size_t)wrote >= out_size) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)wrote;
}

size_t BaselineBuildSampleLine(char *out, size_t out_size, uint32_t sample, const char *build,
                               const char *label, size_t cases) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    char safe_build[96];
    char safe_label[96];
    ReportEscape(safe_build, sizeof(safe_build), build != NULL ? build : "");
    ReportEscape(safe_label, sizeof(safe_label), label != NULL ? label : "");
    return Finish(out, out_size,
                  snprintf(out, out_size,
                           "{\"type\":\"sample\",\"sample\":%u,\"build\":\"%s\",\"label\":\"%s\","
                           "\"cases\":%zu}",
                           (unsigned)sample, safe_build, safe_label, cases));
}

size_t BaselineBuildCaseLine(char *out, size_t out_size, uint32_t sample, const char *suite,
                             const char *name, uint32_t us) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    char safe_suite[BASELINE_SUITE_MAX * 2u];
    char safe_name[BASELINE_NAME_MAX * 2u];
    ReportEscape(safe_suite, sizeof(safe_suite), suite != NULL ? suite : "");
    ReportEscape(safe_name, sizeof(safe_name), name != NULL ? name : "");
    return Finish(out, out_size,
                  snprintf(out, out_size,
                           "{\"sample\":%u,\"suite\":\"%s\",\"name\":\"%s\",\"us\":%u}",
                           (unsigned)sample, safe_suite, safe_name, (unsigned)us));
}

/// The number after @p key in @p line, which must be there: true with it in @p out.
static bool NumberAfter(const char *line, const char *key, uint32_t *out) {
    const char *at = strstr(line, key);
    if (at == NULL) {
        return false;
    }
    at += strlen(key);
    if (*at < '0' || *at > '9') {
        return false;
    }
    uint64_t value = 0;
    for (; *at >= '0' && *at <= '9'; ++at) {
        value = value * 10u + (uint64_t)(*at - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    *out = (uint32_t)value;
    return true;
}

/// The string after @p key in @p line, its escapes undone and cut to fit @p out: true when it is
/// there and ends.
static bool StringAfter(const char *line, const char *key, char *out, size_t out_size) {
    const char *at = strstr(line, key);
    if (at == NULL || out_size == 0u) {
        return false;
    }
    at += strlen(key);
    size_t w = 0;
    for (; *at != '\0' && *at != '"'; ++at) {
        char c = *at;
        if (c == '\\') {
            ++at;
            if (*at == '\0') {
                return false;
            }
            c = *at == 'n' ? '\n' : *at;
        }
        if (w + 1u < out_size) {
            out[w++] = c;
        }
    }
    out[w] = '\0';
    return *at == '"';
}

bool BaselineParseLine(const char *line, uint32_t *sample, char *suite, size_t suite_size,
                       char *name, size_t name_size, uint32_t *us, uint32_t *opens_sample) {
    if (opens_sample != NULL) {
        *opens_sample = 0;
    }
    if (line == NULL) {
        return false;
    }
    uint32_t number = 0;
    if (strncmp(line, "{\"type\":\"sample\",", 16u) == 0) {
        if (opens_sample != NULL && NumberAfter(line, "\"sample\":", &number)) {
            *opens_sample = number;
        }
        return false;
    }
    if (strncmp(line, "{\"sample\":", 10u) != 0) {
        return false;
    }
    uint32_t time = 0;
    if (!NumberAfter(line, "{\"sample\":", &number) ||
        !StringAfter(line, "\"suite\":\"", suite, suite_size) ||
        !StringAfter(line, "\"name\":\"", name, name_size) ||
        !NumberAfter(line, "\"us\":", &time)) {
        return false;
    }
    *sample = number;
    *us = time;
    return true;
}

size_t BaselineBuildMovedLine(char *out, size_t out_size, const char *baseline, const char *suite,
                              const char *name, BaselineMove move, uint32_t us,
                              BaselineLimit limit, size_t samples) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    char safe_baseline[96];
    char safe_suite[BASELINE_SUITE_MAX * 2u];
    char safe_name[BASELINE_NAME_MAX * 2u];
    ReportEscape(safe_baseline, sizeof(safe_baseline), baseline != NULL ? baseline : "");
    ReportEscape(safe_suite, sizeof(safe_suite), suite != NULL ? suite : "");
    ReportEscape(safe_name, sizeof(safe_name), name != NULL ? name : "");
    return Finish(out, out_size,
                  snprintf(out, out_size,
                           "{\"type\":\"baseline\",\"baseline\":\"%s\",\"suite\":\"%s\","
                           "\"name\":\"%s\",\"result\":\"%s\",\"us\":%u,\"reference_us\":%u,"
                           "\"limit_percent\":%u,\"samples\":%zu}",
                           safe_baseline, safe_suite, safe_name, BaselineMoveWord(move),
                           (unsigned)us, (unsigned)limit.reference_us,
                           (unsigned)limit.limit_percent, samples));
}

size_t BaselineBuildSummaryLine(char *out, size_t out_size, const char *baseline,
                                const BaselineTotals *totals) {
    if (out == NULL || out_size == 0u || totals == NULL) {
        if (out != NULL && out_size > 0u) {
            out[0] = '\0';
        }
        return 0;
    }
    char safe_baseline[96];
    ReportEscape(safe_baseline, sizeof(safe_baseline), baseline != NULL ? baseline : "");
    return Finish(out, out_size,
                  snprintf(out, out_size,
                           "{\"type\":\"baseline_summary\",\"baseline\":\"%s\",\"samples\":%u,"
                           "\"compared\":%zu,\"slower\":%zu,\"faster\":%zu,\"too_few\":%zu}",
                           safe_baseline, (unsigned)totals->samples, totals->compared,
                           totals->slower, totals->faster, totals->too_few));
}

/// @p directory/@p name.jsonl into @p out.
static void PathOf(char *out, size_t out_size, const char *directory, const char *name) {
    snprintf(out, out_size, "%s/%s.jsonl", directory, name);
}

/// Reads @p path, giving each kept case the samples it has there and counting the samples the
/// file opens; @p highest gets the largest sample number seen. False when it cannot be opened.
static bool ReadFile(const char *path, uint32_t *samples_in_file, uint32_t *highest,
                     bool fill_cases) {
    *samples_in_file = 0;
    *highest = 0;
    FILE *const file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    if (fill_cases) {
        for (size_t i = 0; i < kept_count; ++i) {
            kept[i].sample_count = 0;
        }
    }
    static char line[BASELINE_LINE_MAX + 2u];
    while (fgets(line, sizeof(line), file) != NULL) {
        uint32_t sample = 0;
        uint32_t us = 0;
        uint32_t opens = 0;
        char suite[BASELINE_SUITE_MAX];
        char name[BASELINE_NAME_MAX];
        if (!BaselineParseLine(line, &sample, suite, sizeof(suite), name, sizeof(name), &us,
                               &opens)) {
            if (opens > 0u) {
                ++*samples_in_file;
                *highest = opens > *highest ? opens : *highest;
            }
            continue;
        }
        *highest = sample > *highest ? sample : *highest;
        if (!fill_cases) {
            continue;
        }
        KeptCase *const c = Find(suite, name);
        if (c == NULL) {
            continue;
        }
        if (c->sample_count < BASELINE_SAMPLES_MAX) {
            c->samples[c->sample_count++] = us;
        } else {
            // Full: the newest samples are the ones a machine's state is closest to, so the
            // oldest goes.
            memmove(c->samples, c->samples + 1, sizeof(c->samples) - sizeof(c->samples[0]));
            c->samples[BASELINE_SAMPLES_MAX - 1u] = us;
        }
    }
    fclose(file);
    return true;
}

static void LineToReport(void *context, const char *line) {
    (void)context;
    ReportLine(line);
}

static void NoteToReport(void *context, const char *text) {
    (void)context;
    ReportNote("%s", text);
}

const BaselineSink *BaselineToReport(void) {
    static const BaselineSink sink = { LineToReport, NoteToReport, NULL };
    return &sink;
}

static void SinkLine(const BaselineSink *sink, const char *line) {
    if (sink != NULL && sink->line != NULL) {
        sink->line(sink->context, line);
    }
}

/// A note to @p sink, counted in @p count, cut where the report would cut it.
static void SinkNote(const BaselineSink *sink, size_t *count, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void SinkNote(const BaselineSink *sink, size_t *count, const char *fmt, ...) {
    char text[REPORT_NOTE_MAX + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (count != NULL) {
        ++*count;
    }
    if (sink != NULL && sink->note != NULL) {
        sink->note(sink->context, text);
    }
}

bool BaselineCompareFile(const char *path, const char *name, const BaselineSink *sink,
                         BaselineTotals *totals) {
    memset(totals, 0, sizeof(*totals));
    uint32_t highest = 0;
    if (!ReadFile(path, &totals->samples, &highest, true)) {
        return false;
    }
    char line[REPORT_LINE_MAX];
    for (size_t i = 0; i < kept_count; ++i) {
        KeptCase *const c = &kept[i];
        if (c->sample_count < BASELINE_MIN_SAMPLES) {
            ++totals->too_few;
            continue;
        }
        const size_t samples = c->sample_count;
        const BaselineLimit limit = BaselineLimitOf(c->samples, samples);
        const BaselineMove move = BaselineJudge(c->us, limit);
        ++totals->compared;
        if (move == BASELINE_SAME) {
            continue;
        }
        if (move == BASELINE_SLOWER) {
            ++totals->slower;
        } else {
            ++totals->faster;
        }
        if (BaselineBuildMovedLine(line, sizeof(line), name, c->suite, c->name, move, c->us,
                                   limit, samples) > 0u) {
            SinkLine(sink, line);
        }
    }
    return true;
}

bool BaselineAddFile(const char *path, const char *build, const char *label, uint32_t *sample,
                     uint32_t *samples_after) {
    uint32_t samples = 0;
    uint32_t highest = 0;
    // A missing file is a new baseline: its first sample is number one.
    (void)ReadFile(path, &samples, &highest, false);
    *sample = highest + 1u;
    *samples_after = samples;
    FILE *const file = fopen(path, "a");
    if (file == NULL) {
        return false;
    }
    static char line[BASELINE_LINE_MAX + 2u];
    bool written = BaselineBuildSampleLine(line, sizeof(line), *sample, build, label, kept_count) >
                       0u &&
                   fprintf(file, "%s\n", line) > 0;
    for (size_t i = 0; written && i < kept_count; ++i) {
        written = BaselineBuildCaseLine(line, sizeof(line), *sample, kept[i].suite, kept[i].name,
                                        kept[i].us) > 0u &&
                  fprintf(file, "%s\n", line) > 0;
    }
    written = fclose(file) == 0 && written;
    if (written) {
        *samples_after = samples + 1u;
    }
    return written;
}

static void Compare(const char *directory, const char *name, const BaselineSink *sink,
                    BaselineOutcome *outcome) {
    char path[256];
    PathOf(path, sizeof(path), directory, name);
    if (!BaselineCompareFile(path, name, sink, &outcome->totals)) {
        SinkNote(sink, &outcome->notes,
                 "compare=%s: no baseline file at %s (%s), so nothing was compared; "
                 "baseline=%s in the mode file makes one, a sample a run",
                 name, path, strerror(errno), name);
        return;
    }
    outcome->compared = true;
    char line[REPORT_LINE_MAX];
    if (BaselineBuildSummaryLine(line, sizeof(line), name, &outcome->totals) > 0u) {
        SinkLine(sink, line);
    }
}

static void Add(const char *directory, const char *name, const char *build, const char *label,
                const BaselineSink *sink, BaselineOutcome *outcome) {
    char path[256];
    PathOf(path, sizeof(path), directory, name);
    mkdir(directory, 0777);
    uint32_t sample = 0;
    uint32_t samples = 0;
    if (!BaselineAddFile(path, build, label, &sample, &samples)) {
        SinkNote(sink, &outcome->notes,
                 "baseline=%s: sample %u of %zu timed cases could not be written to %s (%s), so "
                 "the file may end inside it",
                 name, (unsigned)sample, kept_count, path, strerror(errno));
        return;
    }
    outcome->added = true;
    outcome->sample = sample;
    outcome->samples_after = samples;
    SinkNote(sink, &outcome->notes,
             "baseline=%s: sample %u of %zu timed cases added to %s, which now has %u", name,
             (unsigned)sample, kept_count, path, (unsigned)samples);
}

BaselineOutcome BaselineFinish(const char *directory, const char *add_to, const char *compare,
                               const char *build, const char *label, bool timed, bool resumed,
                               const BaselineSink *sink) {
    BaselineOutcome outcome;
    memset(&outcome, 0, sizeof(outcome));
    const bool adding = add_to != NULL && add_to[0] != '\0';
    const bool comparing = compare != NULL && compare[0] != '\0';
    if (!adding && !comparing) {
        return outcome;
    }
    if (!timed) {
        if (adding) {
            SinkNote(sink, &outcome.notes,
                     "baseline=%s: this run has notime, so its cases carry no time and no "
                     "sample was added",
                     add_to);
        }
        if (comparing) {
            SinkNote(sink, &outcome.notes,
                     "compare=%s: this run has notime, so its cases carry no time and nothing "
                     "was compared",
                     compare);
        }
        return outcome;
    }
    if (dropped_count > 0u) {
        SinkNote(sink, &outcome.notes,
                 "baseline: %zu timed cases past the %u this program keeps are left out",
                 dropped_count, (unsigned)BASELINE_CASES_MAX);
    }
    if (comparing) {
        if (resumed) {
            SinkNote(sink, &outcome.notes,
                     "compare=%s: this run went on after the program ended inside it, so only "
                     "this launch's %zu timed cases are compared",
                     compare, kept_count);
        }
        Compare(directory, compare, sink, &outcome);
    }
    if (adding) {
        if (resumed) {
            SinkNote(sink, &outcome.notes,
                     "baseline=%s: this run went on after the program ended inside it, and this "
                     "launch has only %zu of its timed cases, so no sample was added",
                     add_to, kept_count);
        } else if (kept_count == 0u) {
            // A sample of nothing would add to the file's count of runs and to no case's.
            SinkNote(sink, &outcome.notes,
                     "baseline=%s: this run timed no case (only the drawing suites and fs are "
                     "timed), so no sample was added",
                     add_to);
        } else {
            Add(directory, add_to, build, label, sink, &outcome);
        }
    }
    return outcome;
}
