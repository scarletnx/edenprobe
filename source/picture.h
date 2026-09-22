// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A picture of what one case saw, kept next to the verdict the program already reached.
///
/// Diagnostic and never evidence: no picture decides a case, and none is a reference another
/// machine is measured against. The bytes written are the ones the judge looked at, collected
/// while it judged, so a picture cannot disagree with the line beside it.

/// What the four bytes of a pixel mean.
typedef enum {
    /// A colour, in the order the readback handed it over: red, green, blue, alpha.
    PICTURE_COLOUR,
    /// Four bytes of a 32 bit value, in the order the readback handed them over, which makes a
    /// picture of bit patterns and not of magnitudes. A float target has no honest mapping onto
    /// a byte range, and inventing one would make the picture a judgement rather than a record.
    PICTURE_BITS,
} PictureFormat;

/// What the judge decided about one pixel. The diff picture is this and nothing else, which is
/// what keeps it from becoming a second opinion: a suite that allows a pixel two answers, or a
/// tolerance in the last place, paints no difference here.
typedef enum {
    PICTURE_MATCHED,
    PICTURE_WRONG,
    /// Judged by nobody: the band an edge runs through, where the coverage rule allows either
    /// answer. Its expected pixel is the one the machine drew, so the expected picture shows
    /// what this run accepted.
    PICTURE_UNJUDGED,
} PictureVerdict;

/// The longest path a picture field carries. Long enough for the longest case name this suite
/// has, and stated here because the report line is sized off it.
#define PICTURE_PATH_MAX 160

/// The largest target a picture is collected for. Not a hardware limit: the scratch below is
/// static so that collecting one can never fail for want of memory, and a target past this
/// size gets no picture rather than a partial one.
#define PICTURE_MAX_DIMENSION 128u

/// One case's picture while the judge is still filling it in.
typedef struct {
    uint32_t width;
    uint32_t height;
    PictureFormat format;
    /// The readback itself, not a copy. Valid until the next readback, which is after the case
    /// has reported, so nothing here outlives the buffer it points at.
    const uint8_t *got;
    uint8_t *expected;
    uint8_t *verdict;
    bool open;
} Picture;

/// Where the files for one case ended up, relative to the directory the report is in. An empty
/// string means that file was not written.
typedef struct {
    char image[PICTURE_PATH_MAX];
    char expected[PICTURE_PATH_MAX];
    char diff[PICTURE_PATH_MAX];
    char raw[PICTURE_PATH_MAX];
} PicturePaths;

/// One case kept in memory for the detailed mode to show. The pixels are copies, because the
/// readback block is reused by the very next case.
typedef struct {
    char suite[32];
    char name[128];
    char tier[12];
    char detail[512];
    bool passed;
    uint32_t width;
    uint32_t height;
    PictureFormat format;
    uint8_t *got;
    uint8_t *expected;
    uint8_t *verdict;
} PictureRecord;

/// The directory the images go under, which is the one the report is written into.
void PictureSetRoot(const char *directory);

/// Where the pictures are going, and whether passing cases get one. Both are here so that a
/// case can borrow the settings for a moment and hand them back as it found them.
const char *PictureRoot(void);
bool PictureWritesEveryCase(void);
bool PictureIsRetaining(void);

/// Whether a passing case gets its picture written too. Off by default: a green run would
/// otherwise leave eighty files nobody asked for.
void PictureWriteEveryCase(bool on);

/// Whether finished pictures are kept in memory for the detailed mode. Off by default, so the
/// automatic run allocates nothing and costs what it always did.
void PictureRetain(bool on);

/// Opens the picture the judge is about to fill in, or NULL when the target is too large.
/// The matching finish is inside ReportCase, so every picture is closed by the line it belongs
/// to and none can leak onto the next case.
Picture *PictureBegin(uint32_t width, uint32_t height, PictureFormat format, const uint8_t *got);

/// Records what the judge decided about one pixel, and what it expected there. @p expected may
/// be NULL for a pixel nobody judged, which stores the pixel the machine drew.
static inline void PictureSet(Picture *picture, uint32_t x, uint32_t y,
                              const uint8_t expected[4], PictureVerdict verdict) {
    if (picture == NULL || x >= picture->width || y >= picture->height) {
        return;
    }
    const size_t index = (size_t)y * picture->width + x;
    picture->verdict[index] = (uint8_t)verdict;
    memcpy(picture->expected + index * 4u,
           expected != NULL ? expected : picture->got + index * 4u, 4u);
}

/// Writes and retains whatever picture is open, and hands back where it went. Called by the
/// report and by nothing else.
void PictureFinish(const char *suite, const char *name, const char *tier, bool passed,
                   const char *detail, PicturePaths *paths);

size_t PictureCount(void);
const PictureRecord *PictureAt(size_t index);

/// Turns a case name into the part of a file name it contributes: lower case letters and
/// digits kept, everything else a dash, no run of dashes and none at either end.
void PictureStem(char *out, size_t out_size, const char *name);

/// The three colours a diff is drawn in, by verdict. One table, so the picture on the memory
/// card and the one on a screen cannot come to mean different things.
void PictureDiffColour(uint8_t verdict, uint8_t out[3]);

#ifdef __cplusplus
}
#endif
