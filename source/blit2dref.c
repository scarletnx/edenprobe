// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "blit2dref.h"
#include "coherref.h"
#include "md5.h"
#include "rawref.h"

#define ONE (1ull << 32)

static const Blit2dCase CASES[BLIT2D_CASES] = {
    { "blit2d_corner_half", "half size from the corner origin, into a window cut mid-run",
      BLIT2D_ORIGIN_CORNER, 2u * ONE },
    { "blit2d_center_half", "half size from the centre origin, into the same window", 0u,
      2u * ONE },
    { "blit2d_corner_2_3", "two thirds size from the corner origin, 1.5 texels a pixel",
      BLIT2D_ORIGIN_CORNER, ONE + ONE / 2u },
    { "blit2d_center_2_3", "two thirds size from the centre origin, 1.5 texels a pixel", 0u,
      ONE + ONE / 2u },
};

const Blit2dCase *Blit2dCaseAt(Blit2dCaseId id) {
    return (unsigned)id < BLIT2D_CASES ? &CASES[id] : NULL;
}

uint32_t Blit2dSourceTexel(uint32_t x, uint32_t y) {
    return 0x10000000u | y << 12 | x;
}

static void Put(uint8_t *at, uint32_t word) {
    for (uint32_t i = 0; i < 4u; ++i) {
        at[i] = (uint8_t)(word >> (8u * i));
    }
}

uint32_t Blit2dDstOffset(uint32_t x, uint32_t y) {
    return CohBlockLinearOffset(x * 4u, y, BLIT2D_DST_WIDTH * 4u, BLIT2D_BLOCK_GOBS);
}

void Blit2dFillSource(uint8_t *bytes) {
    for (uint32_t y = 0; y < BLIT2D_SRC_HEIGHT; ++y) {
        for (uint32_t x = 0; x < BLIT2D_SRC_WIDTH; ++x) {
            Put(bytes + CohBlockLinearOffset(x * 4u, y, BLIT2D_SRC_WIDTH * 4u, BLIT2D_BLOCK_GOBS),
                Blit2dSourceTexel(x, y));
        }
    }
}

int32_t Blit2dTexel(const Blit2dCase *c, uint32_t answers, uint32_t n, uint32_t extent) {
    const bool corner = (c->origin == BLIT2D_ORIGIN_CORNER) != ((answers & BLIT2D_Q1_B) != 0u);
    if ((answers & BLIT2D_Q2_B) == 0u) {
        // The engine's own walk: start, plus half a step for the centre, plus n steps.
        const uint64_t at = (corner ? 0u : c->du / 2u) + (uint64_t)n * c->du;
        return (int32_t)(at >> 32);
    }
    // Eden's Blit (fermi_2d.cpp:79-96) as it reads: the shifts on a signed 32.32 start, then
    // its blit shader's nearest sample at each pixel's centre across the whole texel span.
    const int64_t start = corner ? -(int64_t)((c->du >> 33) << 32) : 0;
    const int64_t first = start >> 32;
    const int64_t last = (start + (int64_t)c->du * (int64_t)extent) >> 32;
    return (int32_t)(first + (int64_t)(2u * n + 1u) * (last - first) / (2 * (int64_t)extent));
}

static bool InWindow(uint32_t x, uint32_t y) {
    return x >= BLIT2D_WIN_X && x < BLIT2D_WIN_X + BLIT2D_WIN_WIDTH && y >= BLIT2D_WIN_Y &&
           y < BLIT2D_WIN_Y + BLIT2D_WIN_HEIGHT;
}

uint32_t Blit2dWant(const Blit2dCase *c, uint32_t answers, uint32_t x, uint32_t y) {
    if (!InWindow(x, y)) {
        return BLIT2D_FILL_WORD;
    }
    const int32_t tx = Blit2dTexel(c, answers, x - BLIT2D_WIN_X, BLIT2D_WIN_WIDTH);
    const int32_t ty = Blit2dTexel(c, answers, y - BLIT2D_WIN_Y, BLIT2D_WIN_HEIGHT);
    return Blit2dSourceTexel((uint32_t)tx, (uint32_t)ty);
}

