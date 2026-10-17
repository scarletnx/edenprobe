// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "../check.h"
#include "../report.h"
#include "../schedref.h"
#include "tests.h"

// The kernel's scheduler, as a program sees it: threads of chosen priorities on one core, a
// mutex of the kernel's between them, and a thread in a loop with no supervisor call holding
// the core. Each case runs its threads, a watchdog on another core tells every loop to stop
// after a second, and schedref.c judges what the threads left: a machine without a rule fails
// the case and goes on, it does not hang. The main thread waits, blocked, on core 0.

static const char *const SUITE = "sched";
static const char *const STARTS = "threads start on the core and at the priority asked";

#define THREADS_MAX 5u
#define STACK_SIZE 0x4000u
#define CORE_ONE (1u << SCHED_CASE_CORE)
/// After the watchdog, how long the main thread still waits for a case's threads to exit.
#define GRACE_MS 1000u

static uint8_t stacks[THREADS_MAX][STACK_SIZE] __attribute__((aligned(0x1000)));
static Thread threads[THREADS_MAX];
static bool made[THREADS_MAX], started[THREADS_MAX];
static SchedTrace trace;
static Mutex first, second;
// Set with atomic stores and read with atomic loads, from threads on three cores.
static uint32_t flag, stop, ready, finished;
static uint64_t watch_start;
// A thread of an earlier case never exited: its core may still be taken, and its stack too.
static bool stuck;
static uint8_t settled[SCHED_QUESTIONS];

static void Mark(SchedStep step) {
    SchedMark(&trace, step, armGetSystemTick());
}

static bool Load(const uint32_t *p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE) != 0u;
}

static void Store(uint32_t *p) {
    __atomic_store_n(p, 1u, __ATOMIC_RELEASE);
}

static int32_t Core(void) {
    return (int32_t)svcGetCurrentProcessorNumber();
}

static int32_t Priority(void) {
    s32 priority = -1;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    return priority;
}

static void SleepMs(uint32_t ms) {
    svcSleepThread((s64)ms * 1000000);
}

// ---- the threads

static void Watchdog(void *arg) {
    while (!Load(&finished)) {
        if (armTicksToNs(armGetSystemTick() - watch_start) >= SCHED_WATCH_MS * 1000000ull) {
            Mark(SCHED_WATCHDOG);
            Store(&stop);
            return;
        }
        SleepMs(1);
    }
}

// Reads the flag with no supervisor call in the loop: only preemption takes its core away.
static void Spinner(void *arg) {
    Mark(SCHED_SPINNING);
    while (!Load(&flag)) {
        if (Load(&stop)) {
            Mark(SCHED_STOPPED);
            return;
        }
    }
    Mark(SCHED_FLAG_SEEN);
}

// Holds the mutex and sleeps; once it runs again it hands the mutex over.
static void Owner(void *arg) {
    mutexLock(&first);
    Mark(SCHED_HELD);
    SleepMs(SCHED_HOLD_MS);
    Mark(SCHED_WOKE);
    SchedValue(&trace, SCHED_PRIORITY_DURING, Priority());
    mutexUnlock(&first);
    Mark(SCHED_UNLOCKED);
    SchedValue(&trace, SCHED_PRIORITY_AFTER, Priority());
}

// Asks for the mutex, and sets the flag the spinner reads once it has it.
static void Asker(void *arg) {
    Mark(SCHED_ASKED);
    mutexLock(&first);
    Mark(SCHED_GOT);
    Store(&flag);
    Mark(SCHED_FLAG_SET);
    mutexUnlock(&first);
}

static void Tail(void *arg) {
    mutexLock(&second);
    Mark(SCHED_HELD_TAIL);
    SleepMs(SCHED_HOLD_MS);
    Mark(SCHED_WOKE);
    mutexUnlock(&second);
}

static void Middle(void *arg) {
    mutexLock(&first);
    Mark(SCHED_HELD);
    Mark(SCHED_ASKS_TAIL);
    mutexLock(&second);
    mutexUnlock(&second);
    mutexUnlock(&first);
    Mark(SCHED_UNLOCKED);
}

