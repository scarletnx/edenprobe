// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// fileno and stat, which the host build's C11 does not declare without asking.
#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "journal.h"

static const char *const RESULT_WORDS[JOURNAL_RESULT_COUNT] = {
    "pass", "fail", "skip", "crashed", "muted",
};

const char *JournalResultWord(JournalResult result) {
    return result < JOURNAL_RESULT_COUNT ? RESULT_WORDS[result] : "muted";
}

/// Copies @p text with every character a line cannot carry as itself made a space, so a name
/// can never end its record early or start another.
static void CopyField(char *out, size_t out_size, const char *text) {
    size_t n = 0;
    for (; text != NULL && text[n] != '\0' && n + 1u < out_size; ++n) {
        const unsigned char c = (unsigned char)text[n];
        out[n] = c < 0x20u || c == 0x7Fu ? ' ' : (char)c;
    }
    out[n] = '\0';
}

size_t JournalFormat(char *out, size_t out_size, const JournalRecord *r) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (r == NULL) {
        return 0;
    }
    char suite[JOURNAL_SUITE_MAX];
    char name[JOURNAL_NAME_MAX];
    char mode[JOURNAL_MODE_MAX];
    CopyField(suite, sizeof(suite), r->suite);
    CopyField(name, sizeof(name), r->name);
    CopyField(mode, sizeof(mode), r->mode);
    const JournalCounts *const c = &r->counts;
    int wrote = -1;
    switch (r->kind) {
    case JOURNAL_RUN:
        wrote = snprintf(out, out_size, "run %s %s%s%s\n", suite, name, mode[0] != '\0' ? " " : "",
                         mode);
        break;
    case JOURNAL_CHOSEN:
        wrote = snprintf(out, out_size, "chosen %s\n", mode);
        break;
    case JOURNAL_LAUNCH:
        wrote = snprintf(out, out_size, "launch %u\n", (unsigned)r->number);
        break;
    case JOURNAL_STEP:
        wrote = snprintf(out, out_size, "step %s %" PRIu64 " %u %u %u %u\n", suite, r->offset,
                         (unsigned)c->counts[0], (unsigned)c->counts[1], (unsigned)c->counts[2],
                         (unsigned)c->counts[3]);
        break;
    case JOURNAL_PASS:
        wrote = snprintf(out, out_size, "pass %s %u\n", suite, (unsigned)r->number);
        break;
    case JOURNAL_START:
        wrote = snprintf(out, out_size, "start %s %u %s\n", suite, (unsigned)r->number, name);
        break;
    case JOURNAL_END:
        wrote = snprintf(out, out_size, "end %s %u %s %s\n", suite, (unsigned)r->number,
                         JournalResultWord(r->result), name);
        break;
    case JOURNAL_CRASH:
        wrote = snprintf(out, out_size, "crashed %s %u %s\n", suite, (unsigned)r->number, name);
        break;
    case JOURNAL_DONE:
        wrote = snprintf(out, out_size, "done %s\n", suite);
        break;
    case JOURNAL_FINISHED:
        wrote = snprintf(out, out_size, "finished %u %u %u %u\n", (unsigned)c->counts[0],
                         (unsigned)c->counts[1], (unsigned)c->counts[2], (unsigned)c->counts[3]);
        break;
    case JOURNAL_STOPPED:
        wrote = snprintf(out, out_size, "stopped\n");
        break;
    case JOURNAL_NONE:
        break;
    }
    // A name or a word with nothing in it would leave two fields where the reader wants three.
    const bool empty_field = (r->kind != JOURNAL_LAUNCH && r->kind != JOURNAL_FINISHED &&
                              r->kind != JOURNAL_CHOSEN && r->kind != JOURNAL_STOPPED &&
                              suite[0] == '\0') ||
                             (r->kind == JOURNAL_CHOSEN && mode[0] == '\0') ||
                             ((r->kind == JOURNAL_START || r->kind == JOURNAL_END ||
                               r->kind == JOURNAL_CRASH || r->kind == JOURNAL_RUN) &&
                              name[0] == '\0');
    if (wrote <= 0 || (size_t)wrote >= out_size || strchr(suite, ' ') != NULL ||
        (r->kind == JOURNAL_RUN && strchr(name, ' ') != NULL) || empty_field) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)wrote;
}

