// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// How long a case kept the graphics unit busy, and nothing a verdict reads.
///
/// What is timed is every submit and the wait for the queue to drain after it, which is where the
/// draw, the clears, the uploads and the readback copy all happen. What is not timed is recording
/// the commands, the judge comparing pixels, and writing the report or a picture: a case's number
/// is the machine's work, not this program's. No libnx here, so the arithmetic runs on the host.

/// Passes a timed suite makes. The first is a warm-up whose numbers are dropped; the rehearsals
/// and the verdict pass are the samples, nine of them, and a case's number is the smallest. The
/// smallest because everything else on the machine only ever adds time to a sample: on a shared
/// machine the minimum of nine agreed between runs far more often than the median did, and nine
/// was where adding samples stopped helping. The measurement is in the README's "Time per case".
#define PERF_WARMUPS 1u
#define PERF_REHEARSALS 8u
#define PERF_SAMPLES (PERF_REHEARSALS + 1u)

/// The most cases one suite can time. A case past it reports no time rather than someone else's.
#define PERF_MAX_CASES 256u

typedef enum {
    /// Outside a timed suite. Nothing is kept and no line gets a time.
    PERF_UNTIMED,
    PERF_WARMUP,
    PERF_REHEARSAL,
    PERF_VERDICT,
} PerfPass;

/// Adds @p nanoseconds of graphics work to the case under way. Called by gpu.c alone.
void PerfAddWork(uint64_t nanoseconds);

/// Forgets every sample, for the start of a timed suite.
void PerfSuiteBegin(void);

/// Starts a pass over the suite: the case count goes back to the first case and any work not yet
/// claimed by a case is dropped, so none of it lands on the next pass's first case.
void PerfPassBegin(PerfPass pass);

/// Closes one case and claims the work done since the previous one.
///
/// In a rehearsal the time goes into the case's samples. In the verdict pass it is added too,
/// and the smallest sample the case has is handed back in @p microseconds. False when
/// there is nothing to say: no work since the last case, which is a case that drew nothing, or
/// no timed pass under way. A case whose name differs from the one at the same position in an
/// earlier pass starts its samples again, so a suite that took another path in one pass never
/// hands a case another case's time.
bool PerfCaseEnd(const char *name, uint32_t *microseconds);

/// Which pass is under way, counted from one with the warm-up first, and how many cases of it
/// have closed. For the line that says where a hang happened.
uint32_t PerfPassNumber(void);
uint32_t PerfCaseIndex(void);

/// The name the case at @p index had in an earlier pass of this suite, or NULL when no earlier
/// pass reached it, which is every case of the warm-up.
const char *PerfNameAt(uint32_t index);

/// What a perf line says about one suite.
typedef struct {
    uint32_t timed;  ///< cases that reported a time
    uint64_t total;  ///< their times added up, microseconds
    uint32_t min;
    uint32_t max;
    uint32_t median; ///< of the cases' times; between the two middle ones, rounded down, when even
    /// Submits the timed cases made in the verdict pass. Each costs a round trip whatever it
    /// carries, so this is what a reader multiplies by the machine's per-submit cost to find the
    /// floor under the total.
    uint32_t submits;
} PerfSummary;

/// The summary of @p count times, which it sorts in place. All zeros for none; `submits` is left
/// at zero, since only the suite's own record knows it.
void PerfSummarize(uint32_t *times, size_t count, PerfSummary *out);

/// The summary of the verdict pass's reported times since PerfSuiteBegin.
void PerfSuiteSummary(PerfSummary *out);

/// The median of @p count values, sorted in place: the middle one, or between the two middle ones
/// rounded down. Zero for none.
uint32_t PerfMedian(uint32_t *values, size_t count);

/// The smallest of @p count samples. Zero for none.
uint32_t PerfMinimum(const uint32_t *samples, size_t count);

#ifdef __cplusplus
}
#endif