static void Sleeper(void *arg) {
    Mark(SCHED_SLEEPS);
    SleepMs(SCHED_SLEEP_MS);
    Mark(SCHED_WOKE);
    Store(&flag);
    Mark(SCHED_FLAG_SET);
}

static void Mover(void *arg) {
    SchedValue(&trace, SCHED_CORE_MOVED, Core());
    Mark(SCHED_RAN);
    Store(&flag);
    Mark(SCHED_FLAG_SET);
}

// Keeps its core until the thread behind it is ready, then yields both ways.
static void Yielder(void *arg) {
    Mark(SCHED_RAN);
    while (!Load(&ready) && !Load(&stop)) {
    }
    SchedValue(&trace, SCHED_CORE_P0, Core());
    svcSleepThread(YieldType_WithoutCoreMigration);
    SchedValue(&trace, SCHED_CORE_P1, Core());
    svcSleepThread(YieldType_WithCoreMigration);
    SchedValue(&trace, SCHED_CORE_P2, Core());
    Mark(SCHED_DONE);
}

static void Behind(void *arg) {
    Mark(SCHED_OTHER_RAN);
}

static void Alone(void *arg) {
    SchedValue(&trace, SCHED_CORE_ALONE0, Core());
    svcSleepThread(YieldType_WithCoreMigration);
    SchedValue(&trace, SCHED_CORE_ALONE1, Core());
}

// ---- making, starting and ending a case's threads

static bool Make(uint32_t i, ThreadFunc entry, int priority, int core, uint32_t mask) {
    if (trace.failure[0] != '\0') {
        return false;
    }
    Result rc = threadCreate(&threads[i], entry, NULL, stacks[i], STACK_SIZE, priority, core);
    made[i] = R_SUCCEEDED(rc);
    const char *step = "threadCreate";
    if (made[i] && mask != (1u << core)) {
        step = "svcSetThreadCoreMask";
        rc = svcSetThreadCoreMask(threads[i].handle, core, mask);
    }
    if (R_FAILED(rc)) {
        snprintf(trace.failure, sizeof(trace.failure),
                 "%s for a thread of %d on core %d, mask 0x%x, failed: 0x%x", step, priority,
                 core, (unsigned)mask, (unsigned)rc);
        return false;
    }
    return true;
}

static bool Start(uint32_t i) {
    if (trace.failure[0] != '\0') {
        return false;
    }
    const Result rc = threadStart(&threads[i]);
    started[i] = R_SUCCEEDED(rc);
    if (!started[i]) {
        snprintf(trace.failure, sizeof(trace.failure), "threadStart failed: 0x%x",
                 (unsigned)rc);
    }
    return started[i];
}

/// Waits up to @p ms for a step: the threads run on other cores, and a step that never comes is
/// the case's finding, never a reason to stop.
static void WaitFor(SchedStep step, uint32_t ms) {
    for (uint32_t i = 0; i < ms * 10u && !SchedHappened(&trace, step); ++i) {
        svcSleepThread(100000);
    }
}

static void Begin(void) {
    SchedReset(&trace);
    memset(made, 0, sizeof(made));
    memset(started, 0, sizeof(started));
    flag = stop = ready = finished = 0;
    mutexInit(&first);
    mutexInit(&second);
    watch_start = armGetSystemTick();
    if (Make(0, Watchdog, SCHED_PRIORITY_WATCH, SCHED_WATCH_CORE, 1u << SCHED_WATCH_CORE)) {
        Start(0);
    }
}

/// Waits for threads @p from to @p to - 1 to exit, until the watchdog's second and the grace
/// after it have passed. A thread that never exits keeps its stack and its handle, and the
/// family stops.
static void WaitExit(uint32_t from, uint32_t to) {
    const uint64_t limit = (SCHED_WATCH_MS + GRACE_MS) * 1000000ull;
    for (uint32_t i = from; i < to; ++i) {
        if (!started[i]) {
            continue;
        }
        const uint64_t spent = armTicksToNs(armGetSystemTick() - watch_start);
        const Result rc =
            svcWaitSynchronizationSingle(threads[i].handle, spent < limit ? limit - spent : 0u);
        if (R_FAILED(rc)) {
            stuck = true;
            if (trace.failure[0] == '\0') {
                snprintf(trace.failure, sizeof(trace.failure),
                         "thread %u of the case never exited (0x%x), so the rest of the family "
                         "is skipped",
                         (unsigned)i, (unsigned)rc);
            }
            started[i] = false;
            made[i] = false; // never closed: it may still run on its stack
        }
    }
}

