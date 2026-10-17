// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coherref.h"
#include "../md5.h"
#include "../report.h"
#include "../vp9jobs.h"
#include "../vp9ref.h"
#include "tests.h"

// The nvdec suite's jobs and judge without the engine: the generated jobs fit the buffer and
// rebuild the tables tools/vp9-jobs.py wrote, each case asks the question the other form of
// its jobs answers, the command words are the ones worked out by hand from NVIDIA's class
// header and reden's register positions, a surface reads the way nvtegra's own copy walks it,
// and the verdicts on reads made to order say what they must.

static const char *const SUITE = "reference";

static const Vp9Case *Named(const char *name) {
    for (uint32_t i = 0; i < VP9_CASE_COUNT; ++i) {
        if (strcmp(VP9_CASES[i].name, name) == 0) {
            return &VP9_CASES[i];
        }
    }
    return &VP9_CASES[0];
}

static void CheckJobs(void) {
    uint32_t wrong = 0;
    char first[160] = "";
    uint8_t table[VP9_PROBS_SIZE];
    uint32_t hash = 0x811C9DC5u;
    for (uint32_t i = 0; i < VP9_CASE_COUNT; ++i) {
        const Vp9Case *const c = &VP9_CASES[i];
        const Vp9Layout l = Vp9LayoutOf(c->width, c->height);
        uint32_t shown = 0, jobs = 0;
        for (uint32_t k = 0; k < c->frame_count; ++k) {
            const Vp9Frame *const f = &c->frames[k];
            shown += f->shown;
            if (f->existing) {
                continue;
            }
            // The setup's own frame stride, at 0x64 and 0x66, is the layout's.
            const uint32_t stride = (uint32_t)(f->setup[0x64] | f->setup[0x65] << 8);
            const bool fits = f->size - f->body <= VP9_BODY_MAX && f->tiles <= VP9_TILES_MAX &&
                              f->surface < c->surfaces && stride == l.stride &&
                              (uint32_t)(f->setup[0x66] | f->setup[0x67] << 8) == l.stride &&
                              (jobs > 0u || f->probs != NULL);
            if (!fits && wrong++ == 0u) {
                snprintf(first, sizeof(first), "%s frame %u", c->name, (unsigned)k);
            }
            Vp9TableApply(f, table);
            hash = Vp9Fnv(hash, table, sizeof(table));
            ++jobs;
        }
        const bool sized = c->frame_count <= VP9_FRAMES_MAX && c->image_count == shown &&
                           c->image_count <= VP9_IMAGES_MAX && c->surfaces <= VP9_SURFACES_MAX &&
                           l.size <= VP9_SURFACE_MAX;
        if (!sized && wrong++ == 0u) {
            snprintf(first, sizeof(first), "%s's sizes", c->name);
        }
    }
    CHECK_TRUE(SUITE, "vp9: every job fits the buffer and its surface is the setup's",
               CHECK_NONE, wrong == 0u, "%u wrong; first %s", (unsigned)wrong, first);
    CHECK_TRUE(SUITE, "vp9: the patches rebuild the tables tools/vp9-jobs.py wrote", CHECK_NONE,
               hash == VP9_TABLES_FNV, "FNV 0x%08x, the script's 0x%08x", (unsigned)hash,
               (unsigned)VP9_TABLES_FNV);
    // vp90-2-03-deltaq is a 352 by 240 stream, so its surface is 384 by 256 and the chroma
    // starts 96 KiB in.
    const Vp9Layout l = Vp9LayoutOf(352u, 240u);
    const Vp9Layout small = Vp9LayoutOf(66u, 66u);
    CHECK_TRUE(SUITE, "vp9: surfaces are rows of 64 bytes, heights of 32, chroma after luma",
               CHECK_NONE,
               l.stride == 384u && l.rows == 256u && l.chroma == 0x18000u && l.size == 0x24000u &&
                   small.stride == 128u && small.rows == 96u && small.size == 0x4800u,
               "352x240: %u %u 0x%x 0x%x; 66x66: %u %u 0x%x", (unsigned)l.stride,
               (unsigned)l.rows, (unsigned)l.chroma, (unsigned)l.size, (unsigned)small.stride,
               (unsigned)small.rows, (unsigned)small.size);
}

