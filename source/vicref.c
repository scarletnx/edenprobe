// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vicref.h"

#include <stdio.h>
#include <string.h>

// Bit positions in the configuration structure, from libdrm's vic40.h: each group starts at its
// byte offset times eight, and a slot's at 0x90 plus 0xB0 per slot.
enum {
    PIPE_DOWNSAMPLE_HORIZ = 0,
    PIPE_DOWNSAMPLE_VERT = 16,
    OUTPUT = 0x10 * 8,
    OUT_ALPHA_FILL_MODE = OUTPUT + 0,
    OUT_ALPHA_FILL_SLOT = OUTPUT + 3,
    OUT_BACKGROUND_A = OUTPUT + 6,
    OUT_BACKGROUND_R = OUTPUT + 16,
    OUT_BACKGROUND_G = OUTPUT + 26,
    OUT_BACKGROUND_B = OUTPUT + 36,
    OUT_TARGET_LEFT = OUTPUT + 64,
    OUT_TARGET_RIGHT = OUTPUT + 80,
    OUT_TARGET_TOP = OUTPUT + 96,
    OUT_TARGET_BOTTOM = OUTPUT + 112,
    SURFACE = 0x20 * 8,
    SURF_FORMAT = SURFACE + 0,
    SURF_WIDTH = SURFACE + 32,
    SURF_HEIGHT = SURFACE + 46,
    SURF_LUMA_WIDTH = SURFACE + 64,
    SURF_LUMA_HEIGHT = SURFACE + 78,
    SURF_CHROMA_WIDTH = SURFACE + 96,
    SURF_CHROMA_HEIGHT = SURFACE + 110,
    SLOTS = 0x90 * 8,
    SLOT_BITS = 0xB0 * 8,
    // SlotConfig, from the slot's start
    SLOT_ENABLE = 0,
    SLOT_CURRENT_FIELD = 8,
    SLOT_CLAMP_LOW = 128,
    SLOT_CLAMP_HIGH = 138,
    SLOT_PLANAR_ALPHA = 160,
    SLOT_CONSTANT_ALPHA = 170,
    SLOT_SOURCE_LEFT = 256,
    SLOT_SOURCE_RIGHT = 288,
    SLOT_SOURCE_TOP = 320,
    SLOT_SOURCE_BOTTOM = 352,
    SLOT_DEST_LEFT = 384,
    SLOT_DEST_RIGHT = 400,
    SLOT_DEST_TOP = 416,
    SLOT_DEST_BOTTOM = 432,
    // SlotSurfaceConfig, 0x40 bytes in
    SLOT_SURFACE = 0x40 * 8,
    SLOT_FORMAT = SLOT_SURFACE + 0,
    SLOT_CACHE_WIDTH = SLOT_SURFACE + 19,
    SLOT_WIDTH = SLOT_SURFACE + 32,
    SLOT_HEIGHT = SLOT_SURFACE + 46,
    SLOT_LUMA_WIDTH = SLOT_SURFACE + 64,
    SLOT_LUMA_HEIGHT = SLOT_SURFACE + 78,
    SLOT_CHROMA_WIDTH = SLOT_SURFACE + 96,
    SLOT_CHROMA_HEIGHT = SLOT_SURFACE + 110,
    // the colour matrix, 0x60 bytes in: one 64-bit word per column, rows at 0, 20 and 40
    SLOT_MATRIX = 0x60 * 8,
    SLOT_MATRIX_SHIFT = SLOT_MATRIX + 60,
    SLOT_MATRIX_ENABLE = SLOT_MATRIX + 127,
};

static void Put(uint8_t *config, uint32_t bit, uint32_t width, uint32_t value) {
    for (uint32_t i = 0; i < width; ++i) {
        const uint32_t at = bit + i;
        const uint8_t mask = (uint8_t)(1u << (at % 8u));
        if ((value >> i) & 1u) {
            config[at / 8u] |= mask;
        } else {
            config[at / 8u] &= (uint8_t)~mask;
        }
    }
}

