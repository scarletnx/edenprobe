// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vp9jobs.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The nvdec suite's judge and the bytes of its jobs: where everything sits in the one buffer
/// the engine is given, the command words that hand a VP9 job over, the setup and table each job
/// carries under each form of the open questions, the reading of a decoded surface, and what a
/// case's reads must be. No service here, so both builds run it and the reference suite pins it.
///
/// The buffers and their sizes are the ones the homebrew FFmpeg backend (nvtegra_vp9.c) sets up
/// for the engine; the surfaces are NV12, block linear with blocks two gobs tall, as that
/// backend allocates and reads them.

#define NVDEC_CLASS 0xF0u
/// What every surface byte holds before a pass, so an image still all of it was never written.
#define VP9_FILL 0xEEu

// The one buffer: every offset a multiple of 256, since the engine is given addresses shifted
// right by 8.
#define VP9_AT_SETUP 0x0000u
#define VP9_AT_STATUS 0x0100u
#define VP9_AT_COMMANDS 0x0200u
#define VP9_COMMAND_WORDS 256u
#define VP9_AT_PROBS 0x0600u
#define VP9_AT_TILES 0x1500u
/// The tile size buffer, and the two halfwords nvtegra sets in it once (0x37A is 9, 0x37B 1).
#define VP9_TILES_SIZE 0x700u
#define VP9_AT_COUNTERS 0x1C00u
/// nvdec_vp9EntropyCounts_t, 13264 bytes, rounded up to 256.
#define VP9_COUNTERS_SIZE 0x3400u
#define VP9_SUPERBLOCKS (((VP9_WIDTH_MAX + 63u) / 64u) * ((VP9_HEIGHT_MAX + 63u) / 64u))
#define VP9_SEGMENT_SIZE ((VP9_SUPERBLOCKS * 32u + 0xFFu) & ~0xFFu)
#define VP9_AT_SEGMENT0 (VP9_AT_COUNTERS + VP9_COUNTERS_SIZE)
#define VP9_AT_SEGMENT1 (VP9_AT_SEGMENT0 + VP9_SEGMENT_SIZE)
#define VP9_AT_BITSTREAM (VP9_AT_SEGMENT1 + VP9_SEGMENT_SIZE)
/// The largest body and 16 bytes of nought after it.
#define VP9_BITSTREAM_SIZE ((VP9_BODY_MAX + 16u + 0xFFu) & ~0xFFu)
#define VP9_COLMV_SIZE (VP9_SUPERBLOCKS * 1024u)
#define VP9_AT_COLMV0 (VP9_AT_BITSTREAM + VP9_BITSTREAM_SIZE)
#define VP9_AT_COLMV1 (VP9_AT_COLMV0 + VP9_COLMV_SIZE)
#define VP9_FILTER_SIZE (((VP9_HEIGHT_MAX + 63u) & ~63u) * 988u)
#define VP9_AT_FILTER (VP9_AT_COLMV1 + VP9_COLMV_SIZE)
#define VP9_AT_SURFACES ((VP9_AT_FILTER + VP9_FILTER_SIZE + 0xFFFu) & ~0xFFFu)
#define VP9_STRIDE_MAX ((VP9_WIDTH_MAX + 63u) & ~63u)
#define VP9_ROWS_MAX ((VP9_HEIGHT_MAX + 31u) & ~31u)
#define VP9_SURFACE_MAX (VP9_STRIDE_MAX * VP9_ROWS_MAX * 3u / 2u)
/// Bytes after the last surface that no job is told about, which must still hold the fill.
#define VP9_AT_GUARD (VP9_AT_SURFACES + VP9_SURFACES_MAX * VP9_SURFACE_MAX)
#define VP9_GUARD_SIZE 0x1000u
#define VP9_MAP_SIZE ((VP9_AT_GUARD + VP9_GUARD_SIZE + 0xFFFFu) & ~0xFFFFu)

/// A case's surface: rows of a multiple of 64 bytes, the height a multiple of 32, the
/// interleaved chroma plane right after the luma one.
typedef struct {
    uint32_t stride;
    uint32_t rows;
    uint32_t chroma;
    uint32_t size;
} Vp9Layout;

Vp9Layout Vp9LayoutOf(uint32_t width, uint32_t height);

/// The open questions a case can answer, numbered as they were asked. Q1 and Q2 every case
/// answers by reading; Q3 and Q4 need a second pass, with the job built the other way.
typedef enum {
    VP9_Q1, ///< when the image is on the surface: A at its own job's fence, B a job later
    VP9_Q2, ///< a hidden frame's job: A writes its surface, B does not
    VP9_Q3, ///< LastShowFrame after a frame shown again: A the last decoded frame's, B set
    VP9_Q4, ///< setup bytes 0x74 and 0x75: A chroma AC then DC delta, B DC then AC
    VP9_QUESTIONS
} Vp9Question;