/// The next space separated word of @p *cursor, which moves past it and one space.
static bool Word(const char **cursor, char *out, size_t out_size) {
    const char *p = *cursor;
    size_t n = 0;
    while (p[n] != '\0' && p[n] != ' ') {
        ++n;
    }
    if (n == 0u || n >= out_size) {
        return false;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    *cursor = p[n] == ' ' ? p + n + 1u : p + n;
    return true;
}

/// A decimal number of at most nineteen digits and nothing else.
static bool Number(const char *text, uint64_t *out) {
    uint64_t value = 0;
    size_t n = 0;
    for (; text[n] != '\0'; ++n) {
        if (text[n] < '0' || text[n] > '9' || n >= 19u) {
            return false;
        }
        value = value * 10u + (uint64_t)(text[n] - '0');
    }
    *out = value;
    return n > 0u;
}

static bool NumberWord(const char **cursor, uint64_t *out) {
    char word[24];
    return Word(cursor, word, sizeof(word)) && Number(word, out);
}

static bool Small(const char **cursor, uint32_t *out) {
    uint64_t value = 0;
    if (!NumberWord(cursor, &value) || value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool Counts(const char **cursor, JournalCounts *out) {
    for (int i = 0; i < 4; ++i) {
        if (!Small(cursor, &out->counts[i])) {
            return false;
        }
    }
    return true;
}

/// The rest of the line, which a case name runs to.
static bool Rest(const char *cursor, char *out, size_t out_size) {
    const size_t n = strlen(cursor);
    if (n == 0u || n >= out_size) {
        return false;
    }
    memcpy(out, cursor, n + 1u);
    return true;
}

bool JournalParse(const char *line, JournalRecord *out) {
    if (line == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    char text[JOURNAL_LINE_MAX];
    size_t length = strlen(line);
    if (length > 0u && line[length - 1u] == '\n') {
        --length;
    }
    if (length == 0u || length >= sizeof(text)) {
        return false;
    }
    memcpy(text, line, length);
    text[length] = '\0';
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 0x20u || c == 0x7Fu) {
            return false;
        }
    }

    const char *cursor = text;
    char kind[12];
    if (!Word(&cursor, kind, sizeof(kind))) {
        return false;
    }
    bool ok = false;
    if (strcmp(kind, "run") == 0) {
        out->kind = JOURNAL_RUN;
        ok = Word(&cursor, out->suite, sizeof(out->suite)) &&
             Word(&cursor, out->name, sizeof(out->name));
        if (ok && strlen(cursor) < sizeof(out->mode)) {
            memcpy(out->mode, cursor, strlen(cursor) + 1u);
        } else {
            ok = false;
        }
    } else if (strcmp(kind, "chosen") == 0) {
        out->kind = JOURNAL_CHOSEN;
        ok = Rest(cursor, out->mode, sizeof(out->mode));
    } else if (strcmp(kind, "launch") == 0) {
        out->kind = JOURNAL_LAUNCH;
        ok = Small(&cursor, &out->number) && *cursor == '\0';
    } else if (strcmp(kind, "step") == 0) {
        out->kind = JOURNAL_STEP;
        ok = Word(&cursor, out->suite, sizeof(out->suite)) && NumberWord(&cursor, &out->offset) &&
             Counts(&cursor, &out->counts) && *cursor == '\0';
    } else if (strcmp(kind, "pass") == 0) {
        out->kind = JOURNAL_PASS;
        ok = Word(&cursor, out->suite, sizeof(out->suite)) && Small(&cursor, &out->number) &&
             *cursor == '\0';
    } else if (strcmp(kind, "start") == 0 || strcmp(kind, "crashed") == 0) {
        out->kind = kind[0] == 's' ? JOURNAL_START : JOURNAL_CRASH;
        ok = Word(&cursor, out->suite, sizeof(out->suite)) && Small(&cursor, &out->number) &&
             Rest(cursor, out->name, sizeof(out->name));
    } else if (strcmp(kind, "end") == 0) {
        out->kind = JOURNAL_END;
        char result[12];
        ok = Word(&cursor, out->suite, sizeof(out->suite)) && Small(&cursor, &out->number) &&
             Word(&cursor, result, sizeof(result)) && Rest(cursor, out->name, sizeof(out->name));
        if (ok) {
            ok = false;
            for (int i = 0; i < JOURNAL_RESULT_COUNT; ++i) {
                if (strcmp(result, RESULT_WORDS[i]) == 0) {
                    out->result = (JournalResult)i;
                    ok = true;
                }
            }
        }
    } else if (strcmp(kind, "done") == 0) {
        out->kind = JOURNAL_DONE;
        ok = Word(&cursor, out->suite, sizeof(out->suite)) && *cursor == '\0';
    } else if (strcmp(kind, "finished") == 0) {
        out->kind = JOURNAL_FINISHED;
        ok = Counts(&cursor, &out->counts) && *cursor == '\0';
    } else if (strcmp(kind, "stopped") == 0) {
        out->kind = JOURNAL_STOPPED;
        ok = *cursor == '\0';
    }
    if (!ok) {
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

void JournalStateInit(JournalState *state) {
    memset(state, 0, sizeof(*state));
}

bool JournalStepDone(const JournalState *state, const char *step) {
    for (uint32_t i = 0; i < state->done_count; ++i) {
        if (strcmp(state->done[i], step) == 0) {
            return true;
        }
    }
    return false;
}

static void Forget(JournalState *state, const char *step) {
    for (uint32_t i = 0; i < state->done_count; ++i) {
        if (strcmp(state->done[i], step) == 0) {
            memmove(&state->done[i], &state->done[i + 1u],
                    (state->done_count - i - 1u) * sizeof(state->done[0]));
            --state->done_count;
            return;
        }
    }
}

void JournalFeed(JournalState *state, const char *line, bool complete) {
    if (!complete) {
        state->torn = true;
        return;
    }
    // A whole line after a torn one means the torn one was not the last after all.
    state->torn = false;
    JournalRecord r;
    if (!JournalParse(line, &r)) {
        // A blank line is what an emulator that keeps the rewritten byte of an empty write
        // could leave; anything else is counted.
        if (line[0] != '\0' && line[0] != '\n') {
            ++state->unreadable;
        }
        return;
    }
    state->present = true;
    state->in_case = false;
    switch (r.kind) {
    case JOURNAL_RUN: {
        // A journal holds one run; anything before its first line belongs to none.
        const uint32_t unreadable = state->unreadable;
        JournalStateInit(state);
        state->present = true;
        state->unreadable = unreadable;
        state->has_run = true;
        memcpy(state->version, r.suite, sizeof(state->version));
        memcpy(state->build, r.name, sizeof(state->build));
        memcpy(state->mode, r.mode, sizeof(state->mode));
        break;
    }
    case JOURNAL_CHOSEN:
        memcpy(state->chosen, r.mode, sizeof(state->chosen));
        break;
    case JOURNAL_LAUNCH:
        state->launches = r.number > state->launches ? r.number : state->launches + 1u;
        break;
    case JOURNAL_STEP:
        // Run again after a crash inside it, a step is not done until its new done record.
        Forget(state, r.suite);
        state->in_step = true;
        state->step = r;
        state->counts = r.counts;
        state->last_ended[0] = '\0';
        break;
    case JOURNAL_PASS:
        break;
    case JOURNAL_START:
        state->in_case = true;
        state->started = r;
        break;
    case JOURNAL_END:
        if (r.result < JOURNAL_MUTED) {
            ++state->counts.counts[r.result];
        }
        memcpy(state->last_ended, r.name, sizeof(state->last_ended));
        break;
    case JOURNAL_CRASH: {
        bool known = false;
        for (uint32_t i = 0; i < state->crash_count; ++i) {
            known = known || (strcmp(state->crashes[i].suite, r.suite) == 0 &&
                              strcmp(state->crashes[i].name, r.name) == 0);
        }
        if (!known && state->crash_count < JOURNAL_CRASHES_MAX) {
            state->crashes[state->crash_count++] = r;
        }
        break;
    }
    case JOURNAL_DONE:
        if (!JournalStepDone(state, r.suite) && state->done_count < JOURNAL_STEPS_MAX) {
            memcpy(state->done[state->done_count++], r.suite, JOURNAL_SUITE_MAX);
        }
        if (state->in_step && strcmp(state->step.suite, r.suite) == 0) {
            state->in_step = false;
        }
        break;
    case JOURNAL_FINISHED:
        state->finished = true;
        state->in_step = false;
        state->counts = r.counts;
        break;
    case JOURNAL_STOPPED:
        // Nothing more: the case the last start named is no longer the one the program ended
        // inside, since something outside it did the ending.
        break;
    case JOURNAL_NONE:
        break;
    }
}

void JournalReadText(JournalState *state, const char *text, size_t length) {
    char line[JOURNAL_LINE_MAX + 1u];
    size_t used = 0;
    bool overlong = false;
    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];
        if (c == '\n') {
            line[used] = '\0';
            if (overlong) {
                ++state->unreadable;
                state->torn = false;
            } else {
                JournalFeed(state, line, true);
            }
            used = 0;
            overlong = false;
            continue;
        }
        if (used + 1u < sizeof(line)) {
            line[used++] = c;
        } else {
            overlong = true;
        }
    }
    if (used > 0u || overlong) {
        line[used] = '\0';
        JournalFeed(state, line, false);
    }
}

JournalPlan JournalPlanStart(const JournalState *state, const char *version, const char *build,
                             const char *mode, int64_t report_size) {
    JournalPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.start = JOURNAL_FRESH;
    plan.launch = 1u;
    // Whatever happens to the run, a case that began and never ended ended the program.
    if (state->has_run && !state->finished && state->in_case) {
        plan.crashed = true;
        plan.crash = state->started;
    }
    if (!state->present) {
        plan.why_new = JOURNAL_NEW_NO_JOURNAL;
        return plan;
    }
    if (!state->has_run) {
        plan.why_new = JOURNAL_NEW_NO_RUN;
        return plan;
    }
    if (state->finished) {
        plan.why_new = JOURNAL_NEW_FINISHED;
        return plan;
    }
    if (strcmp(state->version, version) != 0 || strcmp(state->build, build) != 0) {
        plan.why_new = JOURNAL_NEW_OTHER_BUILD;
        return plan;
    }
    if (strcmp(state->mode, mode) != 0) {
        plan.why_new = JOURNAL_NEW_OTHER_MODE;
        return plan;
    }
    // The report the journal describes has to be there, at least as long as the journal says it
    // was when the step under way began: a run cannot go on in a report someone took away.
    const uint64_t needed = state->in_step ? state->step.offset : 0u;
    if (report_size < 0 || (uint64_t)report_size < needed) {
        plan.why_new = JOURNAL_NEW_NO_REPORT;
        return plan;
    }
    plan.launch = state->launches + 1u;
    plan.counts = state->counts;
    plan.offset = (uint64_t)report_size;
    if (!state->in_step) {
        plan.start = JOURNAL_RESUME_BETWEEN;
    } else if (plan.crashed) {
        plan.start = JOURNAL_RESUME_CASE;
        plan.offset = state->step.offset;
        plan.counts = state->step.counts;
    } else {
        plan.start = JOURNAL_RESUME_STEP;
    }
    return plan;
}

bool JournalPlanAsks(const JournalPlan *plan, bool detailed) {
    return plan != NULL && detailed && plan->start != JOURNAL_FRESH;
}

void JournalPlanRestart(JournalPlan *plan) {
    if (plan == NULL || plan->start == JOURNAL_FRESH) {
        return;
    }
    plan->start = JOURNAL_FRESH;
    plan->why_new = JOURNAL_NEW_CHOSEN;
    plan->launch = 1u;
    plan->offset = 0u;
    memset(&plan->counts, 0, sizeof(plan->counts));
}

/// How a pass is said after a case's name: the warm-up, a timed pass, or nothing outside them.
static const char *PassSaid(uint32_t pass) {
    return pass == 1u ? " in its warm-up" : pass > 1u ? " in a timed pass" : "";
}

size_t JournalDescribeStop(char *out, size_t out_size, const JournalPlan *plan,
                           const JournalState *state) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (plan == NULL || state == NULL) {
        return 0;
    }
    switch (plan->start) {
    case JOURNAL_RESUME_CASE:
        snprintf(out, out_size, "the program ended inside %s case \"%s\"%s", plan->crash.suite,
                 plan->crash.name, PassSaid(plan->crash.number));
        break;
    case JOURNAL_RESUME_STEP:
        snprintf(out, out_size, "the program ended in step %s after \"%s\", outside any case",
                 state->step.suite, state->last_ended[0] != '\0' ? state->last_ended : "its start");
        break;
    case JOURNAL_RESUME_BETWEEN:
        snprintf(out, out_size, "the program ended between two steps, after %u done",
                 (unsigned)state->done_count);
        break;
    case JOURNAL_FRESH:
        break;
    }
    return strlen(out);
}

size_t JournalDescribePlan(char *out, size_t out_size, const JournalPlan *plan,
                           const JournalState *state) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (plan == NULL || state == NULL) {
        return 0;
    }
    char crash[JOURNAL_NAME_MAX + JOURNAL_SUITE_MAX + 64];
    crash[0] = '\0';
    if (plan->crashed) {
        snprintf(crash, sizeof(crash), "the program ended inside %s case \"%s\"%s",
                 plan->crash.suite, plan->crash.name, PassSaid(plan->crash.number));
    }
    switch (plan->start) {
    case JOURNAL_FRESH: {
        static const char *const WHY[] = {
            "no journal", "the last run finished", "the journal is another build's",
            "the journal was started with other mode words",
            "the report the journal describes is gone or shorter", "the journal names no run",
            "the screen was asked to start over rather than go on",
        };
        _Static_assert(sizeof(WHY) / sizeof(WHY[0]) == JOURNAL_NEW_CHOSEN + 1,
                       "a reason for every new run");
        snprintf(out, out_size, "journal: a new run (%s)%s%s%s", WHY[plan->why_new],
                 crash[0] != '\0' ? "; before it, " : "", crash,
                 crash[0] != '\0' ? ", which goes on the crash list" : "");
        break;
    }
    case JOURNAL_RESUME_CASE:
        snprintf(out, out_size,
                 "journal: launch %u resumes the run; %s: that case is skipped as crashed and "
                 "goes on the crash list, and step %s runs again from its start",
                 (unsigned)plan->launch, crash, state->step.suite);
        break;
    case JOURNAL_RESUME_STEP:
        snprintf(out, out_size,
                 "journal: launch %u resumes the run; the program ended in step %s outside any "
                 "case it can skip (after \"%s\"), so the step keeps what it reported and is "
                 "not run again",
                 (unsigned)plan->launch, state->step.suite,
                 state->last_ended[0] != '\0' ? state->last_ended : "its start");
        break;
    case JOURNAL_RESUME_BETWEEN:
        snprintf(out, out_size,
                 "journal: launch %u resumes the run after the %u steps it finished",
                 (unsigned)plan->launch, (unsigned)state->done_count);
        break;
    }
    return strlen(out);
}