static void CheckForms(void) {
    char asked[160] = "";
    size_t n = 0;
    for (uint32_t i = 0; i < VP9_CASE_COUNT && n < sizeof(asked); ++i) {
        const Vp9Question q = Vp9CaseQuestion(&VP9_CASES[i]);
        n += (size_t)snprintf(asked + n, sizeof(asked) - n, "%s%s",
                              i ? " " : "", q == VP9_QUESTIONS ? "-" : Vp9QuestionName(q));
    }
    // vp9_two_frames' keyframe has a chroma DC delta of 1 and an AC delta of 2;
    // vp9_show_existing has jobs after a frame shown again whose frame before was hidden.
    CHECK_TRUE(SUITE, "vp9: two cases are built both ways, the others once", CHECK_NONE,
               strcmp(asked, "Q4 - - - - Q3") == 0, "\"%s\"", asked);

    const Vp9Case *const two = Named("vp9_two_frames");
    uint8_t a[VP9_SETUP_SIZE], b[VP9_SETUP_SIZE];
    Vp9Setup(&two->frames[0], false, VP9_Q4, false, a);
    Vp9Setup(&two->frames[0], false, VP9_Q4, true, b);
    // Form A as generated, the AC delta (2) at 0x74; form B the two bytes the other way.
    const bool swapped = memcmp(a, two->frames[0].setup, VP9_SETUP_SIZE) == 0 &&
                         a[0x74] == 2u && a[0x75] == 1u && b[0x74] == 1u && b[0x75] == 2u &&
                         memcmp(b, a, 0x74) == 0 &&
                         memcmp(b + 0x76, a + 0x76, VP9_SETUP_SIZE - 0x76) == 0;
    const Vp9Case *const existing = Named("vp9_show_existing");
    uint32_t set = 0, other = 0;
    bool after = false;
    for (uint32_t k = 0; k < existing->frame_count; ++k) {
        const Vp9Frame *const f = &existing->frames[k];
        if (f->existing) {
            after = true;
            continue;
        }
        Vp9Setup(f, after, VP9_Q3, true, b);
        for (uint32_t i = 0; i < VP9_SETUP_SIZE; ++i) {
            if (b[i] != f->setup[i]) {
                set += i == 0x68u && (b[i] ^ f->setup[i]) == 0x10u;
                other += i != 0x68u || (b[i] ^ f->setup[i]) != 0x10u;
            }
        }
        after = false;
    }
    CHECK_TRUE(SUITE, "vp9: the B forms change only LastShowFrame, or the chroma deltas",
               CHECK_NONE, swapped && set > 0u && other == 0u,
               "Q4 swap %s; Q3 sets LastShowFrame on %u jobs, %u other bytes",
               swapped ? "right" : "wrong", (unsigned)set, (unsigned)other);
}

