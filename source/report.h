// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/// The tolerance the comparison on one reported line was held to, and the claim about the
/// hardware that goes with it.
///
/// A property of the line, not an ambition of the case that wrote it: a case stopped before it
/// compared anything reports CHECK_NONE. It lives here rather than beside the comparisons in
/// check.h because it has to travel with the line, where whoever diffs two machines reads it.
typedef enum {
    /// Not a tier. For a line that reports no comparison of anything the hardware produced: a
    /// selfcheck case, a bring-up case, or a drawing case stopped before it saw a pixel.
    /// Claiming a tolerance on one of those would be a claim about a machine nobody asked.
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