size_t JournalDescribeRun(char *out, size_t out_size, const JournalPlan *plan,
                          const JournalState *state, uint32_t crashed) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (plan == NULL || state == NULL) {
        return 0;
    }
    // The crash records are this run's only when the launch went on with it: the journal a new
    // run reads is the run before's.
    const bool resumed = plan->start != JOURNAL_FRESH;
    const uint32_t launches = resumed ? plan->launch : 1u;
    const JournalRecord *ended[JOURNAL_CRASHES_MAX + 1u];
    uint32_t count = 0;
    for (uint32_t i = 0; resumed && i < state->crash_count; ++i) {
        ended[count++] = &state->crashes[i];
    }
    if (plan->start == JOURNAL_RESUME_CASE) {
        bool known = false;
        for (uint32_t i = 0; i < count && !known; ++i) {
            known = strcmp(ended[i]->suite, plan->crash.suite) == 0 &&
                    strcmp(ended[i]->name, plan->crash.name) == 0;
        }
        if (!known) {
            ended[count++] = &plan->crash;
        }
    }
    if (launches == 1u && crashed == 0u) {
        return 0;
    }
    int wrote = snprintf(out, out_size, "journal: this run took %u launch%s; %u case%s skipped as "
                         "crashed",
                         (unsigned)launches, launches == 1u ? "" : "es", (unsigned)crashed,
                         crashed == 1u ? "" : "s");
    size_t used = wrote < 0 ? 0u : (size_t)wrote;
    if (crashed > count && used < out_size) {
        // The rest the crash list named: earlier runs ended inside them, and this one never
        // started them.
        wrote = snprintf(out + used, out_size - used, ", %u of them from the crash list",
                         (unsigned)(crashed - count));
        used += wrote < 0 ? 0u : (size_t)wrote;
    }
    for (uint32_t i = 0; i < count && used < out_size; ++i) {
        wrote = snprintf(out + used, out_size - used, "%s%s \"%s\"",
                         i == 0u ? "; the program ended inside " : ", ", ended[i]->suite,
                         ended[i]->name);
        if (wrote < 0 || used + (size_t)wrote + 4u >= out_size) {
            snprintf(out + used, out_size - used, "...");
            break;
        }
        used += (size_t)wrote;
    }
    return strlen(out);
}