static void CheckCommands(void) {
    // A job of a 352 by 240 case at a buffer mapped at 0x100000, the surfaces of slots 1, 2 and 3
    // read and 0 written, worked out by hand: each method's offset from NVIDIA's clc5b0.h over
    // four (reden's NvdecRegisters puts each at the same index), each address the buffer's
    // plus the layout's offset, shifted right by 8.
    static const uint32_t WANT[] = {
        0x00003c00u,                                // SETCLASS 0xF0
        0x10100002u, 0x080u, 9u,                    // SET_APPLICATION_ID, VP9
        0x10100002u, 0x100u, 0x59u,                 // SET_CONTROL_PARAMS
        0x10100002u, 0x103u, 0u,                    // SET_PICTURE_INDEX
        0x10100002u, 0x101u, 0x1000u,               // the setup at 0x0000
        0x10100002u, 0x102u, 0x1058u,               // the bitstream at 0x5800
        0x10100002u, 0x109u, 0x1001u,               // the status at 0x0100
        0x10100002u, 0x170u, 0x1006u,               // the table at 0x0600
        0x10100002u, 0x171u, 0x101cu,               // the counters at 0x1C00
        0x10100002u, 0x174u, 0x1015u,               // the tile sizes at 0x1500
        0x10100002u, 0x175u, 0x1118u,               // motion vectors written at 0x11800
        0x10100002u, 0x176u, 0x1190u,               // and read at 0x19000
        0x10100002u, 0x172u, 0x1050u,               // segment map read at 0x5000
        0x10100002u, 0x173u, 0x1054u,               // and written at 0x5400
        0x10100002u, 0x177u, 0x1208u,               // the filter buffer at 0x20800
        0x10100002u, 0x10cu, 0x1920u, 0x10100002u, 0x11du, 0x1aa0u, // last, surface 1
        0x10100002u, 0x10du, 0x1b60u, 0x10100002u, 0x11eu, 0x1ce0u, // golden, surface 2
        0x10100002u, 0x10eu, 0x1da0u, 0x10100002u, 0x11fu, 0x1f20u, // altref, surface 3
        0x10100002u, 0x10fu, 0x16e0u, 0x10100002u, 0x120u, 0x1860u, // current, surface 0
        0x10100002u, 0x0c0u, 0x100u,                // Execute, AWAKEN
        0x20000001u, 0x121u,                        // INCR_SYNCPT 33 on OP_DONE
    };
    const Vp9Layout l = Vp9LayoutOf(352u, 240u);
    Vp9JobAt at = {.iova = 0x100000u, .surface_size = l.size, .chroma = l.chroma, .index = 0u,
                   .segment_read = 0u, .surfaces = {1u, 2u, 3u, 0u}};
    uint32_t words[VP9_COMMAND_WORDS];
    const size_t count = Vp9Commands(words, VP9_COMMAND_WORDS, &at, 33u);
    const size_t want = sizeof(WANT) / sizeof(WANT[0]);
    size_t first = want;
    for (size_t i = 0; i < want && i < count; ++i) {
        if (words[i] != WANT[i]) {
            first = i;
            break;
        }
    }
    CHECK_TRUE(SUITE, "vp9: a job's command words, word by word", CHECK_NONE,
               count == want && first == want, "%u words, %u expected; first difference at %u: 0x%x",
               (unsigned)count, (unsigned)want, (unsigned)first,
               first < count ? (unsigned)words[first] : 0u);
    // The next job swaps both pairs: it reads the motion vectors and the segment map the first
    // wrote.
    at.index = 1u;
    at.segment_read = 1u;
    const size_t again = Vp9Commands(words, VP9_COMMAND_WORDS, &at, 33u);
    CHECK_TRUE(SUITE, "vp9: the next job reads what the one before wrote", CHECK_NONE,
               again == want && words[9] == 1u && words[30] == 0x1190u && words[33] == 0x1118u &&
                   words[36] == 0x1054u && words[39] == 0x1050u,
               "index %u, motion vectors 0x%x 0x%x, segments 0x%x 0x%x", (unsigned)words[9],
               (unsigned)words[30], (unsigned)words[33], (unsigned)words[36],
               (unsigned)words[39]);
    CHECK_TRUE(SUITE, "vp9: command words that do not fit are none", CHECK_NONE,
               Vp9Commands(words, 20u, &at, 33u) == 0u, "the words claimed to fit in 20");
}

// nvtegra's copy (hwcontext_nvtegra.c, nvtegra_cpu_copy_plane), restated: it walks a block
// linear plane in 16-byte runs two rows at a time, stepping its offsets through the address
// bits of x and y with two masks, and a block row of 16 rows on when the y bits wrap.
static uint32_t walked[64][24];

static void NvtegraWalk(uint32_t stride, uint32_t rows) {
    const uint32_t x_mask = (uint32_t)-0x2e, y_mask = 0x2cu, runs = stride / 16u;
    uint32_t offs_x = 0, offs_y = 0;
    for (uint32_t y = 0; y < rows; y += 2u) {
        uint32_t offs_line = offs_x;
        for (uint32_t x = 0; x < runs; ++x) {
            walked[y][x] = (offs_y + offs_line) * 16u;
            walked[y + 1u][x] = (offs_y + offs_line + 1u) * 16u;
            offs_line = (offs_line - x_mask) & x_mask;
        }
        offs_y = (offs_y - y_mask) & y_mask;
        if (offs_y == 0u) {
            offs_x += stride;
        }
    }
}

static uint8_t surface[0x4800];
static uint8_t planes[66 * 66 + 2 * 33 * 33];

