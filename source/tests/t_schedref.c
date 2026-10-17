// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../report.h"
#include "../schedref.h"
#include "tests.h"

// The kernel family's judge on traces made to order: what the threads of each case leave on a
// machine with the three rules, and on one without each of them. The steps are written in the
// order they would happen there, each at a time in tenths of a millisecond.

static const char *const SUITE = "reference";

static SchedTrace t;
static char said[480];
static uint32_t answers;

static void At(SchedStep step, uint32_t tenths) {
    SchedMark(&t, step, (uint64_t)tenths * SCHED_TICKS_PER_MS / 10u);
}

static bool Judge(SchedCaseId id) {
    return SchedJudge(id, &t, said, sizeof(said), &answers);
}

/// Cases 1 and 4 where the rules hold: the waiter takes the core, blocks, the owner inherits 44,
/// wakes at 50 ms, hands the mutex over and drops back below the spinner.
static void Inherited(int32_t during, int32_t after) {
    SchedReset(&t);
    At(SCHED_HELD, 0);
    At(SCHED_SPINNING, 2);
    At(SCHED_ASKED, 3);
    At(SCHED_WOKE, 500);
    SchedValue(&t, SCHED_PRIORITY_DURING, during);
    At(SCHED_GOT, 501);
    At(SCHED_FLAG_SET, 501);
    At(SCHED_FLAG_SEEN, 502);
    At(SCHED_UNLOCKED, 503);
    SchedValue(&t, SCHED_PRIORITY_AFTER, after);
}

static void CheckBasics(void) {
    SchedReset(&t);
    At(SCHED_WOKE, 10);
    At(SCHED_HELD, 20);
    At(SCHED_WOKE, 30); // a second time changes nothing
    const bool order = SchedBefore(&t, SCHED_WOKE, SCHED_HELD) &&
                       !SchedBefore(&t, SCHED_HELD, SCHED_WOKE) &&
                       SchedBefore(&t, SCHED_HELD, SCHED_WATCHDOG) &&
                       !SchedBefore(&t, SCHED_WATCHDOG, SCHED_HELD) &&
                       SchedTenths(&t, SCHED_WOKE, SCHED_HELD) == 10 &&
                       SchedTenths(&t, SCHED_WOKE, SCHED_GOT) == -1 && t.order[SCHED_WOKE] == 1u;
    CHECK_TRUE(SUITE, "sched: a step keeps its first place and time, and one not taken comes last",
               CHECK_NONE, order, "order %u %u, tenths %d", (unsigned)t.order[SCHED_WOKE],
               (unsigned)t.order[SCHED_HELD], (int)SchedTenths(&t, SCHED_WOKE, SCHED_HELD));
}

static void CheckInheritance(void) {
    Inherited(44, 46);
    const bool held = Judge(SCHED_INHERIT) &&
                      strcmp(said, "the flag 49.9 ms after the thread of 44 asked, while the "
                                   "thread of 45 spun: the owner ran at 44; held 0.0, spinning "
                                   "0.2, asked 0.3, woke 50.0, got 50.1, flag set 50.1, flag "
                                   "seen 50.2, unlocked 50.3") == 0;
    char held_said[480];
    memcpy(held_said, said, sizeof(said));

    // No inheritance: the owner wakes at 46 behind the spinner and runs only once the watchdog
    // stops it.
    SchedReset(&t);
    At(SCHED_HELD, 0);
    At(SCHED_SPINNING, 2);
    At(SCHED_ASKED, 3);
    At(SCHED_WATCHDOG, 10000);
    At(SCHED_STOPPED, 10001);
    At(SCHED_WOKE, 10002);
    At(SCHED_GOT, 10003);
    At(SCHED_FLAG_SET, 10003);
    At(SCHED_UNLOCKED, 10004);
    const bool lost = !Judge(SCHED_INHERIT) &&
                      strstr(said, "no flag before the watchdog (1000 ms): the owner (46) never "
                                   "ran after its sleep while the thread of 44 waited for its "
                                   "mutex and the thread of 45 spun: it did not inherit 44") ==
                          said;
    char lost_said[480];
    memcpy(lost_said, said, sizeof(said));

    // No preemption: the waiter never gets the core to ask.
    SchedReset(&t);
    At(SCHED_HELD, 0);
    At(SCHED_SPINNING, 2);
    At(SCHED_WATCHDOG, 10000);
    At(SCHED_STOPPED, 10001);
    At(SCHED_ASKED, 10002);
    const bool late = !Judge(SCHED_INHERIT) && strstr(said, "(no preemption") != NULL;

    CHECK_TRUE(SUITE, "sched: inheritance passes on the console's order and names what stalled",
               CHECK_NONE, held && lost && late, "held \"%.140s\"; lost \"%.160s\"; late %d",
               held_said, lost_said, late);
}

