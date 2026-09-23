// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "picture.h"

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

/// Whether a report is still being written. The detailed mode asks before it puts anything on
/// a screen, so that presentation can never reach a line.
bool ReportIsOpen(void);

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
///
/// This is also where a case's picture is closed, written and named on the line, so a picture
/// belongs to exactly one case and cannot outlive it.
void ReportCase(const char *suite, const char *name, CheckTier tier, bool passed,
                const char *detail);

/// The longest line a case can produce: the fields below plus four paths to its pictures.
#define REPORT_LINE_MAX 2048

/// Builds the line a case reports, without emitting it.
///
/// Exposed so that the shape of a line, and the fields a picture adds to it, can be checked on
/// the host with no device and no file. @p paths may be NULL. Returns the length written.
///
/// What it writes always parses. A buffer too small for the whole line gets an empty object
/// rather than the front of a real one, because a truncated line ends inside a string and takes
/// the rest of the file down with it.
size_t ReportBuildCaseLine(char *out, size_t out_size, const char *suite, const char *name,
                           const char *tier, bool passed, const char *detail,
                           const PicturePaths *paths);


/// The longest note ReportNote carries whole. Stated here rather than buried in the buffer, so a
/// caller with a long note can be checked against it before anything is emitted: a note cut
/// short leaves the reader half a sentence and no sign that there was more.
#define REPORT_NOTE_MAX 767

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