const char *Vp9QuestionName(Vp9Question question);

/// The question whose two forms differ for @p c, Q3 or Q4, or VP9_QUESTIONS when one pass says
/// everything: Q3 when a job after a frame shown again would have its LastShowFrame bit set
/// only by the other form, Q4 when a setup's two chroma deltas differ.
Vp9Question Vp9CaseQuestion(const Vp9Case *c);

/// The setup frame @p f's job hands over: the generated one, with LastShowFrame set when
/// @p after_existing and the pass is Q3's B, or the chroma deltas swapped when it is Q4's B.
void Vp9Setup(const Vp9Frame *f, bool after_existing, Vp9Question question, bool b,
              uint8_t out[VP9_SETUP_SIZE]);

/// Brings @p table to frame @p f's: the whole table for a case's first job, the patch after.
void Vp9TableApply(const Vp9Frame *f, uint8_t table[VP9_PROBS_SIZE]);

/// Where one job's buffers are for the engine.
typedef struct {
    uint32_t iova;          ///< the buffer's address for the engine
    uint32_t surface_size;  ///< the case's surface size
    uint32_t chroma;        ///< where a surface's chroma plane starts
    uint32_t index;         ///< the job's number in the pass (SET_PICTURE_INDEX)
    uint32_t segment_read;  ///< which segment buffer the job reads, 0 or 1; it writes the other
    uint8_t surfaces[4];    ///< last, golden, altref, current
} Vp9JobAt;

/// The command words of one job, the way nvtegra_vp9_prepare_cmdbuf orders them: the
/// application and codec, the picture index, the setup, the bitstream and the status, the VP9
/// buffers, the four surfaces, Execute, and an increment of @p syncpt when the job is done.
/// Returns how many words, or nought when they do not fit in @p max.
size_t Vp9Commands(uint32_t *words, size_t max, const Vp9JobAt *at, uint32_t syncpt);

/// One image as read back: the MD5 of the whole picture and of each plane (I420, cropped, as
/// libvpx's lists are made), whether every byte read still held VP9_FILL, and whether it was
/// read at all.
typedef struct {
    uint8_t md5[4][16];
    bool blank;
    bool read;
} Vp9Image;

/// Reads a decoded image off a copy of a surface laid out as @p layout: luma and interleaved
/// chroma, block linear in blocks two gobs tall, cropped to @p width by @p height.
void Vp9ReadImage(const uint8_t *surface, const Vp9Layout *layout, uint32_t width,
                  uint32_t height, Vp9Image *out);

/// What one pass of a case left. Per frame, the image when its turn came (right after its job,
/// or when a frame shown again came up) and again after the next job; per job, what the
/// engine's status said. A pass that stopped at a job says why and how far it got.
typedef struct {
    Vp9Question question; ///< the question this pass answers B to, VP9_QUESTIONS for none
    bool b;
    uint32_t frames_run;
    char failure[200];
    Vp9Image now[VP9_FRAMES_MAX];
    Vp9Image later[VP9_FRAMES_MAX];
    uint32_t error_status[VP9_FRAMES_MAX];
    uint32_t mbs_in_error[VP9_FRAMES_MAX];
} Vp9Pass;

typedef struct {
    uint32_t images;       ///< the list's length
    uint32_t right_now;    ///< images right when their turn came
    uint32_t later_read;   ///< images read again a job later
    uint32_t right_later;  ///< and right then
    int32_t first;         ///< the first image wrong when its turn came, or -1
    uint32_t first_frame;  ///< its frame
    uint32_t hidden;       ///< hidden frames whose job ran
    uint32_t hidden_right; ///< and whose surface held FFmpeg's image at the fence
    bool q1a;              ///< every image right at its turn
    bool q1b;              ///< not, but every image read a job later right then
    bool passed;           ///< one of the two, and the pass did not stop
} Vp9Verdict;

void Vp9Judge(const Vp9Case *c, const Vp9Pass *pass, Vp9Verdict *v);

/// The case's line from its passes (one, or the A and the B of its question): which answers fit
/// when it passed, how many images were right and the first one wrong when it did not. Returns
/// whether it passed, and in @p fits a bit per question and answer (bit 2q for A, 2q+1 for B).
bool Vp9Detail(const Vp9Case *c, const Vp9Pass *passes, uint32_t pass_count, char *out,
               size_t size, uint32_t *fits);

/// Adds a passing case's answers into @p settled (bit 0 A, bit 1 B per question); a question
/// the case could not tell apart settles nothing.
void Vp9Settle(uint32_t fits, uint8_t settled[VP9_QUESTIONS]);
/// The note's text: each question's answer, or that two cases answered it apart. Returns the
/// length.
size_t Vp9Settled(const uint8_t settled[VP9_QUESTIONS], char *out, size_t size);

/// FNV-1a 32 of @p size bytes, from @p hash.
uint32_t Vp9Fnv(uint32_t hash, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