static void CheckChain(void) {
    SchedReset(&t);
    At(SCHED_HELD_TAIL, 0);
    At(SCHED_HELD, 1);
    At(SCHED_ASKS_TAIL, 1);
    At(SCHED_SPINNING, 30);
    At(SCHED_ASKED, 31);
    At(SCHED_WOKE, 500);
    At(SCHED_UNLOCKED, 501);
    At(SCHED_GOT, 501);
    At(SCHED_FLAG_SET, 501);
    At(SCHED_FLAG_SEEN, 502);
    const bool chained = Judge(SCHED_CHAIN) && strstr(said, "the end of the chain ran at 44") != NULL;
    // One level only: the middle owner gets 44, the end of the chain keeps 46 and sleeps on.
    SchedReset(&t);
    At(SCHED_HELD_TAIL, 0);
    At(SCHED_HELD, 1);
    At(SCHED_ASKS_TAIL, 1);
    At(SCHED_SPINNING, 30);
    At(SCHED_ASKED, 31);
    At(SCHED_WATCHDOG, 10000);
    At(SCHED_STOPPED, 10001);
    At(SCHED_WOKE, 10002);
    const bool broken =
        !Judge(SCHED_CHAIN) && strstr(said, "44 did not pass down the chain") != NULL;
    CHECK_TRUE(SUITE, "sched: the chain passes when its end runs at 44, and fails one level deep",
               CHECK_NONE, chained && broken, "chained %d, broken %d: \"%.200s\"", chained,
               broken, said);
}

static void CheckPreemption(void) {
    SchedReset(&t);
    At(SCHED_SPINNING, 0);
    At(SCHED_SLEEPS, 2);
    At(SCHED_WOKE, 103);
    At(SCHED_FLAG_SET, 103);
    At(SCHED_FLAG_SEEN, 106);
    const bool taken = Judge(SCHED_PREEMPT) &&
                       strcmp(said, "the thread of 45 saw the flag 10.4 ms after the thread of 43 "
                                    "went to sleep for 10 ms; spinning 0.0, sleeps 0.2, woke 10.3, "
                                    "flag set 10.3, flag seen 10.6") == 0;
    char taken_said[480];
    memcpy(taken_said, said, sizeof(said));
    SchedReset(&t);
    At(SCHED_SPINNING, 0);
    At(SCHED_SLEEPS, 2);
    At(SCHED_WATCHDOG, 10000);
    At(SCHED_STOPPED, 10001);
    At(SCHED_WOKE, 10002);
    At(SCHED_FLAG_SET, 10002);
    const bool held = !Judge(SCHED_PREEMPT) &&
                      strstr(said, "never took the core back from the thread of 45 after its "
                                   "10 ms sleep") != NULL;
    CHECK_TRUE(SUITE, "sched: preemption gives the time the core changed hands, or that it never did",
               CHECK_NONE, taken && held, "taken \"%.150s\"; held %d: \"%.150s\"", taken_said,
               held, said);
}

static void CheckBase(void) {
    Inherited(44, 46);
    const bool effective = Judge(SCHED_BASE) && answers == 1u << (2u * SCHED_Q1) &&
                           strstr(said, "which reads 46; while the thread of 44 waited it read "
                                        "44 (Q1 A") != NULL;
    Inherited(46, 46);
    const bool own = Judge(SCHED_BASE) && answers == 2u << (2u * SCHED_Q1);
    // Kept the inherited priority: after the handover it runs again before the spinner.
    Inherited(44, 44);
    t.order[SCHED_UNLOCKED] = 0u;
    At(SCHED_UNLOCKED, 501);
    t.order[SCHED_FLAG_SEEN] = 0u;
    At(SCHED_FLAG_SEEN, 502);
    const bool kept = !Judge(SCHED_BASE) && strstr(said, "it kept the priority it inherited") != NULL;
    // Back in the order, but it reads the priority it inherited.
    Inherited(44, 44);
    const bool reads = !Judge(SCHED_BASE) && strstr(said, "reads its priority as 44, not its own "
                                                          "46") != NULL;
    CHECK_TRUE(SUITE, "sched: the owner's drop back is read from the order, and Q1 from its priority",
               CHECK_NONE, effective && own && kept && reads,
               "effective %d, own %d, kept %d, reads %d: \"%.200s\"", effective, own, kept, reads,
               said);
}

