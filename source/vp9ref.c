// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vp9ref.h"

#include <stdio.h>
#include <string.h>

#include "coherref.h"
#include "md5.h"

_Static_assert(VP9_AT_STATUS >= VP9_AT_SETUP + VP9_SETUP_SIZE, "the setup fits");
_Static_assert(VP9_AT_COMMANDS >= VP9_AT_STATUS + 0x100u, "the status fits");
_Static_assert(VP9_AT_PROBS >= VP9_AT_COMMANDS + VP9_COMMAND_WORDS * 4u, "the commands fit");
_Static_assert(VP9_AT_TILES >= VP9_AT_PROBS + VP9_PROBS_SIZE, "the table fits");
_Static_assert(VP9_AT_COUNTERS >= VP9_AT_TILES + VP9_TILES_SIZE, "the tile sizes fit");
_Static_assert(VP9_TILES_MAX * 2u * 2u <= 0x37Au * 2u, "the tile sizes end before the fixed words");
_Static_assert((VP9_AT_SURFACES & 0xFFu) == 0u && (VP9_SURFACE_MAX & 0x3FFu) == 0u,
               "surfaces start on a block");

Vp9Layout Vp9LayoutOf(uint32_t width, uint32_t height) {
    Vp9Layout l;
    l.stride = (((width + 1u) & ~1u) + 63u) & ~63u;
    l.rows = (((height + 1u) & ~1u) + 31u) & ~31u;
    l.chroma = l.stride * l.rows;
    l.size = l.chroma + l.stride * l.rows / 2u;
    return l;
}

static const char *const QUESTION_NAMES[VP9_QUESTIONS] = {"Q1", "Q2", "Q3", "Q4"};

const char *Vp9QuestionName(Vp9Question question) {
    return question < VP9_QUESTIONS ? QUESTION_NAMES[question] : "Q?";
}

#define SETUP_FLAGS 0x68u
#define SETUP_LAST_SHOWN 0x10u
#define SETUP_CHROMA_AC 0x74u
#define SETUP_CHROMA_DC 0x75u
#define SETUP_SEGMENT_MAP_UPDATE 0x81u

Vp9Question Vp9CaseQuestion(const Vp9Case *c) {
    bool after = false, q3 = false, q4 = false;
    for (uint32_t i = 0; i < c->frame_count; ++i) {
        const Vp9Frame *const f = &c->frames[i];
        if (f->existing) {
            after = true;
            continue;
        }
        q3 |= after && (f->setup[SETUP_FLAGS] & SETUP_LAST_SHOWN) == 0u;
        q4 |= f->setup[SETUP_CHROMA_AC] != f->setup[SETUP_CHROMA_DC];
        after = false;
    }
    return q3 ? VP9_Q3 : q4 ? VP9_Q4 : VP9_QUESTIONS;
}

void Vp9Setup(const Vp9Frame *f, bool after_existing, Vp9Question question, bool b,
              uint8_t out[VP9_SETUP_SIZE]) {
    memcpy(out, f->setup, VP9_SETUP_SIZE);
    if (b && question == VP9_Q3 && after_existing) {
        out[SETUP_FLAGS] |= SETUP_LAST_SHOWN;
    }
    if (b && question == VP9_Q4) {
        const uint8_t ac = out[SETUP_CHROMA_AC];
        out[SETUP_CHROMA_AC] = out[SETUP_CHROMA_DC];
        out[SETUP_CHROMA_DC] = ac;
    }
}

void Vp9TableApply(const Vp9Frame *f, uint8_t table[VP9_PROBS_SIZE]) {
    if (f->probs != NULL) {
        memcpy(table, f->probs, VP9_PROBS_SIZE);
        return;
    }
    for (uint32_t i = 0; i < f->patch_count; ++i) {
        const uint32_t at = f->patch[i] >> 8;
        if (at < VP9_PROBS_SIZE) {
            table[at] = (uint8_t)(f->patch[i] & 0xFFu);
        }
    }
}

// The host interface's method registers, as the vic suite drives them: METHOD0 takes the
// method's word offset, METHOD1 its value.
#define THI_METHOD0 0x10u
#define THI_INCR_SYNCPT 0x00u

static uint32_t Incr(uint32_t offset, uint32_t count) {
    return (1u << 28) | (offset << 16) | count;
}

static uint32_t NonIncr(uint32_t offset, uint32_t count) {
    return (2u << 28) | (offset << 16) | count;
}