static void CheckSurface(void) {
    NvtegraWalk(384u, 64u);
    uint32_t wrong = 0;
    for (uint32_t y = 0; y < 64u; ++y) {
        for (uint32_t x = 0; x < 24u; ++x) {
            wrong += walked[y][x] != CohBlockLinearOffset(x * 16u, y, 384u, 2u);
        }
    }
    CHECK_TRUE(SUITE, "vp9: surfaces are read as nvtegra's copy walks them", CHECK_NONE,
               wrong == 0u, "%u of 1536 runs elsewhere; (1, 1) at %u, the walk %u",
               (unsigned)wrong, (unsigned)CohBlockLinearOffset(16u, 1u, 384u, 2u),
               (unsigned)walked[1][1]);

    // A 66 by 66 picture, odd chroma included, written into its surface the way the engine
    // lays one out and read back, against MD5s of its planes taken straight.
    const Vp9Layout l = Vp9LayoutOf(66u, 66u);
    memset(surface, VP9_FILL, sizeof(surface));
    uint8_t *const yp = planes, *const up = planes + 66 * 66, *const vp = up + 33 * 33;
    for (uint32_t y = 0; y < 66u; ++y) {
        for (uint32_t x = 0; x < 66u; ++x) {
            yp[y * 66u + x] = (uint8_t)(x * 7u + y * 13u);
            surface[CohBlockLinearOffset(x, y, l.stride, 2u)] = yp[y * 66u + x];
        }
    }
    for (uint32_t y = 0; y < 33u; ++y) {
        for (uint32_t x = 0; x < 33u; ++x) {
            up[y * 33u + x] = (uint8_t)(x * 3u + y * 5u + 17u);
            vp[y * 33u + x] = (uint8_t)(x * 11u + y + 99u);
            surface[l.chroma + CohBlockLinearOffset(2u * x, y, l.stride, 2u)] = up[y * 33u + x];
            surface[l.chroma + CohBlockLinearOffset(2u * x + 1u, y, l.stride, 2u)] =
                vp[y * 33u + x];
        }
    }
    uint8_t want[4][16];
    const uint8_t *const starts[3] = {yp, up, vp};
    const unsigned long sizes[3] = {66u * 66u, 33u * 33u, 33u * 33u};
    MD5_CTX whole, one;
    MD5_Init(&whole);
    for (uint32_t p = 0; p < 3u; ++p) {
        MD5_Init(&one);
        MD5_Update(&one, starts[p], sizes[p]);
        MD5_Final(want[p + 1u], &one);
        MD5_Update(&whole, starts[p], sizes[p]);
    }
    MD5_Final(want[0], &whole);
    Vp9Image image;
    Vp9ReadImage(surface, &l, 66u, 66u, &image);
    const bool right = image.read && !image.blank && memcmp(image.md5, want, sizeof(want)) == 0;
    memset(surface, VP9_FILL, sizeof(surface));
    Vp9ReadImage(surface, &l, 66u, 66u, &image);
    CHECK_TRUE(SUITE, "vp9: a picture written block linear reads back as its planes' MD5s",
               CHECK_NONE, right && image.blank, "picture %s, fill blank %d",
               right ? "right" : "wrong", image.blank);
}

static Vp9Pass pass[2];

/// A pass where frame i's image is right when @p right says so (FFmpeg's MD5s), nothing was
/// written otherwise, and nothing is read again.
static void Made(const Vp9Case *c, Vp9Pass *p, bool (*right)(uint32_t frame)) {
    memset(p, 0, sizeof(*p));
    p->question = VP9_QUESTIONS;
    p->frames_run = c->frame_count;
    for (uint32_t i = 0; i < c->frame_count; ++i) {
        p->now[i].read = true;
        if (right(i)) {
            memcpy(p->now[i].md5, c->frames[i].md5, sizeof(p->now[i].md5));
        } else {
            p->now[i].blank = true;
        }
    }
}

static bool All(uint32_t frame) {
    return true;
}

static bool None(uint32_t frame) {
    return false;
}

static bool NotFive(uint32_t frame) {
    return frame != 5u;
}

// vp9_show_existing's last frame is shown again, so it is an image.
static bool NotLast(uint32_t frame) {
    return frame + 1u != VP9_CASES[5].frame_count;
}

