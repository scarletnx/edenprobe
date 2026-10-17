// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "schedref.h"

#include <stdio.h>
#include <string.h>

void SchedReset(SchedTrace *t) {
    memset(t, 0, sizeof(*t));
}

void SchedMark(SchedTrace *t, SchedStep step, uint64_t tick) {
    if (step >= SCHED_STEPS || __atomic_load_n(&t->order[step], __ATOMIC_ACQUIRE) != 0u) {
        return;
    }
    __atomic_store_n(&t->tick[step], tick, __ATOMIC_RELAXED);
    __atomic_store_n(&t->order[step], __atomic_add_fetch(&t->next, 1u, __ATOMIC_SEQ_CST),
                     __ATOMIC_RELEASE);
}

void SchedValue(SchedTrace *t, SchedValueId id, int32_t value) {
    if (id < SCHED_VALUES) {
        __atomic_store_n(&t->value[id], value, __ATOMIC_RELAXED);
        __atomic_or_fetch(&t->has, 1u << id, __ATOMIC_RELEASE);
    }
}

bool SchedHappened(const SchedTrace *t, SchedStep step) {
    return step < SCHED_STEPS && t->order[step] != 0u;
}

bool SchedBefore(const SchedTrace *t, SchedStep a, SchedStep b) {
    return SchedHappened(t, a) && (!SchedHappened(t, b) || t->order[a] < t->order[b]);
}

static bool Has(const SchedTrace *t, SchedValueId id) {
    return (t->has >> id & 1u) != 0u;
}

int32_t SchedTenths(const SchedTrace *t, SchedStep from, SchedStep to) {
    if (!SchedHappened(t, from) || !SchedHappened(t, to) || t->tick[to] < t->tick[from]) {
        return -1;
    }
    return (int32_t)((t->tick[to] - t->tick[from]) * 10u / SCHED_TICKS_PER_MS);
}

static const char *const CASE_NAMES[SCHED_CASES] = {
    "the owner of a mutex runs at the priority of the thread waiting for it",
    "the inherited priority passes down a chain of owners",
    "a thread that becomes ready at a better priority takes the core from one spinning",
    "the owner goes back to its own priority when it hands the mutex over",
    "a thread behind a spinning one moves to an idle core of its mask",
    "svcSleepThread(0) leaves the thread on its core",
};

const char *SchedCaseName(SchedCaseId id) {
    return id < SCHED_CASES ? CASE_NAMES[id] : "?";
}

static const char *const STEP_NAMES[SCHED_STEPS] = {
    "held", "tail held", "asks the tail", "spinning", "asked", "woke", "got", "flag set",
    "flag seen", "stopped", "unlocked", "sleeps", "ran", "other ran", "done", "watchdog",
};

/// Every step that happened, in the order it did, with its time from the first in ms.
static size_t Timeline(const SchedTrace *t, char *out, size_t size) {
    uint64_t base = UINT64_MAX;
    for (uint32_t s = 0; s < SCHED_STEPS; ++s) {
        if (SchedHappened(t, (SchedStep)s) && t->tick[s] < base) {
            base = t->tick[s];
        }
    }
    size_t n = 0;
    out[0] = '\0';
    for (uint32_t k = 1; k <= t->next && n < size; ++k) {
        for (uint32_t s = 0; s < SCHED_STEPS; ++s) {
            if (t->order[s] != k) {
                continue;
            }
            const uint64_t tenths = (t->tick[s] - base) * 10u / SCHED_TICKS_PER_MS;
            n += (size_t)snprintf(out + n, size - n, "%s%s %u.%u", n ? ", " : "", STEP_NAMES[s],
                                  (unsigned)(tenths / 10u), (unsigned)(tenths % 10u));
        }
    }
    return n < size ? n : size - 1u;
}

static size_t Ms(char *out, size_t size, int32_t tenths) {
    return (size_t)snprintf(out, size, "%d.%d", (int)(tenths / 10), (int)(tenths % 10));
}

static uint32_t Bit(SchedQuestion q, bool b) {
    return 1u << (2u * (uint32_t)q + (b ? 1u : 0u));
}