static void CheckMigration(void) {
    SchedReset(&t);
    At(SCHED_SPINNING, 0);
    SchedValue(&t, SCHED_CORE_MOVED, 2);
    At(SCHED_RAN, 1);
    At(SCHED_FLAG_SET, 1);
    At(SCHED_FLAG_SEEN, 2);
    const bool moved = Judge(SCHED_MIGRATE) &&
                       strstr(said, "the thread of 46 ran on core 2 while the thread of 45 spun "
                                    "on core 1") == said;
    SchedReset(&t);
    At(SCHED_SPINNING, 0);
    At(SCHED_WATCHDOG, 10000);
    At(SCHED_STOPPED, 10001);
    SchedValue(&t, SCHED_CORE_MOVED, 1);
    At(SCHED_RAN, 10002);
    At(SCHED_FLAG_SET, 10002);
    const bool waited = !Judge(SCHED_MIGRATE) &&
                        strstr(said, "waited on core 1 behind the one spinning, and ran on core 1 "
                                     "after it") != NULL;
    // In time, but on the spinner's own core: no idle core took it, and the order is wrong.
    SchedReset(&t);
    At(SCHED_SPINNING, 0);
    SchedValue(&t, SCHED_CORE_MOVED, 1);
    At(SCHED_RAN, 1);
    At(SCHED_FLAG_SET, 1);
    At(SCHED_FLAG_SEEN, 2);
    const bool same = !Judge(SCHED_MIGRATE) && strstr(said, "says it ran on core 1") != NULL;
    CHECK_TRUE(SUITE, "sched: migration passes on an idle core and fails behind the spinner",
               CHECK_NONE, moved && waited && same, "moved %d, waited %d, same %d: \"%.200s\"",
               moved, waited, same, said);
}

static void Yielded(int32_t p0, int32_t p1, int32_t p2, int32_t alone0, int32_t alone1) {
    SchedReset(&t);
    At(SCHED_RAN, 0);
    At(SCHED_OTHER_RAN, 1);
    SchedValue(&t, SCHED_CORE_P0, p0);
    SchedValue(&t, SCHED_CORE_P1, p1);
    SchedValue(&t, SCHED_CORE_P2, p2);
    At(SCHED_DONE, 2);
    SchedValue(&t, SCHED_CORE_ALONE0, alone0);
    SchedValue(&t, SCHED_CORE_ALONE1, alone1);
}

static void CheckYield(void) {
    // The scheduler reden ports: an idle core takes the yielder as soon as a thread waits behind
    // it, and svcSleepThread(-1) alone leaves it where it is.
    Yielded(2, 2, 2, 1, 1);
    const bool ported = Judge(SCHED_YIELD) &&
                        answers == (2u << (2u * SCHED_Q3) | 2u << (2u * SCHED_Q2)) &&
                        strstr(said, "it was on core 2 (Q3 B), on core 2 after svcSleepThread(0), "
                                     "on 2 after svcSleepThread(-1); alone, svcSleepThread(-1) "
                                     "took it from core 1 to 1 (Q2 B)") != NULL;
    char ported_said[480];
    memcpy(ported_said, said, sizeof(said));
    // The handback's reading: it stays until it yields, and -1 always moves it.
    Yielded(1, 1, 0, 1, 2);
    const bool asked = Judge(SCHED_YIELD) &&
                       answers == (1u << (2u * SCHED_Q3) | 1u << (2u * SCHED_Q2));
    // svcSleepThread(0) moving the thread is the failure.
    Yielded(1, 2, 2, 1, 1);
    const bool moved = !Judge(SCHED_YIELD) &&
                       strstr(said, "svcSleepThread(0) moved the thread from core 1 to core 2") ==
                           said;
    // A core outside the three, or the yielder never done.
    Yielded(1, 1, 3, 1, 1);
    const bool outside = !Judge(SCHED_YIELD);
    CHECK_TRUE(SUITE, "sched: svcSleepThread(0) must keep the core, and the cores answer Q2 and Q3",
               CHECK_NONE, ported && asked && moved && outside,
               "ported \"%.200s\"; asked %d, moved %d, outside %d", ported_said, asked, moved,
               outside);
}

static void CheckNote(void) {
    SchedReset(&t);
    snprintf(t.failure, sizeof(t.failure), "threadCreate for a thread of 43 on core 1 failed");
    At(SCHED_SPINNING, 0);
    const bool failed = !Judge(SCHED_PREEMPT) &&
                        strcmp(said, "threadCreate for a thread of 43 on core 1 failed; "
                                     "spinning 0.0") == 0;
    uint8_t settled[SCHED_QUESTIONS] = {0};
    char note[160];
    const bool empty = SchedSettled(settled, note, sizeof(note)) == 0u;
    SchedSettle(1u << (2u * SCHED_Q1), settled);
    SchedSettle(2u << (2u * SCHED_Q3) | 2u << (2u * SCHED_Q2), settled);
    SchedSettle(3u << (2u * SCHED_Q2), settled); // both answers fit: settles nothing
    SchedSettled(settled, note, sizeof(note));
    const bool agreed = strcmp(note, "Q1 A, Q2 B, Q3 B") == 0;
    SchedSettle(1u << (2u * SCHED_Q3), settled);
    SchedSettled(settled, note, sizeof(note));
    CHECK_TRUE(SUITE, "sched: a case that could not run says why, and the note adds the cases up",
               CHECK_NONE,
               failed && empty && agreed && strcmp(note, "Q1 A, Q2 B, Q3 A and B, cases disagree") == 0,
               "failed %d \"%.120s\"; note \"%s\"", failed, said, note);
}

void RunReferenceSched(void) {
    CheckBasics();
    CheckInheritance();
    CheckChain();
    CheckPreemption();
    CheckBase();
    CheckMigration();
    CheckYield();
    CheckNote();
}