static size_t Method(uint32_t *words, size_t n, size_t max, uint32_t method, uint32_t value) {
    if (n + 3u > max) {
        return max + 1u;
    }
    words[n] = Incr(THI_METHOD0, 2u);
    words[n + 1u] = method >> 2;
    words[n + 2u] = value;
    return n + 3u;
}

size_t Vp9Commands(uint32_t *words, size_t max, const Vp9JobAt *at, uint32_t syncpt) {
    if (max < 1u) {
        return 0;
    }
    const uint32_t base = at->iova;
    const bool even = at->index % 2u == 0u;
    size_t n = 0;
    words[n++] = NVDEC_CLASS << 6; // SETCLASS, no offset, no mask
    n = Method(words, n, max, 0x200u, 9u);                      // SET_APPLICATION_ID, VP9
    // SET_CONTROL_PARAMS: the codec, ERR_CONCEAL_ON (bit 6) and GPTIMER_ON (bit 4).
    n = Method(words, n, max, 0x400u, 9u | 1u << 6 | 1u << 4);
    n = Method(words, n, max, 0x40Cu, at->index);               // SET_PICTURE_INDEX
    n = Method(words, n, max, 0x404u, (base + VP9_AT_SETUP) >> 8);
    n = Method(words, n, max, 0x408u, (base + VP9_AT_BITSTREAM) >> 8);
    n = Method(words, n, max, 0x424u, (base + VP9_AT_STATUS) >> 8);
    n = Method(words, n, max, 0x5C0u, (base + VP9_AT_PROBS) >> 8);
    n = Method(words, n, max, 0x5C4u, (base + VP9_AT_COUNTERS) >> 8);
    n = Method(words, n, max, 0x5D0u, (base + VP9_AT_TILES) >> 8);
    // The motion vectors one job writes, the next reads.
    n = Method(words, n, max, 0x5D4u, (base + (even ? VP9_AT_COLMV0 : VP9_AT_COLMV1)) >> 8);
    n = Method(words, n, max, 0x5D8u, (base + (even ? VP9_AT_COLMV1 : VP9_AT_COLMV0)) >> 8);
    const uint32_t read = at->segment_read != 0u ? VP9_AT_SEGMENT1 : VP9_AT_SEGMENT0;
    const uint32_t write = at->segment_read != 0u ? VP9_AT_SEGMENT0 : VP9_AT_SEGMENT1;
    n = Method(words, n, max, 0x5C8u, (base + read) >> 8);
    n = Method(words, n, max, 0x5CCu, (base + write) >> 8);
    n = Method(words, n, max, 0x5DCu, (base + VP9_AT_FILTER) >> 8);
    for (uint32_t i = 0; i < 4u; ++i) {
        const uint32_t surface = base + VP9_AT_SURFACES + at->surfaces[i] * at->surface_size;
        n = Method(words, n, max, 0x430u + 4u * i, surface >> 8);
        n = Method(words, n, max, 0x474u + 4u * i, (surface + at->chroma) >> 8);
    }
    n = Method(words, n, max, 0x300u, 1u << 8); // Execute, AWAKEN
    if (n + 2u > max) {
        return 0;
    }
    // INCR_SYNCPT: the index in bits 7:0, the condition in 15:8, 1 being OP_DONE.
    words[n++] = NonIncr(THI_INCR_SYNCPT, 1u);
    words[n++] = 1u << 8 | (syncpt & 0xFFu);
    return n;
}

// One image's planes, I420, while they are hashed.
static uint8_t planes[VP9_WIDTH_MAX * VP9_HEIGHT_MAX +
                      2u * ((VP9_WIDTH_MAX + 1u) / 2u) * ((VP9_HEIGHT_MAX + 1u) / 2u)];