void CrashListInit(CrashList *list) {
    memset(list, 0, sizeof(*list));
}

static void CrashListTake(CrashList *list, const char *line, bool complete) {
    size_t length = strlen(line);
    while (length > 0u && (line[length - 1u] == ' ' || line[length - 1u] == '\r')) {
        --length;
    }
    size_t start = 0;
    while (start < length && line[start] == ' ') {
        ++start;
    }
    if (start == length || line[start] == '#') {
        return;
    }
    if (!complete) {
        list->torn = true;
        return;
    }
    char text[JOURNAL_LINE_MAX];
    if (length - start >= sizeof(text)) {
        ++list->unreadable;
        return;
    }
    memcpy(text, line + start, length - start);
    text[length - start] = '\0';
    const char *cursor = text;
    char suite[JOURNAL_SUITE_MAX];
    char name[JOURNAL_NAME_MAX];
    if (!Word(&cursor, suite, sizeof(suite)) || !Rest(cursor, name, sizeof(name))) {
        ++list->unreadable;
        return;
    }
    if (CrashListHas(list, suite, name)) {
        return;
    }
    if (!CrashListAdd(list, suite, name)) {
        ++list->dropped;
    }
}

void CrashListParse(CrashList *list, const char *text, size_t length) {
    char line[JOURNAL_LINE_MAX + 1u];
    size_t used = 0;
    bool overlong = false;
    for (size_t i = 0; text != NULL && i < length; ++i) {
        if (text[i] == '\n') {
            line[used] = '\0';
            if (overlong) {
                ++list->unreadable;
            } else {
                CrashListTake(list, line, true);
            }
            used = 0;
            overlong = false;
            continue;
        }
        if (used + 1u < sizeof(line)) {
            line[used++] = text[i];
        } else {
            overlong = true;
        }
    }
    if (used > 0u || overlong) {
        line[used] = '\0';
        CrashListTake(list, line, false);
    }
}