void VicPack(const VicJob *job, uint8_t config[VIC_CONFIG_SIZE]) {
    memset(config, 0, VIC_CONFIG_SIZE);
    // One in U9.2: no downsampling, as the homebrew transfer that runs on the console sets it.
    Put(config, PIPE_DOWNSAMPLE_HORIZ, 11, 4u);
    Put(config, PIPE_DOWNSAMPLE_VERT, 11, 4u);
    Put(config, OUT_ALPHA_FILL_MODE, 3, job->alpha_fill_mode);
    Put(config, OUT_ALPHA_FILL_SLOT, 3, job->alpha_fill_slot);
    Put(config, OUT_BACKGROUND_R, 10, job->background[0]);
    Put(config, OUT_BACKGROUND_G, 10, job->background[1]);
    Put(config, OUT_BACKGROUND_B, 10, job->background[2]);
    Put(config, OUT_BACKGROUND_A, 10, job->background[3]);
    Put(config, OUT_TARGET_LEFT, 14, job->target.left);
    Put(config, OUT_TARGET_RIGHT, 14, job->target.right);
    Put(config, OUT_TARGET_TOP, 14, job->target.top);
    Put(config, OUT_TARGET_BOTTOM, 14, job->target.bottom);
    Put(config, SURF_FORMAT, 7, job->out_format);
    Put(config, SURF_WIDTH, 14, job->out_width - 1u);
    Put(config, SURF_HEIGHT, 14, VIC_SIZE - 1u);
    Put(config, SURF_LUMA_WIDTH, 14, VIC_LUMA_STRIDE - 1u);
    Put(config, SURF_LUMA_HEIGHT, 14, VIC_SIZE - 1u);
    Put(config, SURF_CHROMA_WIDTH, 14, job->out_chroma_width);
    Put(config, SURF_CHROMA_HEIGHT, 14, job->out_chroma_height);
    for (uint32_t n = 0; n < job->slot_count && n < VIC_SLOTS_MAX; ++n) {
        const VicSlot *const s = &job->slots[n];
        const uint32_t at = SLOTS + n * SLOT_BITS;
        Put(config, at + SLOT_ENABLE, 1, s->enable ? 1u : 0u);
        Put(config, at + SLOT_CURRENT_FIELD, 1, 1u);
        Put(config, at + SLOT_CLAMP_LOW, 10, s->clamp_low);
        Put(config, at + SLOT_CLAMP_HIGH, 10, s->clamp_high);
        Put(config, at + SLOT_PLANAR_ALPHA, 10, s->planar_alpha);
        Put(config, at + SLOT_CONSTANT_ALPHA, 1, 1u);
        Put(config, at + SLOT_SOURCE_LEFT, 30, (uint32_t)s->source.left << 16);
        Put(config, at + SLOT_SOURCE_RIGHT, 30, (uint32_t)s->source.right << 16);
        Put(config, at + SLOT_SOURCE_TOP, 30, (uint32_t)s->source.top << 16);
        Put(config, at + SLOT_SOURCE_BOTTOM, 30, (uint32_t)s->source.bottom << 16);
        Put(config, at + SLOT_DEST_LEFT, 14, s->dest.left);
        Put(config, at + SLOT_DEST_RIGHT, 14, s->dest.right);
        Put(config, at + SLOT_DEST_TOP, 14, s->dest.top);
        Put(config, at + SLOT_DEST_BOTTOM, 14, s->dest.bottom);
        Put(config, at + SLOT_FORMAT, 7, s->format);
        Put(config, at + SLOT_CACHE_WIDTH, 3, 2u); // 64Bx4, which libdrm uses for pitch linear
        Put(config, at + SLOT_WIDTH, 14, VIC_SIZE - 1u);
        Put(config, at + SLOT_HEIGHT, 14, VIC_SIZE - 1u);
        Put(config, at + SLOT_LUMA_WIDTH, 14, VIC_LUMA_STRIDE - 1u);
        Put(config, at + SLOT_LUMA_HEIGHT, 14, VIC_SIZE - 1u);
        // The chroma plane's stride in pairs and its rows, each minus one.
        Put(config, at + SLOT_CHROMA_WIDTH, 14, VIC_LUMA_STRIDE / 2u - 1u);
        Put(config, at + SLOT_CHROMA_HEIGHT, 14, VIC_CHROMA_ROWS - 1u);
        if (s->matrix) {
            Put(config, at + SLOT_MATRIX_ENABLE, 1, 1u);
            Put(config, at + SLOT_MATRIX_SHIFT, 4, s->matrix_shift);
            for (uint32_t column = 0; column < 4u; ++column) {
                for (uint32_t row = 0; row < 3u; ++row) {
                    Put(config, at + SLOT_MATRIX + column * 64u + row * 20u, 20,
                        (uint32_t)s->m[row][column]);
                }
            }
        }
    }
}