void Vp9ReadImage(const uint8_t *surface, const Vp9Layout *layout, uint32_t width,
                  uint32_t height, Vp9Image *out) {
    memset(out, 0, sizeof(*out));
    if (width > VP9_WIDTH_MAX || height > VP9_HEIGHT_MAX) {
        return;
    }
    const uint32_t cw = (width + 1u) / 2u, ch = (height + 1u) / 2u;
    uint8_t *const y_plane = planes;
    uint8_t *const u_plane = planes + width * height;
    uint8_t *const v_plane = u_plane + cw * ch;
    // A gob's rows are runs of 16 bytes, so each run is read whole.
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; x += 16u) {
            const uint32_t n = width - x < 16u ? width - x : 16u;
            memcpy(y_plane + y * width + x,
                   surface + CohBlockLinearOffset(x, y, layout->stride, 2u), n);
        }
    }
    const uint8_t *const chroma = surface + layout->chroma;
    for (uint32_t y = 0; y < ch; ++y) {
        for (uint32_t x = 0; x < 2u * cw; x += 16u) {
            const uint8_t *const run = chroma + CohBlockLinearOffset(x, y, layout->stride, 2u);
            for (uint32_t i = 0; i < 16u && x + i < 2u * cw; i += 2u) {
                u_plane[y * cw + (x + i) / 2u] = run[i];
                v_plane[y * cw + (x + i) / 2u] = run[i + 1u];
            }
        }
    }
    const uint32_t sizes[3] = {width * height, cw * ch, cw * ch};
    const uint8_t *const starts[3] = {y_plane, u_plane, v_plane};
    MD5_CTX whole;
    MD5_Init(&whole);
    for (uint32_t p = 0; p < 3u; ++p) {
        MD5_CTX one;
        MD5_Init(&one);
        MD5_Update(&one, starts[p], sizes[p]);
        MD5_Final(out->md5[p + 1u], &one);
        MD5_Update(&whole, starts[p], sizes[p]);
    }
    MD5_Final(out->md5[0], &whole);
    out->blank = true;
    for (uint32_t i = 0; i < sizes[0] + sizes[1] + sizes[2] && out->blank; ++i) {
        out->blank = planes[i] == VP9_FILL;
    }
    out->read = true;
}

void Vp9Judge(const Vp9Case *c, const Vp9Pass *pass, Vp9Verdict *v) {
    memset(v, 0, sizeof(*v));
    v->images = c->image_count;
    v->first = -1;
    uint32_t k = 0;
    for (uint32_t i = 0; i < c->frame_count && i < VP9_FRAMES_MAX; ++i) {
        const Vp9Frame *const f = &c->frames[i];
        const bool ran = i < pass->frames_run && pass->now[i].read;
        if (f->shown && k < c->image_count) {
            if (ran && memcmp(pass->now[i].md5[0], c->images[k], 16u) == 0) {
                ++v->right_now;
            } else if (v->first < 0) {
                v->first = (int32_t)k;
                v->first_frame = i;
            }
            if (pass->later[i].read) {
                ++v->later_read;
                v->right_later += memcmp(pass->later[i].md5[0], c->images[k], 16u) == 0;
            }
            ++k;
        } else if (!f->shown && ran) {
            ++v->hidden;
            v->hidden_right += memcmp(pass->now[i].md5[0], f->md5[0], 16u) == 0;
        }
    }
    v->q1a = v->right_now == v->images;
    v->q1b = !v->q1a && pass->failure[0] == '\0' && v->later_read > 0u &&
             v->right_later == v->later_read;
    v->passed = (v->q1a || v->q1b) && pass->failure[0] == '\0';
}

/// What is wrong with the first wrong image: the job never ran, nothing was written, or which
/// planes differ from FFmpeg's.
static void Diagnose(const Vp9Case *c, const Vp9Pass *pass, const Vp9Verdict *v, char *out,
                     size_t size) {
    const uint32_t i = v->first_frame;
    const Vp9Image *const image = &pass->now[i];
    if (i >= pass->frames_run || !image->read) {
        snprintf(out, size, "never read, %s", pass->failure[0] ? pass->failure : "not reached");
        return;
    }
    if (image->blank) {
        snprintf(out, size, "nothing was written there");
        return;
    }
    static const char *const PLANE[3] = {"Y", "U", "V"};
    size_t n = 0;
    for (uint32_t p = 0; p < 3u && n < size; ++p) {
        const bool right = memcmp(image->md5[p + 1u], c->frames[i].md5[p + 1u], 16u) == 0;
        n += (size_t)snprintf(out + n, size - n, "%s%s %s", p ? ", " : "", PLANE[p],
                              right ? "right" : "wrong");
    }
    if (n < size && (pass->error_status[i] != 0u || pass->mbs_in_error[i] != 0u)) {
        snprintf(out + n, size - n, "; the engine said error 0x%x, %u macroblocks in error",
                 (unsigned)pass->error_status[i], (unsigned)pass->mbs_in_error[i]);
    }
}

