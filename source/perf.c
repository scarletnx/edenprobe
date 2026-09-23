// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "perf.h"

typedef struct {
    uint32_t name_hash;
    uint32_t count;
    uint32_t samples[PERF_SAMPLES];
} PerfCase;

static PerfPass pass;
static uint32_t case_index;
static uint64_t pending_ns;
static bool pending_any;
static uint32_t pending_submits;
static uint32_t reported_submits;
static PerfCase cases[PERF_MAX_CASES];
static uint32_t reported[PERF_MAX_CASES];
static uint32_t reported_count;
static uint32_t pass_number;
// Names as the last pass that reached each position had them. Cut to fit: a breadcrumb only
// has to be recognisable, and the report line is where a name is whole.
#define PERF_NAME_MAX 96u
static char names[PERF_MAX_CASES][PERF_NAME_MAX];

static uint32_t Hash(const char *text) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; text != NULL && text[i] != '\0'; ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 16777619u;
    }
    return hash;
}

void PerfAddWork(uint64_t nanoseconds) {
    pending_ns += nanoseconds;
    pending_any = true;
    ++pending_submits;
}

void PerfSuiteBegin(void) {
    memset(cases, 0, sizeof(cases));
    memset(names, 0, sizeof(names));
    pass_number = 0;
    reported_count = 0;
    reported_submits = 0;
    pass = PERF_UNTIMED;
    case_index = 0;
    pending_ns = 0;
    pending_any = false;
    pending_submits = 0;
}

void PerfPassBegin(PerfPass next) {
    pass = next;
    pass_number = next == PERF_UNTIMED ? 0u : pass_number + 1u;
    case_index = 0;
    pending_ns = 0;
    pending_any = false;
    pending_submits = 0;
}

/// Insertion sort: the most any caller has is one suite's cases.
static void Sort(uint32_t *values, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        const uint32_t value = values[i];
        size_t j = i;
        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
}

uint32_t PerfMedian(uint32_t *values, size_t count) {
    if (values == NULL || count == 0) {
        return 0;
    }
    Sort(values, count);
    if ((count & 1u) != 0u) {
        return values[count / 2u];
    }
    // Added in 64 bits, so two times near the top of the range cannot wrap into a small one.
    return (uint32_t)(((uint64_t)values[count / 2u - 1u] + values[count / 2u]) / 2u);
}

uint32_t PerfMinimum(const uint32_t *samples, size_t count) {
    if (samples == NULL || count == 0) {
        return 0;
    }
    uint32_t smallest = samples[0];
    for (size_t i = 1; i < count; ++i) {
        if (samples[i] < smallest) {
            smallest = samples[i];
        }
    }
    return smallest;
}

bool PerfCaseEnd(const char *name, uint32_t *microseconds) {
    const bool any = pending_any;
    // Rounded to the nearest microsecond, and at least one: a case that did submit work and
    // reports zero would read as a case that did none.
    uint64_t us = (pending_ns + 500u) / 1000u;
    if (us == 0u) {
        us = 1u;
    }
    if (us > UINT32_MAX) {
        us = UINT32_MAX;
    }
    pending_ns = 0;
    pending_any = false;
    const uint32_t submits = pending_submits;
    pending_submits = 0;

    const uint32_t index = case_index++;
    if (pass != PERF_UNTIMED && index < PERF_MAX_CASES) {
        snprintf(names[index], sizeof(names[index]), "%s", name != NULL ? name : "");
    }
    if (pass == PERF_UNTIMED || !any || index >= PERF_MAX_CASES) {
        return false;
    }
    PerfCase *const slot = &cases[index];
    const uint32_t hash = Hash(name);
    if (slot->name_hash != hash) {
        memset(slot, 0, sizeof(*slot));
        slot->name_hash = hash;
    }
    if (pass == PERF_WARMUP) {
        return false;
    }
    if (slot->count < PERF_SAMPLES) {
        slot->samples[slot->count++] = (uint32_t)us;
    }
    if (pass != PERF_VERDICT) {
        return false;
    }

    const uint32_t smallest = PerfMinimum(slot->samples, slot->count);
    if (reported_count < PERF_MAX_CASES) {
        reported[reported_count++] = smallest;
        reported_submits += submits;
    }
    if (microseconds != NULL) {
        *microseconds = smallest;
    }
    return true;
}

uint32_t PerfPassNumber(void) {
    return pass_number;
}

uint32_t PerfCaseIndex(void) {
    return case_index;
}

const char *PerfNameAt(uint32_t index) {
    if (index >= PERF_MAX_CASES || names[index][0] == '\0') {
        return NULL;
    }
    return names[index];
}

void PerfSummarize(uint32_t *times, size_t count, PerfSummary *out) {
    memset(out, 0, sizeof(*out));
    if (times == NULL || count == 0) {
        return;
    }
    Sort(times, count);
    out->timed = (uint32_t)count;
    for (size_t i = 0; i < count; ++i) {
        out->total += times[i];
    }
    out->min = times[0];
    out->max = times[count - 1u];
    out->median = PerfMedian(times, count);
}

void PerfSuiteSummary(PerfSummary *out) {
    uint32_t copy[PERF_MAX_CASES];
    memcpy(copy, reported, reported_count * sizeof(copy[0]));
    PerfSummarize(copy, reported_count, out);
    out->submits = reported_submits;
}