void VicImageWrite(const VicImage *image, uint8_t *luma, uint8_t *chroma) {
    memset(luma, 0, VIC_LUMA_STRIDE * VIC_SIZE);
    memset(chroma, 0, VIC_LUMA_STRIDE * VIC_CHROMA_ROWS);
    for (uint32_t y = 0; y < VIC_SIZE; ++y) {
        for (uint32_t x = 0; x < VIC_SIZE; ++x) {
            luma[y * VIC_LUMA_STRIDE + x] = image->standard ? (uint8_t)(16u + 14u * x) : image->y;
        }
    }
    for (uint32_t y = 0; y < VIC_CHROMA_ROWS; ++y) {
        for (uint32_t x = 0; x < VIC_SIZE; x += 2u) {
            chroma[y * VIC_LUMA_STRIDE + x] = image->standard ? 90u : image->u;
            chroma[y * VIC_LUMA_STRIDE + x + 1u] = image->standard ? 240u : image->v;
        }
    }
}

bool VicOutputIsNv12(const VicJob *job) {
    return job->out_format == VIC_FORMAT_Y8_V8U8_N420;
}

size_t VicOutputSize(const VicJob *job) {
    return VicOutputIsNv12(job) ? VIC_LUMA_STRIDE * (VIC_SIZE + VIC_CHROMA_ROWS)
                                : VIC_RGBA_PITCH * VIC_SIZE;
}

/// An arithmetic shift right that floors, whatever the compiler does with a negative operand.
static int32_t FloorShift(int64_t value, uint32_t bits) {
    const int64_t divisor = (int64_t)1 << bits;
    int64_t q = value / divisor;
    if (value % divisor != 0 && value < 0) {
        --q;
    }
    return (int32_t)q;
}

int32_t VicMatrixChannel(const int32_t m[3][4], uint32_t row, int32_t y10, int32_t u10,
                         int32_t v10, uint32_t shift, bool after, bool rounding) {
    const int64_t sum = (int64_t)y10 * m[row][0] + (int64_t)u10 * m[row][1] +
                        (int64_t)v10 * m[row][2];
    const int64_t t = after ? FloorShift(sum + m[row][3], shift)
                            : (int64_t)FloorShift(sum, shift) + m[row][3];
    return FloorShift(t + (rounding ? 128 : 0), 8);
}

static bool Answer(VicRules rules, VicQuestion question) {
    return (rules >> question) & 1u;
}

static uint16_t Clamp(int32_t value, uint16_t low, uint16_t high) {
    return (uint16_t)(value < low ? low : value > high ? high : value);
}

static bool Inside(const VicRect *r, uint32_t x, uint32_t y) {
    return x >= r->left && x <= r->right && y >= r->top && y <= r->bottom;
}