/// Why a spinner held its core to the end, from the steps that never came before the watchdog.
static const char *Stall(const SchedTrace *t, SchedCaseId id) {
    if (!SchedBefore(t, SCHED_SPINNING, SCHED_WATCHDOG)) {
        return "the thread of 45 never started spinning";
    }
    switch (id) {
    case SCHED_INHERIT:
    case SCHED_BASE:
        if (!SchedBefore(t, SCHED_ASKED, SCHED_WATCHDOG)) {
            return "the thread of 44 never took the core from the thread of 45 to ask for the "
                   "mutex (no preemption, see the preemption case)";
        }
        if (!SchedBefore(t, SCHED_WOKE, SCHED_WATCHDOG)) {
            return "the owner (46) never ran after its sleep while the thread of 44 waited for "
                   "its mutex and the thread of 45 spun: it did not inherit 44";
        }
        return "the owner ran, but the mutex never reached the thread of 44";
    case SCHED_CHAIN:
        if (!SchedBefore(t, SCHED_ASKED, SCHED_WATCHDOG)) {
            return "the thread of 44 never took the core from the thread of 45 to ask for the "
                   "mutex (no preemption, see the preemption case)";
        }
        if (!SchedBefore(t, SCHED_WOKE, SCHED_WATCHDOG)) {
            return "the end of the chain (50) never ran after its sleep while the thread of 44 "
                   "waited, through the owner of 46, for its mutex: 44 did not pass down the "
                   "chain";
        }
        return "the end of the chain ran, but the mutexes never reached the thread of 44";
    case SCHED_PREEMPT:
        if (!SchedBefore(t, SCHED_SLEEPS, SCHED_WATCHDOG)) {
            return "the thread of 43 never took the core from the thread of 45 to start";
        }
        return "the thread of 43 never took the core back from the thread of 45 after its 10 ms "
               "sleep";
    case SCHED_MIGRATE:
        return "the thread of 46, free to run on cores 0 to 2, waited on core 1 behind the one "
               "spinning";
    default:
        return "the watchdog ended the wait";
    }
}