/// Every thread of the case exited, the watchdog too, and all of them closed.
static void End(void) {
    if (trace.failure[0] != '\0') {
        // Set-up stopped half way: what started still has to get out of its loops.
        Store(&stop);
    }
    WaitExit(1u, THREADS_MAX);
    Store(&finished);
    WaitExit(0u, 1u);
    for (uint32_t i = 0; i < THREADS_MAX; ++i) {
        if (made[i]) {
            threadClose(&threads[i]);
            made[i] = false;
        }
    }
}

// ---- the cases

static void RunInheritance(void) {
    Begin();
    if (Make(1, Owner, SCHED_PRIORITY_OWNER, SCHED_CASE_CORE, CORE_ONE) &&
        Make(2, Spinner, SCHED_PRIORITY_SPINNER, SCHED_CASE_CORE, CORE_ONE) &&
        Make(3, Asker, SCHED_PRIORITY_WAITER, SCHED_CASE_CORE, CORE_ONE) && Start(1)) {
        WaitFor(SCHED_HELD, 100u);
        if (Start(2)) {
            WaitFor(SCHED_SPINNING, 100u);
            Start(3);
        }
    }
    End();
}

static void RunChain(void) {
    Begin();
    if (Make(1, Tail, 50, SCHED_CASE_CORE, CORE_ONE) &&
        Make(2, Middle, SCHED_PRIORITY_OWNER, SCHED_CASE_CORE, CORE_ONE) &&
        Make(3, Spinner, SCHED_PRIORITY_SPINNER, SCHED_CASE_CORE, CORE_ONE) &&
        Make(4, Asker, SCHED_PRIORITY_WAITER, SCHED_CASE_CORE, CORE_ONE) && Start(1)) {
        WaitFor(SCHED_HELD_TAIL, 100u);
        if (Start(2)) {
            WaitFor(SCHED_ASKS_TAIL, 100u);
            // Long enough for the middle owner to block on the tail's mutex.
            SleepMs(2u);
            if (Start(3)) {
                WaitFor(SCHED_SPINNING, 100u);
                Start(4);
            }
        }
    }
    End();
}

static void RunPreemption(void) {
    Begin();
    if (Make(1, Spinner, SCHED_PRIORITY_SPINNER, SCHED_CASE_CORE, CORE_ONE) &&
        Make(2, Sleeper, 43, SCHED_CASE_CORE, CORE_ONE) && Start(1)) {
        WaitFor(SCHED_SPINNING, 100u);
        Start(2);
    }
    End();
}

static void RunMigration(void) {
    Begin();
    if (Make(1, Spinner, SCHED_PRIORITY_SPINNER, SCHED_CASE_CORE, CORE_ONE) &&
        Make(2, Mover, SCHED_PRIORITY_OWNER, SCHED_CASE_CORE, SCHED_ALL_CORES) && Start(1)) {
        WaitFor(SCHED_SPINNING, 100u);
        Start(2);
    }
    End();
}

static void RunYield(void) {
    Begin();
    if (Make(1, Yielder, SCHED_PRIORITY_WAITER, SCHED_CASE_CORE, SCHED_ALL_CORES) &&
        Make(2, Behind, 50, SCHED_CASE_CORE, CORE_ONE) && Start(1)) {
        WaitFor(SCHED_RAN, 100u);
        if (Start(2)) {
            Store(&ready);
        }
    }
    // The same thread again, alone: nothing else ready on its core.
    WaitExit(1u, 3u);
    if (!stuck && Make(3, Alone, SCHED_PRIORITY_WAITER, SCHED_CASE_CORE, SCHED_ALL_CORES)) {
        Start(3);
    }
    End();
}

static void (*const RUN[SCHED_CASES])(void) = {
    RunInheritance, RunChain, RunPreemption, RunInheritance, RunMigration, RunYield,
};