/// One output pixel in ten bits, or false where the engine leaves the byte as it was.
static bool Pixel(const VicJob *job, VicRules rules, uint32_t x, uint32_t y, uint16_t px[4]) {
    if (!Inside(&job->target, x, y)) {
        if (!Answer(rules, VIC_Q7)) {
            return false;
        }
        memcpy(px, job->background, sizeof(job->background));
        return true;
    }
    memcpy(px, job->background, sizeof(job->background));
    bool covered = false;
    for (uint32_t k = 0; k < job->slot_count; ++k) {
        // Later slots go over earlier ones, or the other way round.
        const uint32_t n = Answer(rules, VIC_Q6) ? job->slot_count - 1u - k : k;
        const VicSlot *const s = &job->slots[n];
        if (!s->enable || !Inside(&s->dest, x, y)) {
            continue;
        }
        const uint32_t sx = s->source.left + (x - s->dest.left);
        const uint8_t yv = s->image.standard ? (uint8_t)(16u + 14u * sx) : s->image.y;
        const uint8_t even = s->image.standard ? 90u : s->image.u;
        const uint8_t odd = s->image.standard ? 240u : s->image.v;
        bool u_even = !Answer(rules, VIC_Q11);
        if (s->format == VIC_FORMAT_Y8_U8V8_N420 && !Answer(rules, VIC_Q8)) {
            u_even = !u_even;
        }
        const int32_t y10 = (int32_t)yv << 2;
        const int32_t u10 = (int32_t)(u_even ? even : odd) << 2;
        const int32_t v10 = (int32_t)(u_even ? odd : even) << 2;
        int32_t rgb[3] = {y10, u10, v10};
        bool clamp = !Answer(rules, VIC_Q10);
        if (s->matrix) {
            for (uint32_t row = 0; row < 3u; ++row) {
                rgb[row] = VicMatrixChannel(s->m, row, y10, u10, v10, s->matrix_shift,
                                            Answer(rules, VIC_Q3), Answer(rules, VIC_Q9));
            }
            clamp = true;
        }
        uint16_t alpha = s->planar_alpha;
        for (uint32_t c = 0; c < 3u; ++c) {
            px[c] = clamp ? Clamp(rgb[c], s->clamp_low, s->clamp_high) : (uint16_t)rgb[c];
        }
        if (clamp && !Answer(rules, VIC_Q4)) {
            alpha = Clamp(alpha, s->clamp_low, s->clamp_high);
        }
        px[3] = alpha;
        covered = true;
    }
    if (job->alpha_fill_mode == 0u && !Answer(rules, VIC_Q5)) {
        px[3] = 1023u;
    } else if (job->alpha_fill_mode == 3u && !covered) {
        px[3] = job->background[3];
    }
    return true;
}

void VicModel(const VicJob *job, VicRules rules, uint8_t *out) {
    memset(out, VIC_FILL, VicOutputSize(job));
    const bool nv12 = VicOutputIsNv12(job);
    uint8_t *const chroma = out + VIC_LUMA_STRIDE * VIC_SIZE;
    for (uint32_t y = 0; y < VIC_SIZE; ++y) {
        for (uint32_t x = 0; x < job->out_width; ++x) {
            uint16_t px[4];
            if (!Pixel(job, rules, x, y, px)) {
                continue;
            }
            const uint8_t r = (uint8_t)(px[0] >> 2), g = (uint8_t)(px[1] >> 2);
            const uint8_t b = (uint8_t)(px[2] >> 2), a = (uint8_t)(px[3] >> 2);
            if (nv12) {
                out[y * VIC_LUMA_STRIDE + x] = r;
                if (y % 2u == 0u && x % 2u == 0u) {
                    // Written back in the order the input was read: what came out of the even
                    // byte goes into it.
                    const bool u_even = !Answer(rules, VIC_Q11);
                    chroma[(y / 2u) * VIC_LUMA_STRIDE + x] = u_even ? g : b;
                    chroma[(y / 2u) * VIC_LUMA_STRIDE + x + 1u] = u_even ? b : g;
                }
                continue;
            }
            uint8_t *const p = out + y * VIC_RGBA_PITCH + x * 4u;
            switch (job->out_format) {
            case VIC_FORMAT_A8R8G8B8:
                p[0] = b, p[1] = g, p[2] = r, p[3] = a;
                break;
            case VIC_FORMAT_X8B8G8R8:
                p[0] = r, p[1] = g, p[2] = b, p[3] = Answer(rules, VIC_Q2) ? 255u : a;
                break;
            default:
                p[0] = r, p[1] = g, p[2] = b, p[3] = a;
                break;
            }
        }
    }
}

// ---- the command words

static uint32_t Incr(uint32_t offset, uint32_t count) {
    return (1u << 28) | (offset << 16) | count;
}

static uint32_t NonIncr(uint32_t offset, uint32_t count) {
    return (2u << 28) | (offset << 16) | count;
}

/// The host interface's method registers: METHOD0 takes the method's word offset, METHOD1 its
/// value, so every method is three words.
#define THI_METHOD0 0x10u
#define THI_INCR_SYNCPT 0x00u

