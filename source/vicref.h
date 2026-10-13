// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The video family's judge: the VIC job each case submits, packed into the configuration
/// structure the engine reads, the command words that hand it over, and what the output must
/// be under each answer to the questions only a console settles. No service here, so both
/// builds run it and the reference suite pins it to tools/vic-expect.py.
///
/// The structure's bit positions are the ones libdrm's tests/tegra/vic40.h gives for VIC 4.0,
/// which reden's src/video_core/host1x/vic.h agrees with. The surfaces are 16 by 16, pitch
/// linear, with a stride of 256 pixels; the input is NV12-shaped, a luma plane and a plane of
/// interleaved chroma pairs at half the height.

#define VIC_CONFIG_SIZE 0x610u
#define VIC_SIZE 16u
#define VIC_LUMA_STRIDE 256u
#define VIC_CHROMA_ROWS (VIC_SIZE / 2u)
#define VIC_RGBA_PITCH 1024u
/// What every output byte holds before a job, so a byte the engine wrote is one that changed:
/// no case expects 0xEE anywhere it writes.
#define VIC_FILL 0xEEu
#define VIC_SLOTS_MAX 2u
#define VIC_JOBS_MAX 6u
#define VIC_CASE_QUESTIONS_MAX 3u
/// The largest output a job leaves: an RGBA surface's 16 rows at the full pitch.
#define VIC_OUTPUT_MAX (VIC_RGBA_PITCH * VIC_SIZE)
#define VIC_CLASS 0x5du

enum {
    VIC_FORMAT_A8B8G8R8 = 31,
    VIC_FORMAT_A8R8G8B8 = 32,
    VIC_FORMAT_X8B8G8R8 = 35,
    VIC_FORMAT_Y8_U8V8_N420 = 67,
    VIC_FORMAT_Y8_V8U8_N420 = 68,
};

/// The open questions, numbered as the handback numbers them. Q1 has one answer in every case
/// (no matrix copies Y, U and V into R, G and B), so it is a rule and not a choice; Q11 is new:
/// which byte of a chroma pair format 68 reads U from.
typedef enum {
    VIC_Q2,  ///< X8B8G8R8's fourth byte: A the alpha, B 255
    VIC_Q3,  ///< the matrix shift: A before the constant column, B after
    VIC_Q4,  ///< the soft clamp: A takes the alpha too, B does not
    VIC_Q5,  ///< alpha fill Opaque: A 1023, B the composited alpha
    VIC_Q6,  ///< two slots over each other: A the higher slot on top, B slot 0
    VIC_Q7,  ///< outside the target rect: A untouched, B the background colour
    VIC_Q8,  ///< format 67: A the other chroma order from 68, B the same as 68
    VIC_Q9,  ///< the matrix: A truncates, B adds 128 before the last shift
    VIC_Q10, ///< without the matrix: A the soft clamp holds, B it does not
    VIC_Q11, ///< format 68: A U in the even byte, B V in the even byte
    VIC_QUESTIONS,
} VicQuestion;

/// One answer per question, bit q set for B.
typedef uint32_t VicRules;

typedef struct {
    uint16_t left, top, right, bottom; ///< inclusive, whole pixels
} VicRect;

/// A slot's input: the standard image (Y = 16 + 14 x, U 90, V 240) or one colour throughout.
typedef struct {
    bool standard;
    uint8_t y, u, v;
} VicImage;

typedef struct {
    bool enable;
    uint16_t clamp_low, clamp_high;
    uint16_t planar_alpha;
    VicRect source; ///< sent in 16.16, as libdrm sends it
    VicRect dest;
    uint8_t format;
    bool matrix;
    uint8_t matrix_shift;
    int32_t m[3][4]; ///< rows R, G, B; columns Y, U, V and the constant; S12.8
    VicImage image;
} VicSlot;

typedef struct {
    uint8_t alpha_fill_mode; ///< 0 Opaque, 3 SourceStream
    uint8_t alpha_fill_slot;
    uint16_t background[4]; ///< R, G, B, A, ten bits each
    VicRect target;
    uint8_t out_format;
    uint16_t out_width; ///< the surface's, in pixels
    uint16_t out_chroma_width, out_chroma_height; ///< as written, minus one; 16383 for RGBA
    uint32_t slot_count;
    VicSlot slots[VIC_SLOTS_MAX];
} VicJob;

typedef struct {
    const char *name;  ///< the handback's name for it
    const char *title; ///< what it checks, for the line
    uint32_t job_count;
    VicJob jobs[VIC_JOBS_MAX];
    uint32_t question_count;
    VicQuestion questions[VIC_CASE_QUESTIONS_MAX];
} VicCase;