size_t CrashListFormat(char *out, size_t out_size, const CrashList *list) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    static const char HEADER[] =
        "# Cases that ended this machine's program, one per line: the suite, then the case.\n"
        "# edenprobe skips them until the mode word retry runs them again; delete a line to try\n"
        "# one case again. The program writes this file whole, so comments here do not stay.\n";
    size_t used = 0;
    int wrote = snprintf(out, out_size, "%s", HEADER);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        out[0] = '\0';
        return 0;
    }
    used = (size_t)wrote;
    for (uint32_t i = 0; i < list->count; ++i) {
        wrote = snprintf(out + used, out_size - used, "%s %s\n", list->entries[i].suite,
                         list->entries[i].name);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            out[0] = '\0';
            return 0;
        }
        used += (size_t)wrote;
    }
    return used;
}

bool CrashListHas(const CrashList *list, const char *suite, const char *name) {
    for (uint32_t i = 0; i < list->count; ++i) {
        if (strcmp(list->entries[i].suite, suite) == 0 &&
            strcmp(list->entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

bool CrashListAdd(CrashList *list, const char *suite, const char *name) {
    if (suite == NULL || name == NULL || suite[0] == '\0' || name[0] == '\0' ||
        strchr(suite, ' ') != NULL || strlen(suite) >= JOURNAL_SUITE_MAX ||
        strlen(name) >= JOURNAL_NAME_MAX || CrashListHas(list, suite, name) ||
        list->count >= CRASH_LIST_MAX) {
        return false;
    }
    CrashEntry *const entry = &list->entries[list->count++];
    CopyField(entry->suite, sizeof(entry->suite), suite);
    CopyField(entry->name, sizeof(entry->name), name);
    return true;
}

bool CrashListRemove(CrashList *list, const char *suite, const char *name) {
    for (uint32_t i = 0; i < list->count; ++i) {
        if (strcmp(list->entries[i].suite, suite) == 0 &&
            strcmp(list->entries[i].name, name) == 0) {
            memmove(&list->entries[i], &list->entries[i + 1u],
                    (list->count - i - 1u) * sizeof(list->entries[0]));
            --list->count;
            return true;
        }
    }
    return false;
}

bool JournalDurableAppend(FILE *file, const char *line) {
    if (file == NULL || line == NULL) {
        return false;
    }
    const size_t length = strlen(line);
    if (length == 0u || line[length - 1u] != '\n') {
        return false;
    }
    // The body, then the newline as the next write at the end. Eden seeks to the
    // offset of each host write, and that seek empties the host buffer onto disk,
    // so the newline is the only byte a crash can still hold. Seeking back onto
    // the line to rewrite a newline that was already there drops, on the Android
    // host library, every buffered byte past the seek. The phone's journal kept
    // the offsets and the report lost the suites those offsets named: the file
    // stayed a dense jsonl, shorter by exactly those suites.
    if (length > 1u &&
        (fwrite(line, 1, length - 1u, file) != length - 1u || fflush(file) != 0)) {
        return false;
    }
    return fputc('\n', file) != EOF && fflush(file) == 0;
}

bool JournalReadPath(JournalState *state, const char *path) {
    struct stat about;
    if (stat(path, &about) != 0 || S_ISDIR(about.st_mode)) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    // A full run's journal is a few hundred kilobytes: read in large pieces, one request each.
    static char buffer[64u * 1024u];
    setvbuf(file, buffer, _IOFBF, sizeof(buffer));
    char line[JOURNAL_LINE_MAX + 1u];
    bool overlong = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        const size_t length = strlen(line);
        const bool complete = length > 0u && line[length - 1u] == '\n';
        if (!complete && !feof(file)) {
            // Longer than any record: read on to its end and count it once.
            overlong = true;
            continue;
        }
        if (overlong) {
            overlong = false;
            if (complete) {
                ++state->unreadable;
            } else {
                state->torn = true;
            }
            continue;
        }
        JournalFeed(state, line, complete);
    }
    fclose(file);
    return true;
}

int64_t JournalReadFile(const char *path, char *out, size_t out_size, bool *longer) {
    if (longer != NULL) {
        *longer = false;
    }
    if (out == NULL || out_size == 0u) {
        return -1;
    }
    out[0] = '\0';
    struct stat about;
    if (stat(path, &about) != 0 || S_ISDIR(about.st_mode)) {
        return -1;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    const size_t read = fread(out, 1, out_size - 1u, file);
    if (longer != NULL && read == out_size - 1u) {
        *longer = fgetc(file) != EOF;
    }
    fclose(file);
    out[read] = '\0';
    return (int64_t)read;
}

bool JournalWriteFile(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    const bool wrote = fwrite(text, 1, length, file) == length;
    return fclose(file) == 0 && wrote;
}

int64_t JournalFileSize(const char *path) {
    struct stat about;
    if (stat(path, &about) != 0 || S_ISDIR(about.st_mode)) {
        return -1;
    }
    return (int64_t)about.st_size;
}