bool SchedJudge(SchedCaseId id, const SchedTrace *t, char *out, size_t size, uint32_t *answers) {
    *answers = 0;
    char line[320];
    Timeline(t, line, sizeof(line));
    if (t->failure[0] != '\0') {
        snprintf(out, size, "%s; %s", t->failure, line);
        return false;
    }
    char a[16];
    bool passed = false;
    switch (id) {
    case SCHED_INHERIT:
    case SCHED_CHAIN:
    case SCHED_PREEMPT: {
        passed = SchedBefore(t, SCHED_FLAG_SEEN, SCHED_WATCHDOG);
        if (!passed) {
            snprintf(out, size, "no flag before the watchdog (%u ms): %s; %s",
                     (unsigned)SCHED_WATCH_MS, Stall(t, id), line);
        } else if (id == SCHED_PREEMPT) {
            Ms(a, sizeof(a), SchedTenths(t, SCHED_SLEEPS, SCHED_FLAG_SEEN));
            snprintf(out, size,
                     "the thread of 45 saw the flag %s ms after the thread of 43 went to sleep "
                     "for %u ms; %s",
                     a, (unsigned)SCHED_SLEEP_MS, line);
        } else {
            Ms(a, sizeof(a), SchedTenths(t, SCHED_ASKED, SCHED_FLAG_SEEN));
            snprintf(out, size,
                     "the flag %s ms after the thread of 44 asked, while the thread of 45 spun: "
                     "%s; %s",
                     a,
                     id == SCHED_CHAIN ? "the end of the chain ran at 44"
                                       : "the owner ran at 44",
                     line);
        }
        break;
    }
    case SCHED_BASE: {
        const int32_t during = Has(t, SCHED_PRIORITY_DURING) ? t->value[SCHED_PRIORITY_DURING] : -1;
        const int32_t after = Has(t, SCHED_PRIORITY_AFTER) ? t->value[SCHED_PRIORITY_AFTER] : -1;
        if (!SchedBefore(t, SCHED_GOT, SCHED_WATCHDOG)) {
            snprintf(out, size, "the mutex never changed hands before the watchdog: %s; %s",
                     Stall(t, id), line);
        } else if (!SchedBefore(t, SCHED_FLAG_SEEN, SCHED_UNLOCKED)) {
            snprintf(out, size,
                     "after handing the mutex over the owner ran again before the thread of 45: "
                     "it kept the priority it inherited; it read %d then %d; %s",
                     (int)during, (int)after, line);
        } else if (after != SCHED_PRIORITY_OWNER) {
            snprintf(out, size,
                     "after handing the mutex over the owner reads its priority as %d, not its "
                     "own %d; %s",
                     (int)after, SCHED_PRIORITY_OWNER, line);
        } else {
            passed = true;
            if (during == SCHED_PRIORITY_WAITER || during == SCHED_PRIORITY_OWNER) {
                *answers |= Bit(SCHED_Q1, during == SCHED_PRIORITY_OWNER);
            }
            snprintf(out, size,
                     "after the handover the thread of 45 ran before the owner, which reads %d; "
                     "while the thread of 44 waited it read %d (%s); %s",
                     (int)after, (int)during,
                     during == SCHED_PRIORITY_WAITER  ? "Q1 A, the priority it runs at"
                     : during == SCHED_PRIORITY_OWNER ? "Q1 B, its own"
                                                      : "neither answer to Q1",
                     line);
        }
        break;
    }
    case SCHED_MIGRATE: {
        const int32_t core = Has(t, SCHED_CORE_MOVED) ? t->value[SCHED_CORE_MOVED] : -1;
        if (!SchedBefore(t, SCHED_FLAG_SEEN, SCHED_WATCHDOG)) {
            snprintf(out, size, "no flag before the watchdog (%u ms): %s, and ran on core %d "
                     "after it; %s",
                     (unsigned)SCHED_WATCH_MS, Stall(t, id), (int)core, line);
        } else if (core < 0 || core > 2 || core == SCHED_CASE_CORE) {
            snprintf(out, size,
                     "the thread of 46 says it ran on core %d before the watchdog, which is no "
                     "idle core of its mask; %s",
                     (int)core, line);
        } else {
            passed = true;
            snprintf(out, size, "the thread of 46 ran on core %d while the thread of 45 spun on "
                     "core %d; %s",
                     (int)core, SCHED_CASE_CORE, line);
        }
        break;
    }
    case SCHED_YIELD: {
        int32_t c[5];
        bool known = true;
        for (uint32_t i = 0; i < 5u; ++i) {
            const SchedValueId v = (SchedValueId)(SCHED_CORE_P0 + i);
            c[i] = Has(t, v) ? t->value[v] : -1;
            known &= c[i] >= 0 && c[i] <= 2;
        }
        if (!SchedBefore(t, SCHED_DONE, SCHED_WATCHDOG) || !known) {
            snprintf(out, size,
                     "the thread that yields did not finish before the watchdog, or read a core "
                     "outside 0 to 2: %d, %d, %d, alone %d, %d; %s",
                     (int)c[0], (int)c[1], (int)c[2], (int)c[3], (int)c[4], line);
        } else if (c[1] != c[0]) {
            snprintf(out, size, "svcSleepThread(0) moved the thread from core %d to core %d; %s",
                     (int)c[0], (int)c[1], line);
        } else if (!SchedHappened(t, SCHED_OTHER_RAN)) {
            snprintf(out, size, "the thread of 50 waiting on core %d never ran; %s",
                     SCHED_CASE_CORE, line);
        } else {
            passed = true;
            *answers |= Bit(SCHED_Q3, c[0] != SCHED_CASE_CORE) | Bit(SCHED_Q2, c[4] == c[3]);
            snprintf(out, size,
                     "with a thread of 50 waiting behind it on core %d it was on core %d (Q3 %s), "
                     "on core %d after svcSleepThread(0), on %d after svcSleepThread(-1); alone, "
                     "svcSleepThread(-1) took it from core %d to %d (Q2 %s); %s",
                     SCHED_CASE_CORE, (int)c[0], c[0] != SCHED_CASE_CORE ? "B" : "A", (int)c[1],
                     (int)c[2], (int)c[3], (int)c[4], c[4] == c[3] ? "B" : "A", line);
        }
        break;
    }
    default:
        snprintf(out, size, "no such case");
        break;
    }
    return passed;
}

static const char *const QUESTION_NAMES[SCHED_QUESTIONS] = {"Q1", "Q2", "Q3"};

void SchedSettle(uint32_t answers, uint8_t settled[SCHED_QUESTIONS]) {
    for (uint32_t q = 0; q < SCHED_QUESTIONS; ++q) {
        const uint32_t bits = answers >> (2u * q) & 3u;
        if (bits == 1u || bits == 2u) {
            settled[q] |= (uint8_t)bits;
        }
    }
}

size_t SchedSettled(const uint8_t settled[SCHED_QUESTIONS], char *out, size_t size) {
    size_t n = 0;
    out[0] = '\0';
    for (uint32_t q = 0; q < SCHED_QUESTIONS && n < size; ++q) {
        if (settled[q] == 0u) {
            continue;
        }
        n += (size_t)snprintf(out + n, size - n, "%s%s %s", n ? ", " : "", QUESTION_NAMES[q],
                              settled[q] == 3u ? "A and B, cases disagree"
                              : settled[q] == 1u ? "A"
                                                 : "B");
    }
    return n < size ? n : size - 1u;
}