/// One pass in a sentence: how many images were right, and the first one wrong.
static size_t Summary(const Vp9Case *c, const Vp9Pass *pass, const Vp9Verdict *v, char *out,
                      size_t size) {
    size_t n = (size_t)snprintf(out, size, "%u of %u images right at their fence",
                                (unsigned)v->right_now, (unsigned)v->images);
    if (!v->q1a && v->later_read > 0u && n < size) {
        n += (size_t)snprintf(out + n, size - n, ", %u of %u a job later",
                              (unsigned)v->right_later, (unsigned)v->later_read);
    }
    if (v->first >= 0 && n < size) {
        char why[160];
        Diagnose(c, pass, v, why, sizeof(why));
        n += (size_t)snprintf(out + n, size - n, "; first wrong image %d (frame %u): %s",
                              (int)v->first + 1, (unsigned)v->first_frame, why);
    }
    if (v->hidden > 0u && n < size) {
        n += (size_t)snprintf(out + n, size - n, "; hidden frames written %u of %u",
                              (unsigned)v->hidden_right, (unsigned)v->hidden);
    }
    // A stop the first wrong image did not already explain.
    const bool said = v->first >= 0 && v->first_frame >= pass->frames_run;
    if (pass->failure[0] != '\0' && !said && n < size) {
        n += (size_t)snprintf(out + n, size - n, "; %s", pass->failure);
    }
    return n < size ? n : size - 1u;
}

static uint32_t Bit(Vp9Question q, bool b) {
    return 1u << (2u * (uint32_t)q + (b ? 1u : 0u));
}

static uint32_t PassFits(const Vp9Verdict *v) {
    uint32_t fits = 0;
    if (v->passed) {
        fits |= Bit(VP9_Q1, v->q1b);
        if (v->hidden > 0u && (v->hidden_right == v->hidden || v->hidden_right == 0u)) {
            fits |= Bit(VP9_Q2, v->hidden_right == 0u);
        }
    }
    return fits;
}

bool Vp9Detail(const Vp9Case *c, const Vp9Pass *passes, uint32_t pass_count, char *out,
               size_t size, uint32_t *fits) {
    Vp9Verdict v[2];
    *fits = 0;
    bool passed = false;
    size_t n = 0;
    for (uint32_t p = 0; p < pass_count && p < 2u; ++p) {
        Vp9Judge(c, &passes[p], &v[p]);
        passed |= v[p].passed;
        *fits |= PassFits(&v[p]);
        if (pass_count > 1u && v[p].passed) {
            *fits |= Bit(passes[p].question, passes[p].b);
        }
        if (n < size) {
            if (pass_count > 1u) {
                n += (size_t)snprintf(out + n, size - n, "%sform %s: ", p ? "; " : "",
                                      passes[p].b ? "B" : "A");
            }
            if (n < size) {
                n += Summary(c, &passes[p], &v[p], out + n, size - n);
            }
        }
    }
    if (passed && n < size) {
        char list[96];
        size_t m = 0;
        list[0] = '\0';
        for (uint32_t q = 0; q < VP9_QUESTIONS && m < sizeof(list); ++q) {
            const uint32_t bits = *fits >> (2u * q) & 3u;
            if (bits != 0u) {
                m += (size_t)snprintf(list + m, sizeof(list) - m, "%s%s %s", m ? ", " : "",
                                      QUESTION_NAMES[q],
                                      bits == 3u ? "either" : bits == 1u ? "A" : "B");
            }
        }
        snprintf(out + n, size - n, " (fits %s)", list);
    }
    return passed;
}

void Vp9Settle(uint32_t fits, uint8_t settled[VP9_QUESTIONS]) {
    for (uint32_t q = 0; q < VP9_QUESTIONS; ++q) {
        const uint32_t bits = fits >> (2u * q) & 3u;
        // A case where both answers fit settles nothing.
        if (bits == 1u || bits == 2u) {
            settled[q] |= (uint8_t)bits;
        }
    }
}

size_t Vp9Settled(const uint8_t settled[VP9_QUESTIONS], char *out, size_t size) {
    size_t n = 0;
    out[0] = '\0';
    for (uint32_t q = 0; q < VP9_QUESTIONS && n < size; ++q) {
        if (settled[q] == 0u) {
            continue;
        }
        n += (size_t)snprintf(out + n, size - n, "%s%s %s", n ? ", " : "", QUESTION_NAMES[q],
                              settled[q] == 3u ? "A and B, cases disagree"
                              : settled[q] == 1u ? "A"
                                                 : "B");
    }
    return n < size ? n : size - 1u;
}

uint32_t Vp9Fnv(uint32_t hash, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 16777619u;
    }
    return hash;
}
