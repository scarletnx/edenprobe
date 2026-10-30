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
/// Its names are kept as a file's line carries them, escaped for a JSON string and cut, and found
/// by that text: a name read back is the bytes written, with nothing undone that could differ.
typedef struct {
    char suite[BASELINE_SUITE_MAX];
    char name[BASELINE_NAME_MAX];
    uint32_t us;
    uint32_t samples[BASELINE_SAMPLES_MAX];
    uint32_t sample_count;
    /// Its time in the sample being read, which joins samples only if that sample is whole.
    uint32_t pending_us;
    bool pending;
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

/// @p text as a JSON string carries it, cut to fit @p out: what a file's line holds. A report line
/// writes a tab as a space, so a name with one would never meet its own line read back unless
/// both sides were this text.
static void JsonText(char *out, size_t out_size, const char *text) {
    (void)ReportEscape(out, out_size, text != NULL ? text : "");
}

void BaselineReset(void) {
    kept_count = 0;
    dropped_count = 0;
    memset(slots, 0, sizeof(slots));
}

void BaselineTake(const char *suite, const char *name, uint32_t us) {
    char s[BASELINE_SUITE_MAX];
    char n[BASELINE_NAME_MAX];
    JsonText(s, sizeof(s), suite);
    JsonText(n, sizeof(n), name);
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
    char safe_build[BASELINE_BUILD_MAX];
    char safe_label[BASELINE_BUILD_MAX];
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
    return Finish(out, out_size,
                  snprintf(out, out_size,
                           "{\"sample\":%u,\"suite\":\"%s\",\"name\":\"%s\",\"us\":%u}",
                           (unsigned)sample, suite != NULL ? suite : "", name != NULL ? name : "",
                           (unsigned)us));
}

/// Steps @p at past @p literal when the text there starts with it.
static bool Expect(const char **at, const char *literal) {
    const size_t length = strlen(literal);
    if (strncmp(*at, literal, length) != 0) {
        return false;
    }
    *at += length;
    return true;
}

/// The decimal number at @p at, one digit at least and no more than 32 bits hold.
static bool Number(const char **at, uint32_t *out) {
    const char *p = *at;
    if (*p < '0' || *p > '9') {
        return false;
    }
    uint64_t value = 0;
    for (; *p >= '0' && *p <= '9'; ++p) {
        value = value * 10u + (uint64_t)(*p - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    *out = (uint32_t)value;
    *at = p;
    return true;
}

/// The string at @p at, whose opening quote is already passed, as the line carries it, escapes
/// and all, cut to fit @p out where the writer's escaping cuts (never between a backslash and what
/// it escapes); @p at ends past the closing quote. False when the string does not end.
static bool Text(const char **at, char *out, size_t out_size) {
    const char *p = *at;
    size_t w = 0;
    bool full = out_size < 3u;
    for (; *p != '\0' && *p != '"'; ++p) {
        const bool pair = *p == '\\';
        if (pair && p[1] == '\0') {
            return false;
        }
        full = full || w + 2u >= out_size;
        if (!full) {
            out[w++] = *p;
            if (pair) {
                out[w++] = p[1];
            }
        }
        p += pair ? 1 : 0;
    }
    if (*p != '"') {
        return false;
    }
    if (out_size > 0u) {
        out[w < out_size ? w : out_size - 1u] = '\0';
    }
    *at = p + 1;
    return true;
}

/// The closing brace and the end of the line: a line end, or the end of the file's last line.
/// Anything after the brace is a line that was cut and had the next one written onto it.
static bool Ends(const char *at) {
    return at[0] == '}' &&
           (at[1] == '\0' || strcmp(at + 1, "\n") == 0 || strcmp(at + 1, "\r\n") == 0);
}

BaselineLineKind BaselineParseLine(const char *line, BaselineFileLine *out) {
    if (line == NULL || out == NULL) {
        return BASELINE_LINE_OTHER;
    }
    memset(out, 0, sizeof(*out));
    // Every field, in the order the builders write them, and the line's end: a line cut short
    // anywhere, inside a number included, is not one, and neither is one another was written on.
    const char *at = line;
    if (Expect(&at, "{\"type\":\"sample\",\"sample\":")) {
        const bool whole = Number(&at, &out->sample) && Expect(&at, ",\"build\":\"") &&
                           Text(&at, out->build, sizeof(out->build)) &&
                           Expect(&at, ",\"label\":\"") &&
                           Text(&at, out->label, sizeof(out->label)) &&
                           Expect(&at, ",\"cases\":") && Number(&at, &out->cases) && Ends(at);
        return whole ? BASELINE_LINE_OPENS : BASELINE_LINE_OTHER;
    }
    if (Expect(&at, "{\"sample\":")) {
        const bool whole = Number(&at, &out->sample) && Expect(&at, ",\"suite\":\"") &&
                           Text(&at, out->suite, sizeof(out->suite)) &&
                           Expect(&at, ",\"name\":\"") &&
                           Text(&at, out->name, sizeof(out->name)) && Expect(&at, ",\"us\":") &&
                           Number(&at, &out->us) && Ends(at);
        return whole ? BASELINE_LINE_CASE : BASELINE_LINE_OTHER;
    }
    return BASELINE_LINE_OTHER;
}

size_t BaselineBuildMovedLine(char *out, size_t out_size, const char *baseline, const char *suite,
                              const char *name, BaselineMove move, uint32_t us,
                              BaselineLimit limit, size_t samples) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    char safe_baseline[96];
    ReportEscape(safe_baseline, sizeof(safe_baseline), baseline != NULL ? baseline : "");
    const char *const safe_suite = suite != NULL ? suite : "";
    const char *const safe_name = name != NULL ? name : "";
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

/// A sample a comparison leaves out, for its note: its number, whether a line opening it was read
/// and the cases that line said, and the whole case lines of it read.
typedef struct {
    uint32_t sample;
    bool opened;
    uint32_t said;
    uint32_t read;
} LeftOut;

/// How many left-out samples get a note of their own; one more note counts the rest.
#define BASELINE_LEFT_OUT_NOTED 4u

/// What reading a baseline file found, beyond the samples each kept case got.
typedef struct {
    /// The samples the file has whole: every case its opening line said, read whole.
    uint32_t samples;
    /// The largest sample number on any whole line, so the next sample's is new.
    uint32_t highest;
    /// The last line has no line end: a write cut short left it.
    bool open_end;
    /// The samples left out, and the first few of them.
    uint32_t left_out;
    LeftOut first_left_out[BASELINE_LEFT_OUT_NOTED];
} FileRead;

/// The sample being read: its number and the cases its opening line said, or none open, and the
/// kept cases its lines gave a time, which join their samples only once it turns out whole.
static struct {
    bool open;
    uint32_t sample;
    uint32_t said;
    uint32_t read;
    /// Case lines of a sample whose opening line was not read: its number, or 0.
    uint32_t stray_sample;
    uint32_t stray_read;
    uint32_t touched[BASELINE_CASES_MAX];
    size_t touched_count;
} reading;

static void LeaveOut(FileRead *read_out, uint32_t sample, bool opened, uint32_t said,
                     uint32_t lines) {
    if (read_out->left_out < BASELINE_LEFT_OUT_NOTED) {
        LeftOut *const left = &read_out->first_left_out[read_out->left_out];
        left->sample = sample;
        left->opened = opened;
        left->said = said;
        left->read = lines;
    }
    ++read_out->left_out;
}

/// Ends the sample being read: whole, its times join each case's samples; short of what its
/// opening line said, or opened with none, it is left out.
static void EndSample(FileRead *read_out) {
    if (reading.open) {
        const bool whole = reading.said > 0u && reading.read == reading.said;
        for (size_t i = 0; i < reading.touched_count; ++i) {
            KeptCase *const c = &kept[reading.touched[i]];
            if (whole) {
                if (c->sample_count < BASELINE_SAMPLES_MAX) {
                    c->samples[c->sample_count++] = c->pending_us;
                } else {
                    // Full: the newest samples are the ones a machine's state is closest to,
                    // so the oldest goes.
                    memmove(c->samples, c->samples + 1,
                            sizeof(c->samples) - sizeof(c->samples[0]));
                    c->samples[BASELINE_SAMPLES_MAX - 1u] = c->pending_us;
                }
            }
            c->pending = false;
        }
        if (whole) {
            ++read_out->samples;
        } else {
            LeaveOut(read_out, reading.sample, true, reading.said, reading.read);
        }
    }
    reading.open = false;
    reading.touched_count = 0;
}

/// Ends a run of case lines whose sample's opening line was not read: left out, as nothing says
/// how many cases it had.
static void EndStray(FileRead *read_out) {
    if (reading.stray_sample != 0u) {
        LeaveOut(read_out, reading.stray_sample, false, 0, reading.stray_read);
    }
    reading.stray_sample = 0;
    reading.stray_read = 0;
}

/// Reads @p path into @p read_out, giving each kept case the samples it has there when
/// @p fill_cases. False when it cannot be opened.
static bool ReadFile(const char *path, FileRead *read_out, bool fill_cases) {
    memset(read_out, 0, sizeof(*read_out));
    memset(&reading, 0, sizeof(reading));
    FILE *const file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    if (fill_cases) {
        for (size_t i = 0; i < kept_count; ++i) {
            kept[i].sample_count = 0;
            kept[i].pending = false;
        }
    }
    static char line[BASELINE_LINE_MAX + 2u];
    static BaselineFileLine read;
    while (fgets(line, sizeof(line), file) != NULL) {
        const size_t length = strlen(line);
        read_out->open_end = length > 0u && line[length - 1u] != '\n';
        const BaselineLineKind kind = BaselineParseLine(line, &read);
        if (kind == BASELINE_LINE_OTHER) {
            continue;
        }
        if (read.sample > read_out->highest) {
            read_out->highest = read.sample;
        }
        if (kind == BASELINE_LINE_OPENS) {
            EndSample(read_out);
            EndStray(read_out);
            reading.open = true;
            reading.sample = read.sample;
            reading.said = read.cases;
            reading.read = 0;
            continue;
        }
        if (!reading.open || read.sample != reading.sample) {
            if (reading.stray_sample != read.sample) {
                EndStray(read_out);
                reading.stray_sample = read.sample;
            }
            ++reading.stray_read;
            continue;
        }
        ++reading.read;
        KeptCase *const c = fill_cases ? Find(read.suite, read.name) : NULL;
        if (c == NULL) {
            continue;
        }
        if (!c->pending) {
            c->pending = true;
            reading.touched[reading.touched_count++] = (uint32_t)(c - kept);
        }
        c->pending_us = read.us;
    }
    EndSample(read_out);
    EndStray(read_out);
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

/// Says which samples reading the file left out, the first few one by one.
static void NoteLeftOut(const BaselineSink *sink, size_t *notes, const char *word,
                        const char *name, const FileRead *read) {
    for (uint32_t i = 0; i < read->left_out && i < BASELINE_LEFT_OUT_NOTED; ++i) {
        const LeftOut *const left = &read->first_left_out[i];
        if (!left->opened) {
            SinkNote(sink, notes,
                     "%s=%s: sample %u of the file is left out: no line opening it was read, and "
                     "%u whole line%s of it %s, so nothing says how many cases it had",
                     word, name, (unsigned)left->sample, (unsigned)left->read,
                     left->read == 1u ? "" : "s", left->read == 1u ? "was" : "were");
        } else if (left->said == 0u) {
            SinkNote(sink, notes,
                     "%s=%s: sample %u of the file is left out: it opened with 0 cases, and %u "
                     "whole lines of it were read",
                     word, name, (unsigned)left->sample, (unsigned)left->read);
        } else {
            SinkNote(sink, notes,
                     "%s=%s: sample %u of the file is left out: it opened with %u case%s and %u "
                     "whole line%s of it %s read, as a write cut short leaves it",
                     word, name, (unsigned)left->sample, (unsigned)left->said,
                     left->said == 1u ? "" : "s", (unsigned)left->read,
                     left->read == 1u ? "" : "s", left->read == 1u ? "was" : "were");
        }
    }
    if (read->left_out > BASELINE_LEFT_OUT_NOTED) {
        SinkNote(sink, notes, "%s=%s: %u more samples of the file are left out the same way", word,
                 name, (unsigned)(read->left_out - BASELINE_LEFT_OUT_NOTED));
    }
}

bool BaselineCompareFile(const char *path, const char *name, const BaselineSink *sink,
                         BaselineTotals *totals) {
    memset(totals, 0, sizeof(*totals));
    FileRead read;
    if (!ReadFile(path, &read, true)) {
        return false;
    }
    totals->samples = read.samples;
    NoteLeftOut(sink, &totals->notes, "compare", name, &read);
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

/// What adding a sample did: its number, the file's whole samples after it, whether the last
/// line had to get its line end first, and the samples reading the file left out.
typedef struct {
    bool written;
    uint32_t sample;
    uint32_t samples_after;
    bool ended_line;
    uint32_t left_out;
    /// errno when it was not written, taken before a note can change it.
    int error;
} Added;

static Added AddTo(const char *path, const char *build, const char *label) {
    Added added;
    memset(&added, 0, sizeof(added));
    FileRead read;
    // A missing file is a new baseline: its first sample is number one.
    (void)ReadFile(path, &read, false);
    added.sample = read.highest + 1u;
    added.samples_after = read.samples;
    added.left_out = read.left_out;
    FILE *const file = fopen(path, "a");
    if (file == NULL) {
        added.error = errno;
        return added;
    }
    // A line a write was cut short in has no end: this sample's first line would go on the same
    // line and be lost with it.
    if (read.open_end) {
        if (fputc('\n', file) == EOF) {
            added.error = errno;
            fclose(file);
            return added;
        }
        added.ended_line = true;
    }
    static char line[BASELINE_LINE_MAX + 2u];
    bool written =
        BaselineBuildSampleLine(line, sizeof(line), added.sample, build, label, kept_count) > 0u &&
        fprintf(file, "%s\n", line) > 0;
    for (size_t i = 0; written && i < kept_count; ++i) {
        written = BaselineBuildCaseLine(line, sizeof(line), added.sample, kept[i].suite,
                                        kept[i].name, kept[i].us) > 0u &&
                  fprintf(file, "%s\n", line) > 0;
    }
    added.written = fclose(file) == 0 && written;
    added.error = errno;
    if (added.written) {
        added.samples_after = read.samples + 1u;
    }
    return added;
}

bool BaselineAddFile(const char *path, const char *build, const char *label, uint32_t *sample,
                     uint32_t *samples_after) {
    const Added added = AddTo(path, build, label);
    *sample = added.sample;
    *samples_after = added.samples_after;
    return added.written;
}

static void Compare(const char *directory, const char *name, const BaselineSink *sink,
                    BaselineOutcome *outcome) {
    char path[256];
    PathOf(path, sizeof(path), directory, name);
    const bool read = BaselineCompareFile(path, name, sink, &outcome->totals);
    outcome->notes += outcome->totals.notes;
    if (!read) {
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
    const Added added = AddTo(path, build, label);
    if (added.ended_line) {
        SinkNote(sink, &outcome->notes,
                 "baseline=%s: the last line of %s had no line end, so one was written before "
                 "sample %u: a write the program did not finish leaves a line so, and this "
                 "sample's first line would have gone onto it",
                 name, path, (unsigned)added.sample);
    }
    if (!added.written) {
        SinkNote(sink, &outcome->notes,
                 "baseline=%s: sample %u of %zu timed cases could not be written to %s (%s), so "
                 "the file may end inside it",
                 name, (unsigned)added.sample, kept_count, path, strerror(added.error));
        return;
    }
    outcome->added = true;
    outcome->sample = added.sample;
    outcome->samples_after = added.samples_after;
    if (added.left_out > 0u) {
        SinkNote(sink, &outcome->notes,
                 "baseline=%s: sample %u of %zu timed cases added to %s, which now has %u whole "
                 "and %u left out, short of the cases they opened with (compare=%s names them)",
                 name, (unsigned)added.sample, kept_count, path, (unsigned)added.samples_after,
                 (unsigned)added.left_out, name);
    } else {
        SinkNote(sink, &outcome->notes,
                 "baseline=%s: sample %u of %zu timed cases added to %s, which now has %u", name,
                 (unsigned)added.sample, kept_count, path, (unsigned)added.samples_after);
    }
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
