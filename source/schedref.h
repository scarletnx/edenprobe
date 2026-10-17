// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The sched suite's judge: what each case's threads leave behind (which steps happened, in
/// what order and when, and the priorities and cores they read), and what that must be for the
/// three rules the cases are about. No thread here, so both builds run it and the reference
/// suite pins it on traces made to order.
///
/// R1, inheritance: the owner of a kernel mutex runs at the best priority among the threads
/// waiting for it, down a chain of owners, and goes back to its own when it hands the mutex over.
/// R2, preemption: a thread that becomes ready at a better priority takes its core at once, even
/// from one in a loop with no supervisor call. R3, migration: a ready thread whose affinity mask
/// holds an idle core runs there rather than wait behind its core's owner.

/// Every thread of a case runs on this core, alone in its mask unless the case says otherwise;
/// the watchdog runs on its own; the main thread, which waits blocked, stays on core 0. None of
/// the three cores the program has ever holds a spinning thread and the watchdog together.
#define SCHED_CASE_CORE 1
#define SCHED_WATCH_CORE 2
#define SCHED_MAIN_CORE 0
#define SCHED_ALL_CORES 0x7u

#define SCHED_PRIORITY_WATCH 40
/// The waiter, the spinner and the owner of cases 1, 2 and 4: 44, 45 and 46. The thread that
/// preempts (case 3) is 43, the end of the chain (case 2) 50.
#define SCHED_PRIORITY_WAITER 44
#define SCHED_PRIORITY_SPINNER 45
#define SCHED_PRIORITY_OWNER 46

/// How long the watchdog lets a case run before it tells every spinner to stop, and how long the
/// owners sleep while they hold a mutex.
#define SCHED_WATCH_MS 1000u
#define SCHED_HOLD_MS 50u
#define SCHED_SLEEP_MS 10u
/// The console's system counter.
#define SCHED_TICKS_PER_MS 19200u

typedef enum {
    SCHED_HELD,        ///< the owner holds its mutex
    SCHED_HELD_TAIL,   ///< the end of the chain holds its mutex
    SCHED_ASKS_TAIL,   ///< the owner asks for the end of the chain's mutex
    SCHED_SPINNING,    ///< the spinner is in its loop
    SCHED_ASKED,       ///< the waiter asks for the owner's mutex
    SCHED_WOKE,        ///< the owner (or the sleeper) is past its sleep and running
    SCHED_GOT,         ///< the waiter has the mutex
    SCHED_FLAG_SET,    ///< the flag the spinner reads is set
    SCHED_FLAG_SEEN,   ///< the spinner saw it
    SCHED_STOPPED,     ///< the spinner stopped at the watchdog's word instead
    SCHED_UNLOCKED,    ///< the owner runs again after handing its mutex over
    SCHED_SLEEPS,      ///< the sleeper goes to sleep
    SCHED_RAN,         ///< the thread that moves (case 5) or yields (case 6) runs
    SCHED_OTHER_RAN,   ///< the thread waiting behind the one that yields runs
    SCHED_DONE,        ///< the thread that yields is done
    SCHED_WATCHDOG,    ///< the watchdog ended the wait
    SCHED_STEPS
} SchedStep;

typedef enum {
    SCHED_PRIORITY_DURING, ///< the owner's svcGetThreadPriority while the waiter waits (Q1)
    SCHED_PRIORITY_AFTER,  ///< and after it handed the mutex over
    SCHED_CORE_MOVED,      ///< the core the thread of case 5 ran on
    SCHED_CORE_P0,         ///< case 6: the core once the other thread waits behind it
    SCHED_CORE_P1,         ///< after svcSleepThread(0)
    SCHED_CORE_P2,         ///< after svcSleepThread(-1)
    SCHED_CORE_ALONE0,     ///< alone, before svcSleepThread(-1)
    SCHED_CORE_ALONE1,     ///< and after it
    SCHED_VALUES
} SchedValueId;

/// What one case left. Written by its threads as they go, so every field is set with an atomic
/// store; read once they have all exited.
typedef struct {
    uint32_t next;
    uint32_t order[SCHED_STEPS]; ///< 1 for the first step that happened, 0 for one that did not
    uint64_t tick[SCHED_STEPS];
    int32_t value[SCHED_VALUES];
    uint32_t has; ///< a bit per value read
    /// Why the case could not run or finish: a thread not made, or one that never exited.
    char failure[160];
} SchedTrace;

void SchedReset(SchedTrace *t);
/// Notes that @p step happened now (@p tick, the system counter); the first time only.
void SchedMark(SchedTrace *t, SchedStep step, uint64_t tick);
void SchedValue(SchedTrace *t, SchedValueId id, int32_t value);
bool SchedHappened(const SchedTrace *t, SchedStep step);
/// @p a happened, and @p b either did not or came later.
bool SchedBefore(const SchedTrace *t, SchedStep a, SchedStep b);

typedef enum {
    SCHED_INHERIT,
    SCHED_CHAIN,
    SCHED_PREEMPT,
    SCHED_BASE,
    SCHED_MIGRATE,
    SCHED_YIELD,
    SCHED_CASES
} SchedCaseId;

const char *SchedCaseName(SchedCaseId id);

/// The open questions, numbered as they were asked; Q3 is new.
typedef enum {
    SCHED_Q1, ///< svcGetThreadPriority during inheritance: A the effective 44, B the base 46
    SCHED_Q2, ///< svcSleepThread(-1) alone with idle cores in the mask: A moves, B stays
    SCHED_Q3, ///< a running thread whose core has another ready: A stays until it yields, B an
              ///< idle core takes it at once
    SCHED_QUESTIONS
} SchedQuestion;

/// The verdict on case @p id from what it left, its line in @p out, and in @p answers a bit per
/// question and answer it saw (bit 2q for A, 2q+1 for B).
bool SchedJudge(SchedCaseId id, const SchedTrace *t, char *out, size_t size, uint32_t *answers);

/// Adds a passing case's answers into @p settled (bit 0 A, bit 1 B per question).
void SchedSettle(uint32_t answers, uint8_t settled[SCHED_QUESTIONS]);
/// The note's text: each question's answer, or that two cases answered it apart. Returns the
/// length.
size_t SchedSettled(const uint8_t settled[SCHED_QUESTIONS], char *out, size_t size);

/// Milliseconds between two steps that both happened, to a tenth; -1 when one did not.
int32_t SchedTenths(const SchedTrace *t, SchedStep from, SchedStep to);

#ifdef __cplusplus
}
#endif