static size_t Method(uint32_t *words, size_t n, size_t max, uint32_t method, uint32_t value) {
    if (n + 3u > max) {
        return max + 1u;
    }
    words[n] = Incr(THI_METHOD0, 2u);
    words[n + 1u] = method >> 2;
    words[n + 2u] = value;
    return n + 3u;
}

size_t VicCommands(uint32_t *words, size_t max, const VicAddresses *at, uint32_t slots,
                   bool nv12_out, uint32_t syncpt) {
    if (max < 1u || slots > VIC_SLOTS_MAX) {
        return 0;
    }
    size_t n = 0;
    words[n++] = VIC_CLASS << 6; // SETCLASS, no offset, no mask
    // The structure's size in 16-byte units at bit 16, GPTIMER_ON and FALCON_CONTROL, as the
    // transfer that runs on the console sends them.
    n = Method(words, n, max, 0x704u, (VIC_CONFIG_SIZE / 16u) << 16 | 1u << 8 | 1u);
    n = Method(words, n, max, 0x708u, at->config >> 8);
    n = Method(words, n, max, 0x720u, at->out_luma >> 8);
    if (nv12_out) {
        n = Method(words, n, max, 0x724u, at->out_chroma >> 8);
    }
    for (uint32_t s = 0; s < slots; ++s) {
        n = Method(words, n, max, 0x400u + s * 0x60u, at->luma[s] >> 8);
        n = Method(words, n, max, 0x404u + s * 0x60u, at->chroma[s] >> 8);
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

uint32_t VicHash(uint32_t hash, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 16777619u;
    }
    return hash;
}

// ---- the cases

static const char *const QUESTION_NAMES[VIC_QUESTIONS] = {
    "Q2", "Q3", "Q4", "Q5", "Q6", "Q7", "Q8", "Q9", "Q10", "Q11",
};

const char *VicQuestionName(VicQuestion question) {
    return question < VIC_QUESTIONS ? QUESTION_NAMES[question] : "Q?";
}

static const int32_t BT601[3][4] = {
    {298, 0, 409, -228480},
    {298, -100, -208, 138624},
    {298, 516, 0, -283264},
};

static VicCase cases[19];
static size_t case_count;

static VicSlot Slot(VicImage image) {
    VicSlot s;
    memset(&s, 0, sizeof(s));
    s.enable = true;
    s.clamp_high = 1023u;
    s.planar_alpha = 1023u;
    s.source = (VicRect){0, 0, 15, 15};
    s.dest = (VicRect){0, 0, 15, 15};
    s.format = VIC_FORMAT_Y8_V8U8_N420;
    s.image = image;
    return s;
}

static VicJob Job(VicSlot slot) {
    VicJob j;
    memset(&j, 0, sizeof(j));
    j.target = (VicRect){0, 0, 15, 15};
    j.out_format = VIC_FORMAT_A8B8G8R8;
    j.out_width = VIC_SIZE;
    j.out_chroma_width = 16383u;
    j.out_chroma_height = 16383u;
    j.slot_count = 1;
    j.slots[0] = slot;
    return j;
}

static void Matrix(VicSlot *s, uint32_t times, uint8_t shift) {
    s->matrix = true;
    s->matrix_shift = shift;
    for (uint32_t r = 0; r < 3u; ++r) {
        for (uint32_t c = 0; c < 4u; ++c) {
            s->m[r][c] = c < 3u ? BT601[r][c] * (int32_t)times : BT601[r][c];
        }
    }
}

static VicCase *Add(const char *name, const char *title, VicQuestion q0, VicQuestion q1,
                    VicQuestion q2) {
    VicCase *const c = &cases[case_count++];
    memset(c, 0, sizeof(*c));
    c->name = name;
    c->title = title;
    const VicQuestion qs[3] = {q0, q1, q2};
    for (uint32_t i = 0; i < 3u; ++i) {
        if (qs[i] < VIC_QUESTIONS) {
            c->questions[c->question_count++] = qs[i];
        }
    }
    return c;
}

static void Push(VicCase *c, VicJob job) {
    c->jobs[c->job_count++] = job;
}

#define NONE VIC_QUESTIONS

static void Build(void) {
    if (case_count != 0u) {
        return;
    }
    const VicImage std = {true, 0, 0, 0};
    VicCase *c;
    VicJob j;
    VicSlot s;

    c = Add("vic_passthrough", "without the matrix, Y, U and V reach R, G and B as they are",
            VIC_Q11, NONE, NONE);
    Push(c, Job(Slot(std)));

    c = Add("vic_chroma_order", "format 67 reads its chroma pair against format 68", VIC_Q8,
            VIC_Q11, NONE);
    s = Slot(std);
    s.format = VIC_FORMAT_Y8_U8V8_N420;
    Push(c, Job(s));

    c = Add("vic_order_argb", "A8R8G8B8 puts blue in the first byte", VIC_Q11, NONE, NONE);
    j = Job(Slot(std));
    j.out_format = VIC_FORMAT_A8R8G8B8;
    Push(c, j);

    c = Add("vic_order_x8", "X8B8G8R8 fills its fourth byte", VIC_Q2, VIC_Q11, NONE);
    s = Slot(std);
    s.planar_alpha = 512u;
    j = Job(s);
    j.out_format = VIC_FORMAT_X8B8G8R8;
    j.alpha_fill_mode = 3u;
    Push(c, j);

    c = Add("vic_matrix_601", "the BT.601 matrix turns six colours into RGB", VIC_Q9, VIC_Q11,
            NONE);
    static const uint8_t COLOURS[6][3] = {{235, 128, 128}, {16, 128, 128}, {126, 128, 128},
                                          {81, 90, 240},   {145, 54, 34},  {41, 240, 110}};
    for (uint32_t i = 0; i < 6u; ++i) {
        s = Slot((VicImage){false, COLOURS[i][0], COLOURS[i][1], COLOURS[i][2]});
        Matrix(&s, 1u, 0u);
        Push(c, Job(s));
    }

    c = Add("vic_matrix_shift", "the matrix shift and the constant column, in their order",
            VIC_Q3, VIC_Q9, NONE);
    s = Slot((VicImage){false, 126, 128, 128});
    Matrix(&s, 4u, 2u);
    Push(c, Job(s));

    c = Add("vic_soft_clamp", "the soft clamp holds the matrix output to 64..940", VIC_Q4,
            VIC_Q9, NONE);
    for (uint32_t i = 0; i < 2u; ++i) {
        s = Slot((VicImage){false, i == 0u ? 235 : 16, 128, 128});
        Matrix(&s, 1u, 0u);
        s.clamp_low = 64u;
        s.clamp_high = 940u;
        j = Job(s);
        j.alpha_fill_mode = 3u;
        Push(c, j);
    }

    c = Add("vic_clamp_nomatrix", "the soft clamp without the matrix", VIC_Q10, VIC_Q5, VIC_Q4);
    for (uint32_t i = 0; i < 2u; ++i) {
        s = Slot((VicImage){false, i == 0u ? 0 : 255, 128, 128});
        s.clamp_low = 64u;
        s.clamp_high = 940u;
        Push(c, Job(s));
    }

    c = Add("vic_alpha_opaque", "the opaque alpha fill over a planar alpha below the top",
            VIC_Q5, VIC_Q11, NONE);
    for (uint32_t i = 0; i < 2u; ++i) {
        s = Slot(std);
        s.planar_alpha = i == 0u ? 512u : 0u;
        Push(c, Job(s));
    }

    c = Add("vic_planar_alpha", "the source stream alpha fill takes the planar alpha", VIC_Q11,
            NONE, NONE);
    static const uint16_t ALPHAS[3] = {1023, 512, 0};
    for (uint32_t i = 0; i < 3u; ++i) {
        s = Slot(std);
        s.planar_alpha = ALPHAS[i];
        j = Job(s);
        j.alpha_fill_mode = 3u;
        Push(c, j);
    }

    c = Add("vic_target_clip", "a target rect inside the surface clips the output", VIC_Q7,
            VIC_Q11, NONE);
    j = Job(Slot(std));
    j.target = (VicRect){4, 4, 11, 11};
    Push(c, j);

    c = Add("vic_target_clip_matrix", "the target rect clips the matrix's output too", VIC_Q7,
            VIC_Q9, NONE);
    s = Slot((VicImage){false, 126, 128, 128});
    Matrix(&s, 1u, 0u);
    j = Job(s);
    j.target = (VicRect){4, 4, 11, 11};
    Push(c, j);

    c = Add("vic_target_odd", "a target rect that starts at column 5", VIC_Q7, VIC_Q11, NONE);
    j = Job(Slot(std));
    j.target.left = 5u;
    Push(c, j);

    static const uint16_t ORANGE[4] = {1023, 512, 0, 1023};
    c = Add("vic_background", "with no slot the target rect takes the background colour",
            VIC_Q7, NONE, NONE);
    s = Slot(std);
    s.enable = false;
    j = Job(s);
    j.target = (VicRect){2, 2, 13, 13};
    memcpy(j.background, ORANGE, sizeof(ORANGE));
    Push(c, j);

    c = Add("vic_background_ring", "the background colour around a smaller destination rect",
            VIC_Q11, NONE, NONE);
    s = Slot(std);
    s.source = (VicRect){4, 4, 11, 11};
    s.dest = (VicRect){4, 4, 11, 11};
    j = Job(s);
    memcpy(j.background, ORANGE, sizeof(ORANGE));
    Push(c, j);

    c = Add("vic_source_crop", "a source rect that starts at column 8", VIC_Q7, VIC_Q11, NONE);
    s = Slot(std);
    s.source = (VicRect){8, 0, 15, 15};
    s.dest = (VicRect){0, 0, 7, 15};
    j = Job(s);
    j.target = (VicRect){0, 0, 7, 15};
    Push(c, j);

    c = Add("vic_two_slots", "two slots over each other with blending off", VIC_Q6, NONE, NONE);
    j = Job(Slot((VicImage){false, 60, 128, 128}));
    s = Slot((VicImage){false, 180, 128, 128});
    s.source = (VicRect){4, 4, 11, 11};
    s.dest = (VicRect){4, 4, 11, 11};
    j.slots[1] = s;
    j.slot_count = 2u;
    Push(c, j);

    c = Add("vic_pitch", "a surface 100 pixels wide keeps the 1024-byte pitch", VIC_Q7, VIC_Q11,
            NONE);
    j = Job(Slot(std));
    j.out_width = 100u;
    Push(c, j);

    c = Add("vic_nv12_out", "NV12 in and NV12 out leaves Y, U and V as they were", NONE, NONE,
            NONE);
    j = Job(Slot(std));
    j.out_format = VIC_FORMAT_Y8_V8U8_N420;
    j.out_chroma_width = VIC_LUMA_STRIDE / 2u - 1u;
    j.out_chroma_height = VIC_CHROMA_ROWS - 1u;
    Push(c, j);
}

size_t VicCaseCount(void) {
    Build();
    return case_count;
}

const VicCase *VicCaseAt(size_t index) {
    Build();
    return index < case_count ? &cases[index] : NULL;
}

uint32_t VicCaseCombinations(const VicCase *c) {
    return 1u << c->question_count;
}

VicRules VicCaseRules(const VicCase *c, uint32_t index) {
    VicRules rules = 0;
    for (uint32_t i = 0; i < c->question_count; ++i) {
        if ((index >> i) & 1u) {
            rules |= 1u << c->questions[i];
        }
    }
    return rules;
}

// ---- the judge

void VicJudge(const VicCase *c, const uint8_t (*got)[VIC_OUTPUT_MAX],
              uint8_t (*expected)[VIC_OUTPUT_MAX], VicVerdict *verdict) {
    memset(verdict, 0, sizeof(*verdict));
    verdict->wrong = UINT32_MAX;
    verdict->untouched = true;
    for (uint32_t j = 0; j < c->job_count; ++j) {
        for (size_t i = 0; i < VIC_OUTPUT_MAX && verdict->untouched; ++i) {
            verdict->untouched = got[j][i] == VIC_FILL;
        }
    }
    const uint32_t count = VicCaseCombinations(c);
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t wrong = 0, job = 0;
        size_t at = 0;
        for (uint32_t j = 0; j < c->job_count; ++j) {
            memset(expected[j], VIC_FILL, VIC_OUTPUT_MAX);
            VicModel(&c->jobs[j], VicCaseRules(c, k), expected[j]);
            for (size_t i = 0; i < VIC_OUTPUT_MAX; ++i) {
                if (got[j][i] != expected[j][i] && wrong++ == 0u) {
                    job = j;
                    at = i;
                }
            }
        }
        if (wrong == 0u) {
            verdict->fits |= 1u << k;
        } else if (verdict->fits == 0u && wrong < verdict->wrong) {
            verdict->wrong = wrong, verdict->shown = k, verdict->job = job, verdict->at = at;
        }
    }
    verdict->passed = verdict->fits != 0u;
    if (verdict->passed) {
        verdict->wrong = 0;
        verdict->shown = (uint32_t)__builtin_ctz(verdict->fits);
    }
    // The buffers end up holding the last combination tried; put back the one shown.
    for (uint32_t j = 0; j < c->job_count; ++j) {
        memset(expected[j], VIC_FILL, VIC_OUTPUT_MAX);
        VicModel(&c->jobs[j], VicCaseRules(c, verdict->shown), expected[j]);
    }
}