void Blit2dExpected(const Blit2dCase *c, uint32_t answers, uint8_t *out) {
    for (uint32_t y = 0; y < BLIT2D_DST_HEIGHT; ++y) {
        for (uint32_t x = 0; x < BLIT2D_DST_WIDTH; ++x) {
            Put(out + Blit2dDstOffset(x, y), Blit2dWant(c, answers, x, y));
        }
    }
}

void Blit2dMd5Hex(const void *bytes, size_t size, char out[33]) {
    MD5_CTX ctx;
    unsigned char digest[16];
    MD5_Init(&ctx);
    MD5_Update(&ctx, bytes, (unsigned long)size);
    MD5_Final(digest, &ctx);
    for (uint32_t i = 0; i < 16u; ++i) {
        snprintf(out + 2u * i, 3, "%02x", digest[i]);
    }
}

size_t Blit2dRawWords(const Blit2dCase *c, uint64_t src_iova, uint64_t dst_iova, uint32_t *out) {
    size_t n = 0;
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_OPERATION, BLIT2D_OPERATION_SRCCOPY);
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_CLIP_ENABLE, 0u);
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_SRC_FORMAT, 4u);
    out[n++] = BLIT2D_FORMAT_A8B8G8R8;
    out[n++] = 0u; // block linear
    out[n++] = BLIT2D_BLOCK_SIZE_TWO_GOBS;
    out[n++] = 1u; // depth
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_SRC_WIDTH, 4u);
    out[n++] = BLIT2D_SRC_WIDTH;
    out[n++] = BLIT2D_SRC_HEIGHT;
    out[n++] = (uint32_t)(src_iova >> 32);
    out[n++] = (uint32_t)src_iova;
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_DST_FORMAT, 5u);
    out[n++] = BLIT2D_FORMAT_A8B8G8R8;
    out[n++] = 0u;
    out[n++] = BLIT2D_BLOCK_SIZE_TWO_GOBS;
    out[n++] = 1u;
    out[n++] = 0u; // layer
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_DST_WIDTH, 4u);
    out[n++] = BLIT2D_DST_WIDTH;
    out[n++] = BLIT2D_DST_HEIGHT;
    out[n++] = (uint32_t)(dst_iova >> 32);
    out[n++] = (uint32_t)dst_iova;
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_COMPRESSION, 1u);
    out[n++] = RawInlineHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_SAMPLE_MODE, c->origin);
    out[n++] = RawIncreasingHeader(BLIT2D_SUBCHANNEL, BLIT2D_SET_DST_X0, 12u);
    out[n++] = BLIT2D_WIN_X;
    out[n++] = BLIT2D_WIN_Y;
    out[n++] = BLIT2D_WIN_WIDTH;
    out[n++] = BLIT2D_WIN_HEIGHT;
    for (uint32_t axis = 0; axis < 2u; ++axis) {
        out[n++] = (uint32_t)c->du;
        out[n++] = (uint32_t)(c->du >> 32);
    }
    for (uint32_t i = 0; i < 4u; ++i) {
        out[n++] = 0u; // the source origin, 0.0 on both axes; the last word launches
    }
    return n;
}

