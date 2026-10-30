# The kernel family

## Index

* [How a case runs](#how-a-case-runs)
* [The cases](#the-cases)
* [The open questions](#the-open-questions)
* [What Eden does](#what-eden-does)
* [The judge, pinned](#the-judge-pinned)

The kernel's scheduler as a program sees it: threads of chosen priorities on one core, a mutex of
the kernel's between them, and a thread in a loop with no supervisor call holding the core. Only
the word `kernel` runs it (see [The mode file](../ModeFile.md)), since no console has yet shown
that its cases never hang; the start screen has no row for it, and a file that names it keeps it
whatever the screen changes.

- **sched**, seven cases. One says threads start on the core and at the priority asked (none
  tier); the six after it each run a handful of threads and judge the order they ran in
  (position tier: which came first is the verdict, the time it took is not). If the first fails,
  the six are skipped as `depends`.

A game hung at boot under a machine whose kernel lacked two of these rules: a thread spinning
with no supervisor call on core 0, at 45, held the core while a thread of 43 and the owner (46)
of a mutex a thread of 44 wanted both waited, and cores 1 and 2 sat idle. The three rules:

- **R1, inheritance.** The owner of a kernel mutex runs at the best priority among the threads
  waiting for it, down a chain of owners, and goes back to its own when it hands the mutex over.
- **R2, preemption.** A thread that becomes ready at a better priority takes its core at once,
  even from a thread in a loop with no supervisor call.
- **R3, migration.** A ready thread whose affinity mask holds an idle core runs there, rather than
  wait behind its own core's owner.

## How a case runs

Every thread of a case runs on core 1, alone in its mask unless the case says otherwise. A
watchdog runs on core 2 at priority 40, sleeping a millisecond at a time; after a second it tells
every loop to stop, and the case fails and goes on. The main thread, which waits blocked for the
threads to exit, is put on core 0 for the whole family and back where it was after: it runs at 44
there, and a thread spinning at a better priority on its core would starve it. No spinning thread
ever shares a core with the main thread or the watchdog, and every loop ends when the flag it
reads is set or the watchdog says stop, so a machine without one of the rules fails a case rather
than hang the run. The main thread waits for each thread up to a second past the watchdog; every
thread is closed at the end of its case, passed or failed. A thread that never exits keeps its
stack and its handle, the case says so, and the rest of the family is skipped: none is expected
to, since every thread that waits on a mutex waits for one whose owner runs once the loops stop.
No case is in the hang table for that reason.

The mutexes are libnx's `Mutex`, which marks its owner in the word it waits on and blocks in
`svcArbitrateLock`, so a thread waiting for one is a kernel waiter its owner can inherit from. The
priorities are the ones the hang showed: the waiter 44, the spinner 45, the owner 46; a thread
that preempts is 43, the end of a chain 50; the first case says if any of them is refused.

Each thread notes the steps it takes, with the system counter, in one trace (`SchedTrace` in
`source/schedref.h`, written with atomic stores); `SchedJudge` reads it once all of them exited.
The line gives the verdict, then every step in the order it happened and its time in ms:

```text
the flag 49.9 ms after the thread of 44 asked, while the thread of 45 spun: the owner ran at 44; held 0.0, spinning 0.2, asked 0.3, woke 50.0, got 50.1, flag set 50.1, flag seen 50.2, unlocked 50.3
no flag before the watchdog (1000 ms): the owner (46) never ran after its sleep while the thread of 44 waited for its mutex and the thread of 45 spun: it did not inherit 44; held 0.0, spinning 0.2, asked 0.3, watchdog 1000.0, stopped 1000.1, woke 1000.2, got 1000.3, flag set 1000.3, unlocked 1000.4
```

## The cases

| case | the threads | passes when | rule |
|---|---|---|---|
| threads start on the core and at the priority asked | 50 on core 0, 43 on core 1, 40 on core 2, each reading its core and priority | each reads its own; none tier | |
| the owner of a mutex runs at the priority of the thread waiting for it | the owner (46) locks and sleeps 50 ms; the spinner (45) reads a flag; the waiter (44) asks for the mutex and sets the flag once it has it | the spinner sees the flag before the watchdog | R1, R2 |
| the inherited priority passes down a chain of owners | the end (50) locks a second mutex and sleeps 50 ms; the owner (46) locks the first and waits for the second; the spinner (45); the waiter (44) asks for the first | the flag before the watchdog: the end of the chain ran at 44 | R1, R2 |
| a thread that becomes ready at a better priority takes the core from one spinning | the spinner (45); a thread of 43 sleeps 10 ms, then sets the flag | the flag before the watchdog; the line says how long after the sleep began | R2 |
| the owner goes back to its own priority when it hands the mutex over | as the first case | the mutex changes hands before the watchdog, the spinner sees the flag before the owner runs again, and the owner then reads 46 | R1; Q1 |
| a thread behind a spinning one moves to an idle core of its mask | the spinner (45); a thread of 46 with mask 0 to 2 and core 1 ideal reads its core and sets the flag | the flag before the watchdog, read on core 0 or 2 | R3 |
| svcSleepThread(0) leaves the thread on its core | a thread of 44, mask 0 to 2, spins until a thread of 50 (core 1 only) is ready behind it; then reads its core, svcSleepThread(0), its core, svcSleepThread(-1), its core; then alone, its core, svcSleepThread(-1), its core | its core is the same before and after svcSleepThread(0), every core read is 0 to 2, and the thread of 50 runs | Q2, Q3 |

Cases 1, 2 and 4 need R2 as well as R1: whatever happens on a core while a thread spins there
without a supervisor call, another core or a timer has to take the core from it. When the
preemption case fails, they cannot tell R1 apart, and their lines say which step never came.

## The open questions

- **Q1.** `svcGetThreadPriority` of an owner while a better thread waits for its mutex: A the
  priority it runs at (44), B its own (46). The owner reads it right after its sleep, while the
  waiter waits; the fourth case's line says which.
- **Q2.** `svcSleepThread(-1)` alone on a core, with idle cores in the mask: A the thread moves, B
  it stays (it only moves when its core has another thread ready). The last case's alone half says.
- **Q3.** A thread running on a core where another is ready, with an idle core in its mask:
  A it stays until it yields; B an idle core
  takes it at once, with no yield, which is what the scheduler reden ports does
  (`k_scheduler.cpp:284-308`, an idle core migrates the top thread of a core that has a next one).
  Under B, the thread of the last case is already on core 0 or 2 when it first reads its core, so
  `svcSleepThread(0)` has nothing to keep it from; the case passes under either answer as long as
  that call does not move it, and its line says which answer fit.

One note after the family adds up the answers of the cases that passed:

```json
{"type":"note","text":"sched: the passing cases answer Q1 A, Q2 B, Q3 B"}
```

## What Eden does

Read in reden at 75c12299a6, `src/core/hle/kernel/` unless said otherwise. Every case should pass;
read in the code, not run.

- R1: `svcArbitrateLock` waits in `KConditionVariable::WaitForAddress`, which makes the waiter a
  waiter of the owner (`k_condition_variable.cpp:184`); `KThread::AddWaiter` (`k_thread.cpp:1075`)
  inherits when the waiter is better, and `RestorePriority` (`k_thread.cpp:1023-1072`) takes the
  best waiter's priority and walks on to the owner of whatever lock the owner waits on
  (`thread = lock_owner`, 1071): the chain. Handing the mutex over removes the waiter
  (`RemoveWaiterByKey`, 1094) and restores the priority again.
- Q1: `svcGetThreadPriority` answers `GetPriority()`, the priority the thread runs at
  (`svc/svc_thread.cpp:177`): A.
- R2: a scheduler update that leaves another core needing a new thread interrupts it
  (`RescheduleCores`, `k_scheduler.cpp:886-892`), and `PhysicalCore::Interrupt` signals its CPU to
  stop (`physical_core.cpp:233-254`), loop or not.
- R3 and Q3: on every update, each idle core takes the first thread of its suggested queue that is
  not running, and failing that the running thread of a core that has another ready
  (`k_scheduler.cpp:249-311`): a thread behind the spinner moves (R3), and so does a running thread
  with a thread behind it (Q3 B).
- Q2: `svcSleepThread` with 0, -1 and -2 yields without migration, with migration and to any
  thread (`svc/svc_thread.cpp:133-138`); the yield with migration puts the thread at the back of
  its queue and pulls a suggested thread from another core onto its own
  (`k_scheduler.cpp:716-802`); it never moves the thread itself, so alone it stays: B.

## The judge, pinned

The `reference` suite holds `source/schedref.c` on traces made to order, eight cases in both
builds: what each case leaves on a machine with the rules and on one without each of them (the
owner never waking, the waiter never getting the core, a chain one level deep, the sleeper never
back, the thread of 46 waiting behind the spinner, `svcSleepThread(0)` moving the thread), Q1 to Q3
from the cores and priorities read, a case that could not set up, and the note. Mutations of the
order test, the owner's drop back, the yield check, the migrated core and the Q3 answer each fail
it.