/// Per question of @p c, which answers the combinations in @p fits give: bit 0 A, bit 1 B.
static uint8_t Seen(const VicCase *c, uint32_t fits, uint32_t q) {
    uint8_t seen = 0;
    for (uint32_t k = 0; k < VicCaseCombinations(c); ++k) {
        if ((fits >> k) & 1u) {
            seen |= (uint8_t)(((k >> q) & 1u) ? 2u : 1u);
        }
    }
    return seen;
}

static size_t AnswerText(const VicCase *c, uint32_t fits, char *out, size_t out_size) {
    out[0] = '\0';
    for (uint32_t q = 0; q < c->question_count; ++q) {
        const uint8_t seen = Seen(c, fits, q);
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "%s%s %s", used > 0u ? ", " : "",
                 VicQuestionName(c->questions[q]),
                 seen == 1u ? "A" : seen == 2u ? "B" : "either");
    }
    return strlen(out);
}

size_t VicDetail(const VicCase *c, const VicVerdict *verdict, const uint8_t (*got)[VIC_OUTPUT_MAX],
                 const uint8_t (*expected)[VIC_OUTPUT_MAX], char *out, size_t out_size) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    char answers[160];
    if (verdict->passed) {
        AnswerText(c, verdict->fits, answers, sizeof(answers));
        snprintf(out, out_size, "%s%s", c->question_count > 0u ? "fits " : "", answers);
        return strlen(out);
    }
    char under[180] = "";
    if (c->question_count > 0u) {
        AnswerText(c, 1u << verdict->shown, answers, sizeof(answers));
        snprintf(under, sizeof(under), " (%s)", answers);
    }
    const size_t pitch =
        VicOutputIsNv12(&c->jobs[verdict->job]) ? VIC_LUMA_STRIDE : VIC_RGBA_PITCH;
    snprintf(out, out_size,
             "%s%u bytes wrong under the closest answers%s, first in job %u at byte %u (row %u, "
             "byte %u of it): 0x%02x, expected 0x%02x",
             verdict->untouched ? "nothing was written, every byte is still 0xEE; " : "",
             (unsigned)verdict->wrong, under, (unsigned)(verdict->job + 1u),
             (unsigned)verdict->at, (unsigned)(verdict->at / pitch),
             (unsigned)(verdict->at % pitch), got[verdict->job][verdict->at],
             expected[verdict->job][verdict->at]);
    return strlen(out);
}

void VicSettle(const VicCase *c, uint32_t fits, uint8_t settled[VIC_QUESTIONS]) {
    for (uint32_t q = 0; q < c->question_count; ++q) {
        const uint8_t seen = Seen(c, fits, q);
        if (seen != 3u) {
            settled[c->questions[q]] |= seen;
        }
    }
}

size_t VicSettled(const uint8_t settled[VIC_QUESTIONS], char *out, size_t out_size) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    for (uint32_t q = 0; q < VIC_QUESTIONS; ++q) {
        if (settled[q] == 0u) {
            continue;
        }
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "%s%s %s", used > 0u ? ", " : "",
                 VicQuestionName((VicQuestion)q),
                 settled[q] == 1u ? "A" : settled[q] == 2u ? "B" : "A and B, cases disagree");
    }
    return strlen(out);
}