static void CheckJudge(void) {
    // vp9_hidden_altref: 11 frames, frame 1 hidden, 10 images.
    const Vp9Case *const altref = Named("vp9_hidden_altref");
    char detail[480];
    uint32_t fits = 0;
    Made(altref, &pass[0], All);
    const bool all = Vp9Detail(altref, pass, 1u, detail, sizeof(detail), &fits) &&
                     strcmp(detail, "10 of 10 images right at their fence; hidden frames written "
                                    "1 of 1 (fits Q1 A, Q2 A)") == 0;
    char all_said[480];
    memcpy(all_said, detail, sizeof(detail));

    Made(altref, &pass[0], None);
    const bool nothing = !Vp9Detail(altref, pass, 1u, detail, sizeof(detail), &fits) &&
                         strstr(detail, "0 of 10 images right at their fence; first wrong image 1 "
                                        "(frame 0): nothing was written there") != NULL;
    char nothing_said[480];
    memcpy(nothing_said, detail, sizeof(detail));

    // Every image only there a job later: Q1 B. The last job's image has no later read.
    Made(altref, &pass[0], None);
    for (uint32_t i = 0; i + 1u < altref->frame_count; ++i) {
        pass[0].later[i].read = true;
        memcpy(pass[0].later[i].md5, altref->frames[i].md5, 16u);
    }
    const bool late = Vp9Detail(altref, pass, 1u, detail, sizeof(detail), &fits) &&
                      strstr(detail, "0 of 10 images right at their fence, 9 of 9 a job later") !=
                          NULL &&
                      strstr(detail, "(fits Q1 B, Q2 B)") != NULL;
    char late_said[480];
    memcpy(late_said, detail, sizeof(detail));

    // Frame 5's luma right and its chroma not.
    Made(altref, &pass[0], NotFive);
    pass[0].now[5].blank = false;
    memcpy(pass[0].now[5].md5[1], altref->frames[5].md5[1], 16u);
    pass[0].error_status[5] = 3u;
    const bool chroma = !Vp9Detail(altref, pass, 1u, detail, sizeof(detail), &fits) &&
                        strstr(detail, "9 of 10 images right at their fence; first wrong image 5 "
                                       "(frame 5): Y right, U wrong, V wrong; the engine said "
                                       "error 0x3, 0 macroblocks in error") != NULL;
    char chroma_said[480];
    memcpy(chroma_said, detail, sizeof(detail));

    // The job for frame 3 never finished.
    Made(altref, &pass[0], All);
    pass[0].frames_run = 3u;
    snprintf(pass[0].failure, sizeof(pass[0].failure), "job 3 of 11 (frame 3): did not finish");
    const bool stopped = !Vp9Detail(altref, pass, 1u, detail, sizeof(detail), &fits) &&
                         strstr(detail, "2 of 10 images right at their fence; first wrong image 3 "
                                        "(frame 3): never read, job 3 of 11 (frame 3): did not "
                                        "finish") != NULL;
    char stopped_said[480];
    memcpy(stopped_said, detail, sizeof(detail));

    CHECK_TRUE(SUITE, "vp9: a case's line counts its images and names the first wrong one",
               CHECK_NONE, all && nothing && late && chroma && stopped,
               "all \"%.70s\"; nothing \"%.70s\"; late \"%.70s\"; chroma \"%.70s\"; "
               "stopped \"%.70s\"",
               all_said, nothing_said, late_said, chroma_said, stopped_said);

    // Built both ways: the A form decodes, the B form goes wrong from some image on.
    const Vp9Case *const existing = Named("vp9_show_existing");
    Made(existing, &pass[0], All);
    pass[0].question = VP9_Q3;
    Made(existing, &pass[1], NotLast);
    pass[1].question = VP9_Q3;
    pass[1].b = true;
    const bool both = Vp9Detail(existing, pass, 2u, detail, sizeof(detail), &fits);
    char forms_said[480];
    memcpy(forms_said, detail, sizeof(detail));
    uint8_t settled[VP9_QUESTIONS] = {0};
    char said[160];
    Vp9Settle(fits, settled);
    Vp9Settled(settled, said, sizeof(said));
    const bool forms = both && strncmp(forms_said, "form A: 13 of 13 images right", 29) == 0 &&
                       strstr(forms_said, "; form B: 12 of 13 images right") != NULL &&
                       strstr(forms_said, "(fits Q1 A, Q2 A, Q3 A)") != NULL &&
                       strcmp(said, "Q1 A, Q2 A, Q3 A") == 0;
    // Another case that only fits Q1 B: the note says the cases disagree.
    Made(altref, &pass[0], None);
    for (uint32_t i = 0; i + 1u < altref->frame_count; ++i) {
        pass[0].later[i].read = true;
        memcpy(pass[0].later[i].md5, altref->frames[i].md5, 16u);
    }
    Vp9Detail(altref, pass, 1u, detail, sizeof(detail), &fits);
    Vp9Settle(fits, settled);
    Vp9Settled(settled, said, sizeof(said));
    CHECK_TRUE(SUITE, "vp9: two forms say which decodes, and the note adds the cases up",
               CHECK_NONE, forms && strcmp(said, "Q1 A and B, cases disagree, Q2 A and B, "
                                                 "cases disagree, Q3 A") == 0,
               "forms %d \"%.200s\"; \"%s\"", forms, forms_said, said);
}

void RunReferenceVp9(void) {
    CheckJobs();
    CheckForms();
    CheckCommands();
    CheckSurface();
    CheckJudge();
}