/// The nineteen cases, in the order they run.
size_t VicCaseCount(void);
const VicCase *VicCaseAt(size_t index);

/// The rules of combination @p index of a case's questions: the first question's answer
/// changes fastest, every question the case is not open on answers A.
VicRules VicCaseRules(const VicCase *c, uint32_t index);
uint32_t VicCaseCombinations(const VicCase *c);
const char *VicQuestionName(VicQuestion question);

/// The configuration structure for @p job, every field the job does not set zero.
void VicPack(const VicJob *job, uint8_t config[VIC_CONFIG_SIZE]);

/// Writes a slot's planes: luma VIC_LUMA_STRIDE by VIC_SIZE, chroma VIC_LUMA_STRIDE by
/// VIC_CHROMA_ROWS, U in each pair's even byte.
void VicImageWrite(const VicImage *image, uint8_t *luma, uint8_t *chroma);

/// The bytes a job's output is judged over: the RGBA surface's rows at the full pitch, or an
/// NV12 surface's luma rows followed by its chroma rows.
size_t VicOutputSize(const VicJob *job);
bool VicOutputIsNv12(const VicJob *job);

/// What the output must hold after @p job under @p rules, filled from VIC_FILL first.
void VicModel(const VicJob *job, VicRules rules, uint8_t *out);

/// One channel of the colour matrix in ten bits, before the clamp: the three products summed,
/// then the shift and the constant column in the order @p after says, then the last shift by 8,
/// truncating or adding 128 first. Shifts floor, as an arithmetic shift does.
int32_t VicMatrixChannel(const int32_t m[3][4], uint32_t row, int32_t y10, int32_t u10,
                         int32_t v10, uint32_t shift, bool after, bool rounding);

/// Where the engine finds each buffer: I/O virtual addresses, sent shifted right by 8.
typedef struct {
    uint32_t config;
    uint32_t out_luma;
    uint32_t out_chroma;
    uint32_t luma[VIC_SLOTS_MAX];
    uint32_t chroma[VIC_SLOTS_MAX];
} VicAddresses;

/// The command words of one job: the class, the methods through the host interface's method
/// registers, Execute, and the syncpoint increment when the operation is done. Hands back the
/// count, or 0 when @p max is too small.
size_t VicCommands(uint32_t *words, size_t max, const VicAddresses *at, uint32_t slots,
                   bool nv12_out, uint32_t syncpt);

/// What the judge made of a case's outputs.
typedef struct {
    bool passed;
    /// Bit k: combination k of the case's answers explains every byte of every job.
    uint32_t fits;
    /// The combination the expected buffers hold now: the first that fits, or the one with the
    /// fewest wrong bytes, and how many, and the first of them.
    uint32_t shown;
    uint32_t wrong;
    uint32_t job;
    size_t at;
    /// Every byte of every job still VIC_FILL: the engine wrote nothing.
    bool untouched;
} VicVerdict;

/// Judges @p got, one VIC_OUTPUT_MAX buffer per job, against the model under every
/// combination of the case's answers, over each whole buffer. @p expected is scratch of the
/// same shape, left holding the model under the combination the verdict shows.
void VicJudge(const VicCase *c, const uint8_t (*got)[VIC_OUTPUT_MAX],
              uint8_t (*expected)[VIC_OUTPUT_MAX], VicVerdict *verdict);

/// The line's detail: on a pass the answers that fit ("fits Q9 either, Q11 A", or nothing for
/// a case with no open question), on a failure the wrong bytes under the closest answers and
/// the first of them, and whether the engine wrote anything at all.
size_t VicDetail(const VicCase *c, const VicVerdict *verdict, const uint8_t (*got)[VIC_OUTPUT_MAX],
                 const uint8_t (*expected)[VIC_OUTPUT_MAX], char *out, size_t out_size);

/// Adds what a passing case settled to @p settled, bit 0 for an A and bit 1 for a B per
/// question, and says it all as text: "Q8 A, Q11 A", a question two cases answered apart
/// marked so. Nothing for a run that settled nothing.
void VicSettle(const VicCase *c, uint32_t fits, uint8_t settled[VIC_QUESTIONS]);
size_t VicSettled(const uint8_t settled[VIC_QUESTIONS], char *out, size_t out_size);

/// FNV-1a over @p size bytes, continuing from @p hash (2166136261 to start).
uint32_t VicHash(uint32_t hash, const uint8_t *data, size_t size);
#define VIC_HASH_START 2166136261u

#ifdef __cplusplus
}
#endif