// ---- the bring-up

static int32_t seen_core[3], seen_priority[3];

static void ReadWhere(void *arg) {
    const uintptr_t i = (uintptr_t)arg;
    seen_core[i] = Core();
    seen_priority[i] = Priority();
}

/// One thread on each core at the priorities the cases use at the edges: 50 on core 0, 43 on
/// the case core, 40 on the watchdog's.
static bool StartsWhereAsked(char *why, size_t why_size, int main_core, int main_priority) {
    static const int PRIORITY[3] = {50, 43, SCHED_PRIORITY_WATCH};
    bool ok = true;
    char what[64] = "";
    for (uint32_t i = 0; i < 3u; ++i) {
        seen_core[i] = seen_priority[i] = -1;
        Result rc = threadCreate(&threads[i], ReadWhere, (void *)(uintptr_t)i, stacks[i],
                                 STACK_SIZE, PRIORITY[i], (int)i);
        if (R_SUCCEEDED(rc)) {
            rc = threadStart(&threads[i]);
            if (R_SUCCEEDED(rc)) {
                rc = svcWaitSynchronizationSingle(threads[i].handle, 1000000000ull);
            }
            if (R_FAILED(rc)) {
                stuck = true; // started and never seen to end: leave its stack alone
            } else {
                threadClose(&threads[i]);
            }
        }
        if (R_FAILED(rc) && what[0] == '\0') {
            snprintf(what, sizeof(what), "the thread of %d on core %u: 0x%x", PRIORITY[i],
                     (unsigned)i, (unsigned)rc);
        }
        ok &= R_SUCCEEDED(rc) && seen_core[i] == (int32_t)i && seen_priority[i] == PRIORITY[i];
    }
    snprintf(why, why_size,
             "the main thread on core %d at %d; threads of 50, 43 and 40 read cores %d, %d, %d "
             "and priorities %d, %d, %d%s%s",
             main_core, main_priority, (int)seen_core[0], (int)seen_core[1], (int)seen_core[2],
             (int)seen_priority[0], (int)seen_priority[1], (int)seen_priority[2],
             what[0] ? "; " : "", what);
    return ok;
}

static void NoteSettled(void) {
    char text[REPORT_NOTE_MAX - 32];
    if (SchedSettled(settled, text, sizeof(text)) > 0u) {
        ReportNote("sched: the passing cases answer %s", text);
    }
}

void RunSched(void) {
    memset(settled, 0, sizeof(settled));
    stuck = false;
    // The main thread waits on core 0 for the whole family, whatever core it was on.
    s32 ideal = 0;
    u64 mask = 0;
    const bool pinned = R_SUCCEEDED(svcGetThreadCoreMask(&ideal, &mask, CUR_THREAD_HANDLE)) &&
                        R_SUCCEEDED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, SCHED_MAIN_CORE,
                                                         1u << SCHED_MAIN_CORE));
    bool open = false;
    if (ReportStart(SUITE, STARTS)) {
        char why[240];
        open = pinned && StartsWhereAsked(why, sizeof(why), Core(), Priority());
        ReportCase(SUITE, STARTS, CHECK_NONE, open,
                   pinned ? why : "the main thread could not be put on core 0");
    }
    for (uint32_t i = 0; i < SCHED_CASES; ++i) {
        const char *const name = SchedCaseName((SchedCaseId)i);
        if (!open || stuck) {
            ReportSkip(SUITE, name, REPORT_REASON_DEPENDS,
                       stuck ? "a thread of an earlier case never exited"
                             : "threads could not be started where the cases need them");
            continue;
        }
        if (!ReportStart(SUITE, name)) {
            continue;
        }
        RUN[i]();
        char detail[480];
        uint32_t answers = 0;
        const bool passed = SchedJudge((SchedCaseId)i, &trace, detail, sizeof(detail), &answers);
        if (passed) {
            SchedSettle(answers, settled);
        }
        ReportCase(SUITE, name, CHECK_POSITION, passed, detail);
    }
    if (pinned) {
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, ideal, (u32)mask);
    }
    NoteSettled();
}