static uint32_t Word(const uint8_t *at) {
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

uint32_t Blit2dRawPixel(const uint8_t *raw, uint32_t x, uint32_t y) {
    return Word(raw + Blit2dDstOffset(x, y));
}

uint32_t Blit2dBufferPixel(const uint8_t *buffer, uint32_t x, uint32_t y) {
    return Word(buffer + ((size_t)y * BLIT2D_DST_WIDTH + x) * 4u);
}

typedef uint32_t (*PixelOf)(const uint8_t *read, uint32_t x, uint32_t y);

static uint32_t Fits(const Blit2dCase *c, const uint8_t *read, PixelOf pixel) {
    uint32_t fits = 0;
    for (uint32_t k = 0; k < BLIT2D_COMBINATIONS; ++k) {
        bool all = true;
        for (uint32_t y = 0; all && y < BLIT2D_DST_HEIGHT; ++y) {
            for (uint32_t x = 0; all && x < BLIT2D_DST_WIDTH; ++x) {
                all = pixel(read, x, y) == Blit2dWant(c, k, x, y);
            }
        }
        fits |= all ? 1u << k : 0u;
    }
    return fits;
}

static void Count(const Blit2dCase *c, uint32_t shown, const uint8_t *read, PixelOf pixel,
                  Blit2dRead *r) {
    r->untouched = true;
    for (uint32_t y = 0; y < BLIT2D_DST_HEIGHT; ++y) {
        for (uint32_t x = 0; x < BLIT2D_DST_WIDTH; ++x) {
            const uint32_t got = pixel(read, x, y);
            const uint32_t want = Blit2dWant(c, shown, x, y);
            r->untouched = r->untouched && got == BLIT2D_FILL_WORD;
            if (!InWindow(x, y)) {
                r->outside += got != BLIT2D_FILL_WORD ? 1u : 0u;
            } else if (got == want) {
                ++r->right;
            } else if (!r->wrong) {
                r->wrong = true;
                r->wrong_x = x;
                r->wrong_y = y;
                r->wrong_got = got;
                r->wrong_want = want;
            }
        }
    }
}

static uint32_t First(uint32_t bits) {
    uint32_t k = 0;
    while (k < BLIT2D_COMBINATIONS - 1u && (bits >> k & 1u) == 0u) {
        ++k;
    }
    return k;
}

void Blit2dJudge(const Blit2dCase *c, const uint8_t *raw, const uint8_t *buffer,
                 Blit2dVerdict *out) {
    memset(out, 0, sizeof(*out));
    out->raw.fits = Fits(c, raw, Blit2dRawPixel);
    out->buffer.fits = Fits(c, buffer, Blit2dBufferPixel);
    out->fits = out->raw.fits & out->buffer.fits;
    out->shown = out->fits != 0u          ? First(out->fits)
                 : out->buffer.fits != 0u ? First(out->buffer.fits)
                 : out->raw.fits != 0u    ? First(out->raw.fits)
                                          : 0u;
    Count(c, out->shown, raw, Blit2dRawPixel, &out->raw);
    Count(c, out->shown, buffer, Blit2dBufferPixel, &out->buffer);
    out->passed = out->fits != 0u;
}

/// A for a question every fitting combination answers A, B for B, "either" when both fit.
static const char *AnswerOf(uint32_t fits, uint32_t bit) {
    bool a = false, b = false;
    for (uint32_t k = 0; k < BLIT2D_COMBINATIONS; ++k) {
        if ((fits >> k & 1u) != 0u) {
            a = a || (k & bit) == 0u;
            b = b || (k & bit) != 0u;
        }
    }
    return a && b ? "either" : b ? "B" : "A";
}

/// Q2 only changes the cases whose du has a fraction: at a whole number both readings agree.
static bool Q2Open(const Blit2dCase *c) {
    return (uint32_t)c->du != 0u;
}

static void Answers(const Blit2dCase *c, uint32_t fits, char *out, size_t size) {
    if (Q2Open(c)) {
        snprintf(out, size, "Q1 %s, Q2 %s", AnswerOf(fits, BLIT2D_Q1_B),
                 AnswerOf(fits, BLIT2D_Q2_B));
    } else {
        snprintf(out, size, "Q1 %s", AnswerOf(fits, BLIT2D_Q1_B));
    }
}

static void Describe(uint32_t v, char *out, size_t size) {
    const uint32_t x = v & 0xFFFu, y = v >> 12 & 0xFFFFu;
    if (v == BLIT2D_FILL_WORD) {
        snprintf(out, size, "the fill");
    } else if (x < BLIT2D_SRC_WIDTH && y < BLIT2D_SRC_HEIGHT && v == Blit2dSourceTexel(x, y)) {
        snprintf(out, size, "texel (%u,%u)", (unsigned)x, (unsigned)y);
    } else {
        snprintf(out, size, "no texel");
    }
}

/// One coordinate of the texel @p v is, or - for the fill and ? for anything else.
static void Axis(uint32_t v, bool want_y, char *out, size_t size) {
    const uint32_t x = v & 0xFFFu, y = v >> 12 & 0xFFFFu;
    if (v == BLIT2D_FILL_WORD) {
        snprintf(out, size, " -");
    } else if (x < BLIT2D_SRC_WIDTH && y < BLIT2D_SRC_HEIGHT && v == Blit2dSourceTexel(x, y)) {
        snprintf(out, size, " %u", (unsigned)(want_y ? y : x));
    } else {
        snprintf(out, size, " ?");
    }
}

static size_t Append(char *out, size_t size, size_t used, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

static size_t Append(char *out, size_t size, size_t used, const char *format, ...) {
    if (used >= size) {
        return used;
    }
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(out + used, size - used, format, args);
    va_end(args);
    return n < 0 ? used : used + (size_t)n;
}

static size_t ReadText(const Blit2dCase *c, const Blit2dRead *r, const uint8_t *read,
                       PixelOf pixel, const char *label, char *out, size_t size, size_t used) {
    if (r->untouched) {
        return Append(out, size, used, "%s: the destination as filled, nothing written", label);
    }
    char fits[40] = "no answer's image";
    if (r->fits != 0u) {
        char answers[32];
        Answers(c, r->fits, answers, sizeof(answers));
        snprintf(fits, sizeof(fits), "fits %s", answers);
    }
    used = Append(out, size, used, "%s: %s, %u of %u right", label, fits, (unsigned)r->right,
                  (unsigned)BLIT2D_WIN_PIXELS);
    if (r->wrong) {
        char got[24], want[24];
        Describe(r->wrong_got, got, sizeof(got));
        Describe(r->wrong_want, want, sizeof(want));
        used = Append(out, size, used, ", first wrong (%u,%u) 0x%08x, %s, wanted %s",
                      (unsigned)r->wrong_x, (unsigned)r->wrong_y, (unsigned)r->wrong_got, got,
                      want);
    }
    used = r->outside != 0u ? Append(out, size, used, ", %u of %u outside the window changed",
                                     (unsigned)r->outside, (unsigned)BLIT2D_OUTSIDE_PIXELS)
                            : Append(out, size, used, ", nothing outside the window changed");
    if (r->fits == 0u) {
        // A result neither answer explains: the texels it took, to name a third rule by.
        for (uint32_t axis = 0; axis < 2u; ++axis) {
            used = Append(out, size, used, axis == 0u ? "; x texels" : ", y texels");
            for (uint32_t n = 0; n < 8u; ++n) {
                char one[8];
                const uint32_t x = BLIT2D_WIN_X + (axis == 0u ? n : 0u);
                const uint32_t y = BLIT2D_WIN_Y + (axis == 0u ? 0u : n);
                Axis(pixel(read, x, y), axis != 0u, one, sizeof(one));
                used = Append(out, size, used, "%s", one);
            }
        }
    }
    return used;
}

void Blit2dDetail(const Blit2dCase *c, const Blit2dVerdict *v, const uint8_t *raw,
                  const uint8_t *buffer, char *out, size_t size) {
    char md5[33], answers[32];
    Blit2dMd5Hex(raw, BLIT2D_DST_BYTES, md5);
    if (v->passed) {
        Answers(c, v->fits, answers, sizeof(answers));
        snprintf(out, size,
                 "fits %s: raw MD5 %s, all %u right and nothing outside the window changed in "
                 "both reads",
                 answers, md5, (unsigned)BLIT2D_WIN_PIXELS);
        return;
    }
    size_t used = Append(out, size, 0, "raw MD5 %s; ", md5);
    used = ReadText(c, &v->raw, raw, Blit2dRawPixel, "raw", out, size, used);
    used = Append(out, size, used, "; ");
    used = ReadText(c, &v->buffer, buffer, Blit2dBufferPixel, "copy to buffer", out, size, used);
    Answers(c, 1u << v->shown, answers, sizeof(answers));
    Append(out, size, used, "; counted against %s", answers);
}

/// What the cases in @p use answer, as text; false when none of them answers anything.
static bool Settle(const uint32_t fits[BLIT2D_CASES], const bool use[BLIT2D_CASES], char *out,
                   size_t size) {
    // Bit 0 an A seen, bit 1 a B, per origin for Q1 and once for Q2.
    uint32_t corner = 0, centre = 0, q2 = 0;
    for (uint32_t i = 0; i < BLIT2D_CASES; ++i) {
        const Blit2dCase *const c = &CASES[i];
        if (!use[i] || fits[i] == 0u) {
            continue;
        }
        const char *const a1 = AnswerOf(fits[i], BLIT2D_Q1_B);
        const uint32_t seen1 = a1[0] == 'A' ? 1u : a1[0] == 'B' ? 2u : 0u;
        *(c->origin == BLIT2D_ORIGIN_CORNER ? &corner : &centre) |= seen1;
        if (Q2Open(c)) {
            const char *const a2 = AnswerOf(fits[i], BLIT2D_Q2_B);
            q2 |= a2[0] == 'A' ? 1u : a2[0] == 'B' ? 2u : 0u;
        }
    }
    const uint32_t both = corner | centre;
    const char *q1 = both == 0u                       ? NULL
                     : corner == 3u || centre == 3u   ? "Q1 apart, cases of one origin disagree"
                     : both == 1u                     ? "Q1 A"
                     : both == 2u                     ? "Q1 B"
                     : corner == 1u                   ? "Q1 neither, both origins sample at n du"
                                                      : "Q1 neither, both origins sample at "
                                                        "(n + 1/2) du";
    const char *const q2_text = q2 == 1u   ? "Q2 A"
                                : q2 == 2u ? "Q2 B"
                                : q2 == 3u ? "Q2 apart"
                                           : NULL;
    if (q1 == NULL && q2_text == NULL) {
        return false;
    }
    snprintf(out, size, "%s%s%s", q1 != NULL ? q1 : "", q1 != NULL && q2_text != NULL ? ", " : "",
             q2_text != NULL ? q2_text : "");
    return true;
}

bool Blit2dNote(const uint32_t fits[BLIT2D_CASES], const uint32_t buffer[BLIT2D_CASES],
                char *out, size_t size) {
    bool passed[BLIT2D_CASES], failed[BLIT2D_CASES];
    for (uint32_t i = 0; i < BLIT2D_CASES; ++i) {
        passed[i] = fits[i] != 0u;
        failed[i] = !passed[i];
    }
    char from_passed[96], from_buffer[96];
    const bool p = Settle(fits, passed, from_passed, sizeof(from_passed));
    const bool b = Settle(buffer, failed, from_buffer, sizeof(from_buffer));
    if (p && b) {
        snprintf(out, size,
                 "blit2d: the passing cases answer %s; the copies to buffer of the failing "
                 "ones answer %s",
                 from_passed, from_buffer);
    } else if (p) {
        snprintf(out, size, "blit2d: the passing cases answer %s", from_passed);
    } else if (b) {
        snprintf(out, size, "blit2d: no case passed; the copies to buffer answer %s",
                 from_buffer);
    }
    return p || b;
}
