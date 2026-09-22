// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Where a line goes once a test has produced it.
///
/// Two channels, always both, because the two machines this runs on read different ones: an
/// emulator gets the debug string for free, and a console run leaves the file on the memory
/// card. The format is one JSON object per line rather than an array, so a run that dies halfway
/// still leaves a readable file. The last line of a finished run is the summary.
void ReportOpen(const char *path);
void ReportClose(void);

/// The tolerance a case allowed itself, and the claim about the hardware that goes with it.
///
/// It lives here rather than next to the comparisons in check.h because it has to travel with
/// the line: a tier that is only ever a comment cannot be read by whoever diffs two machines, and
/// a report that says a case passed without saying what it was willing to accept is not a report.
/// check.h includes this header, so everything that compares still sees it.
typedef enum {
    /// Not a tier. For a case that compares nothing the hardware produced: the selfcheck suite
    /// judges this program's own comparisons, so claiming a tolerance of the hardware there would
    /// be a claim about a machine that was never asked anything.
    CHECK_NONE,
    /// Bit for bit. Legal only where the result is defined exactly: integer render targets,
    /// integer arithmetic, bit patterns copied around. A float shader never qualifies, whatever
    /// the numbers look like on one machine.
    CHECK_EXACT,
    /// Within a stated number of units in the last place. For float work, where two correct
    /// implementations legitimately differ: the graphics API only requires division within
    /// 2.5 ULP, and the transcendentals are looser still.
    CHECK_ULP,
    /// The right answer, possibly a pixel or two away. For rasterisation coverage, where sub
    /// pixel precision is an implementation-reported limit rather than a constant.
    CHECK_POSITION,
} CheckTier;

/// One finished case. @p detail is free text explaining a failure, and is ignored when passing.
void ReportCase(const char *suite, const char *name, CheckTier tier, bool passed,
                const char *detail);

/// A line that is not a case: progress, a note, an environment fact worth recording.
void ReportNote(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/// The last line, carrying the counts a runner checks without parsing everything above it.
void ReportSummary(void);

/// How many cases have passed and failed so far.
size_t ReportPassed(void);
size_t ReportFailed(void);

#ifdef __cplusplus
}
#endif
