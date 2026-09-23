// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coherref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// The caches kept between draws, submits and copies, each asked the one question it can get
// wrong: does the next reader see the last write, or a copy of what was there before.
//
// Every other suite draws once into memory nobody has read, so an emulator's texture, buffer and
// render target caches never have an old answer to give. Here every case first makes the reader
// hold the old contents (a draw samples the texture, a readback downloads the image, a draw reads
// the uniform), then writes new contents by one path and reads them by another, in the same
// list wherever a game would. Region one of the 64 by 64 target shows the old contents and
// region two the new ones; the rest stays the sentinel, and every pixel is judged.
//
// The synchronisation is what deko3d.h documents for each hand over, and nothing more:
//   DkBarrier_Fragments "orders the processing of fragments (similar to Vulkan renderpasses)",
//     between a draw that samples an image and a draw that renders it, either way round;
//   DkBarrier_Full "completes the processing of all previous commands", around the copy engine,
//     which the 3D pipeline's own barriers do not order;
//   DkInvalidateFlags_Image "invalidates the image (texture) cache", before sampling anything
//     written since it was last sampled;
//   DkInvalidateFlags_Shader "invalidates the shader code/data/uniform cache", before a draw
//     reads a uniform buffer the processor rewrote.
// dkQueueWaitIdle only where the processor touches the memory, which is the one place a game
// waits too. A wait anywhere else would drain the machine and hide exactly what is tested. The
// library also drops these caches on every queue flush (Queue::postSubmitFlush); the explicit
// invalidation stays, since it is what the pattern asks of a program and what an emulator acts on.
//
// Sampling is nearest, one texel of a 32 by 32 texture per pixel, so every pixel centre is a
// texel centre, half a texel from any edge. Drawn contents are quads on whole pixels, the
// geometry the raster suite already proved, filled by pattern.frag.glsl with four bytes unique
// to each fragment, so a shift, a transposition or a stale texel anywhere is a wrong byte. The
// expectations are source/coherref.c's.

static const char *const SUITE = "coherence";

#define TARGET_SIZE 64u
#define TEX 32u
#define ALL_CELLS 0xFFFFu
// The four cells in the middle of the 4 by 4 grid of cells, for the partial redraws.
#define MIDDLE_CELLS ((1u << 5) | (1u << 6) | (1u << 9) | (1u << 10))
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

static const CohRect FIRST = { 0, 16, 32, 32 };
static const CohRect SECOND = { 32, 16, 32, 32 };

// Which image descriptor each sampled texture sits at.
enum {
    SLOT_RENDERED,
    SLOT_PITCH,
    SLOT_BLOCK,
    SLOT_ALIAS,
    SLOT_COPIED,
    SLOT_CACHED,
    SLOT_COUNT,
};

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader pattern;
    DkShader sample;
    DkShader uniform;
    DkShader alias;
    DkGpuAddr image_set;
    DkGpuAddr sampler_set;
} Suite;

// The model's images. Static, since each is 16 KiB and a case needs several at once.
static CohImage expected;
static CohImage stale;
static CohImage scratch;

/// Judges every pixel against @p want. @p olds are the pictures had a read seen contents from
/// before a write, @p old_count of them; a wrong pixel matching any is counted apart, since that
/// is the fault this suite hunts and the detail should say so rather than leave it to be
/// inferred.
static void JudgeStale(const char *name, const uint8_t *pixels, const CohImage *want,
                       const CohImage *const *olds, size_t old_count) {
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, "the readback handed back nothing");
        return;
    }
    const uint32_t width = want->width;
    const uint32_t height = want->height;
    Picture *const picture = PictureBegin(width, height, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t stale_count = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < width * height; ++i) {
        const uint8_t *const pixel = pixels + (size_t)i * 4u;
        const bool right = memcmp(pixel, want->texels[i], 4) == 0;
        PictureSet(picture, i % width, i / width, want->texels[i],
                   right ? PICTURE_MATCHED : PICTURE_WRONG);
        if (right) {
            continue;
        }
        if (wrong == 0) {
            first = i;
        }
        ++wrong;
        for (size_t k = 0; k < old_count; ++k) {
            if (memcmp(pixel, olds[k]->texels[i], 4) == 0) {
                ++stale_count;
                break;
            }
        }
    }
    const uint8_t *const got = pixels + (size_t)first * 4u;
    const uint8_t *const w = want->texels[first];
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong, %u of them holding the contents from before the write; "
               "first at (%u,%u) is %02x%02x%02x%02x, expected %02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)(width * height), (unsigned)stale_count,
               (unsigned)(first % width), (unsigned)(first / width), got[0], got[1], got[2],
               got[3], w[0], w[1], w[2], w[3]);
}

/// JudgeStale with one picture of the old contents, or none when @p old is NULL.
static void Judge(const char *name, const uint8_t *pixels, const CohImage *want,
                  const CohImage *old) {
    JudgeStale(name, pixels, want, &old, old != NULL ? 1u : 0u);
}

/// The target a two region case expects, and the one it would hold had region two seen
/// @p before: region one always samples @p before.
static void ExpectTwoRegions(const CohImage *before, const CohImage *after, CohMove how) {
    CohImageInit(&expected, TARGET_SIZE, TARGET_SIZE);
    CohFill(&expected, COH_SENTINEL);
    CohSampleRegion(&expected, &FIRST, before, how);
    stale = expected;
    CohSampleRegion(&expected, &SECOND, after, how);
    CohSampleRegion(&stale, &SECOND, before, how);
}

static GpuVertex Corner(float x, float y, float u, float v, const uint8_t colour[4]) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = 0.0f;
    vertex.u = u;
    vertex.v = v;
    vertex.r = (float)colour[0] / 255.0f;
    vertex.g = (float)colour[1] / 255.0f;
    vertex.b = (float)colour[2] / 255.0f;
    vertex.a = (float)colour[3] / 255.0f;
    return vertex;
}

/// Two triangles over the pixel rectangle @p x0..x1, @p y0..y1 of a @p size square target.
/// Clip space plus one is the top row, so y flips on the way.
static void Quad(GpuVertex out[6], uint32_t size, uint32_t x0, uint32_t y0, uint32_t x1,
                 uint32_t y1, const uint8_t colour[4]) {
    const float left = 2.0f * (float)x0 / (float)size - 1.0f;
    const float right = 2.0f * (float)x1 / (float)size - 1.0f;
    const float top = 1.0f - 2.0f * (float)y0 / (float)size;
    const float bottom = 1.0f - 2.0f * (float)y1 / (float)size;
    out[0] = Corner(left, top, 0.0f, 0.0f, colour);
    out[1] = Corner(right, top, 1.0f, 0.0f, colour);
    out[2] = Corner(right, bottom, 1.0f, 1.0f, colour);
    out[3] = Corner(left, top, 0.0f, 0.0f, colour);
    out[4] = Corner(right, bottom, 1.0f, 1.0f, colour);
    out[5] = Corner(left, bottom, 0.0f, 1.0f, colour);
}

/// Records one draw of the cells in @p mask onto whatever target is bound, @p size square,
/// which is what CohPaintCells models. The seed rides in the vertex red, as seed / 255.
static bool PaintCells(Suite *s, uint32_t seed, uint32_t mask, uint32_t size) {
    GpuVertex vertices[6u * 16u];
    uint32_t count = 0;
    const uint32_t cell_size = size / 4u;
    const uint8_t carrier[4] = { (uint8_t)seed, 0, 0, 0 };
    for (uint32_t cell = 0; cell < 16u; ++cell) {
        if ((mask >> cell & 1u) == 0u) {
            continue;
        }
        const uint32_t x0 = (cell % 4u) * cell_size;
        const uint32_t y0 = (cell / 4u) * cell_size;
        Quad(&vertices[count], size, x0, y0, x0 + cell_size, y0 + cell_size, carrier);
        count += 6u;
    }
    return GpuDrawTriangles(&s->gpu, &s->vertex, &s->pattern, vertices, count);
}

/// Records a draw of @p region on the main target sampling descriptor @p slot through
/// @p shader. The descriptor sets are bound again each time, which costs nothing and keeps each
/// draw independent of what the list bound before it.
static bool SampleRegion(Suite *s, const CohRect *region, uint32_t slot, const DkShader *shader) {
    dkCmdBufBindImageDescriptorSet(s->gpu.cmdbuf, s->image_set, SLOT_COUNT);
    dkCmdBufBindSamplerDescriptorSet(s->gpu.cmdbuf, s->sampler_set, 1);
    dkCmdBufBindTexture(s->gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(slot, 0));
    GpuVertex vertices[6];
    Quad(vertices, TARGET_SIZE, region->x, region->y, region->x + region->width,
         region->y + region->height, COH_SENTINEL);
    return GpuDrawTriangles(&s->gpu, &s->vertex, shader, vertices, 6);
}

/// Reports a case whose list could not be recorded, so that it still has its line.
static bool Recorded(Suite *s, const char *name, bool recorded) {
    if (!recorded || !s->gpu.ready) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   s->gpu.failure[0] != '\0' ? s->gpu.failure : "a draw was not recorded");
        return false;
    }
    return true;
}

static void ClearMain(Suite *s) {
    GpuClear(&s->gpu, 1.0f, 0.0f, 1.0f, 0.0f);
}

/// Draws into an offscreen target, then samples it, in one list. Region one samples the
/// texture before the draw so the texture cache holds @p texture as it was; region two after.
/// @p texture follows the draw.
static void RenderThenSample(Suite *s, const char *name, const DkImage *target, uint32_t slot,
                             const DkShader *shader, CohMove how, uint32_t seed, uint32_t mask,
                             CohImage *texture) {
    scratch = *texture;
    CohPaintCells(&scratch, seed, mask);

    ClearMain(s);
    GpuBeginList(&s->gpu);
    bool recorded = SampleRegion(s, &FIRST, slot, shader);
    // The first draw's fragments read the texture the next draw renders into: they have to be
    // finished before it starts writing.
    dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_Fragments, 0);
    GpuSetTarget(&s->gpu, target, TEX, TEX);
    recorded = recorded && PaintCells(s, seed, mask, TEX);
    // The written fragments have to land before anything samples them, and the texture cache
    // still holds what the first draw read.
    dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
    GpuSetTarget(&s->gpu, NULL, 0, 0);
    recorded = recorded && SampleRegion(s, &SECOND, slot, shader);
    GpuSubmit(&s->gpu);
    if (Recorded(s, name, recorded)) {
        ExpectTwoRegions(texture, &scratch, how);
        Judge(name, GpuReadBack(&s->gpu), &expected, &stale);
    }
    *texture = scratch;
}

/// Where texel @p x, @p y of a texture the processor writes sits, for the layouts used.
typedef uint32_t (*TexelOffset)(uint32_t x, uint32_t y);

/// Both pitch linear textures have rows of exactly TEX texels: 128 bytes is already a multiple
/// of the linear stride alignment, which RunCoherence checks for the cached one.
static uint32_t PitchOffset(uint32_t x, uint32_t y) {
    return y * TEX * 4u + x * 4u;
}

static uint32_t BlockOffset(uint32_t x, uint32_t y) {
    // One gob per block: the layout was made with DkTileSize_OneGob so this can be named. Two
    // gobs across, so the rule's step from one gob to the next along a row is exercised too.
    return CohBlockLinearOffset(x * 4u, y, TEX * 4u, 1u);
}

/// Stores @p rows rows of @p image from @p first_row through the processor's pointer.
static void CpuWrite(uint8_t *bytes, TexelOffset where, const CohImage *image,
                     uint32_t first_row, uint32_t rows) {
    for (uint32_t y = first_row; y < first_row + rows; ++y) {
        for (uint32_t x = 0; x < image->width; ++x) {
            memcpy(bytes + where(x, y), image->texels[y * image->width + x], 4);
        }
    }
}

/// Samples a texture, has the processor rewrite @p rows rows from @p first_row with pattern
/// @p seed, and samples it again, in two lists. @p cached says the pointer goes through the
/// processor's data cache. @p texture follows the write.
static void SampleThenProcessorWrites(Suite *s, const char *name, uint8_t *bytes,
                                      TexelOffset where, bool cached, uint32_t slot,
                                      CohImage *texture, uint32_t seed, uint32_t first_row,
                                      uint32_t rows) {
    CohImage *const written = &scratch;
    CohImageInit(written, TEX, TEX);
    CohFillPattern(written, seed);

    ClearMain(s);
    GpuBeginList(&s->gpu);
    bool recorded = SampleRegion(s, &FIRST, slot, &s->sample);
    // GpuSubmit waits for the queue: the processor is about to write memory this draw reads.
    GpuSubmit(&s->gpu);

    CpuWrite(bytes, where, written, first_row, rows);
    if (cached) {
        // Through the data cache: until flushed the rows may be nowhere the graphics unit can
        // read. The pitch linear rows are contiguous, so one range covers them.
        GpuFlushCachedImage(&s->gpu, bytes + where(0, first_row), rows * TEX * 4u);
    }
    // Uncached, the stores are in memory as they retire and no flush is owed.

    GpuBeginList(&s->gpu);
    // The texture cache may still hold the texels the first draw read. Nothing is left running
    // to order against, so the barrier itself is none.
    dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_None, DkInvalidateFlags_Image);
    recorded = recorded && SampleRegion(s, &SECOND, slot, &s->sample);
    GpuSubmit(&s->gpu);

    // The rewritten rows are the new pattern, every other row keeps the old one.
    static CohImage after;
    after = *texture;
    CohMoveRect(&after, 0, first_row, written, 0, first_row, TEX, rows, COH_MOVE_BYTES);
    if (Recorded(s, name, recorded)) {
        ExpectTwoRegions(texture, &after, COH_MOVE_BYTES);
        Judge(name, GpuReadBack(&s->gpu), &expected, &stale);
    }
    *texture = after;
}

static void UniformColour(float out[4], const uint8_t colour[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = (float)colour[i] / 255.0f;
    }
}

static bool UniformDraw(Suite *s, const CohRect *region, DkGpuAddr buffer) {
    dkCmdBufBindUniformBuffer(s->gpu.cmdbuf, DkStage_Fragment, 0, buffer, UNIFORM_SIZE);
    GpuVertex vertices[6];
    Quad(vertices, TARGET_SIZE, region->x, region->y, region->x + region->width,
         region->y + region->height, COH_SENTINEL);
    return GpuDrawTriangles(&s->gpu, &s->vertex, &s->uniform, vertices, 6);
}

static void JudgeTwoColours(const char *name, const uint8_t first[4], const uint8_t second[4],
                            const uint8_t *pixels) {
    CohImageInit(&expected, TARGET_SIZE, TARGET_SIZE);
    CohFill(&expected, COH_SENTINEL);
    CohFillRect(&expected, FIRST.x, FIRST.y, FIRST.width, FIRST.height, first);
    stale = expected;
    CohFillRect(&expected, SECOND.x, SECOND.y, SECOND.width, SECOND.height, second);
    CohFillRect(&stale, SECOND.x, SECOND.y, SECOND.width, SECOND.height, first);
    Judge(name, pixels, &expected, &stale);
}

/// The processor rewrites a uniform buffer at the same address between two submits.
static void UniformBetweenSubmits(Suite *s) {
    static const char *const NAME =
        "a uniform buffer the processor rewrites between submits reaches the next draw";
    static const uint8_t ONE[4] = { 0x21, 0x43, 0x65, 0x87 };
    static const uint8_t TWO[4] = { 0xA9, 0xCB, 0xED, 0x0F };
    DkGpuAddr buffer = 0;
    float *const colour =
        (float *)GpuAllocData(&s->gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &buffer);
    if (colour == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the uniform buffer");
        return;
    }
    UniformColour(colour, ONE);

    ClearMain(s);
    GpuBeginList(&s->gpu);
    bool recorded = UniformDraw(s, &FIRST, buffer);
    // Waits: the processor writes the buffer this draw reads, at the same address, next.
    GpuSubmit(&s->gpu);

    // Uncached to the processor, like the texture block: in memory once stored.
    UniformColour(colour, TWO);

    GpuBeginList(&s->gpu);
    // The uniform cache may still hold the first colour.
    dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_None, DkInvalidateFlags_Shader);
    recorded = recorded && UniformDraw(s, &SECOND, buffer);
    GpuSubmit(&s->gpu);
    if (!Recorded(s, NAME, recorded)) {
        return;
    }
    JudgeTwoColours(NAME, ONE, TWO, GpuReadBack(&s->gpu));
}

/// Four submits in a row, each after the processor rewrote one word of the same buffer: green,
/// the second of the four. A cache that reloads a buffer only when all of it changed, or that
/// tracks writes too coarsely, draws some quadrant with the green before its own.
static void UniformOneWordFourSubmits(Suite *s) {
    static const char *const NAME =
        "four submits each see the one word of a uniform buffer rewritten before them";
    static const uint8_t BASE[4] = { 0x40, 0x00, 0xA0, 0xFF };
    static const uint8_t GREENS[4] = { 0x11, 0x55, 0x99, 0xDD };
    // In reading order, as CohQuadrantColours lays them out.
    static const CohRect QUADRANTS[4] = {
        { 0, 0, 32, 32 }, { 32, 0, 32, 32 }, { 0, 32, 32, 32 }, { 32, 32, 32, 32 }
    };
    DkGpuAddr buffer = 0;
    float *const colour =
        (float *)GpuAllocData(&s->gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &buffer);
    if (colour == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the uniform buffer");
        return;
    }
    uint8_t first[4];
    memcpy(first, BASE, 4);
    first[1] = GREENS[0];
    UniformColour(colour, first);

    ClearMain(s);
    GpuBeginList(&s->gpu);
    bool recorded = UniformDraw(s, &QUADRANTS[0], buffer);
    for (uint32_t q = 1; q < 4u && recorded; ++q) {
        // Waits: the processor rewrites the word the draw before reads, at the same address.
        GpuSubmit(&s->gpu);
        colour[1] = (float)GREENS[q] / 255.0f;
        GpuBeginList(&s->gpu);
        // The uniform cache may still hold the green before.
        dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_None, DkInvalidateFlags_Shader);
        recorded = UniformDraw(s, &QUADRANTS[q], buffer);
    }
    GpuSubmit(&s->gpu);
    if (!Recorded(s, NAME, recorded)) {
        return;
    }
    // A quadrant is stale when it holds any earlier green, one, two or three rewrites behind.
    static CohImage behind[3];
    const CohImage *olds[3];
    CohImageInit(&expected, TARGET_SIZE, TARGET_SIZE);
    CohQuadrantColours(&expected, BASE, 1, GREENS, 0);
    for (uint32_t lag = 1; lag <= 3u; ++lag) {
        CohImageInit(&behind[lag - 1u], TARGET_SIZE, TARGET_SIZE);
        CohQuadrantColours(&behind[lag - 1u], BASE, 1, GREENS, lag);
        olds[lag - 1u] = &behind[lag - 1u];
    }
    JudgeStale(NAME, GpuReadBack(&s->gpu), &expected, olds, 3);
}

/// The list itself rewrites a uniform buffer between two draws.
static void UniformPushedBetweenDraws(Suite *s) {
    static const char *const NAME =
        "a uniform buffer pushed between two draws of one list reaches the second only";
    static const uint8_t ONE[4] = { 0x3C, 0x5A, 0x78, 0x96 };
    static const uint8_t TWO[4] = { 0xC3, 0xA5, 0x87, 0x69 };
    DkGpuAddr buffer = 0;
    if (GpuAllocData(&s->gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &buffer) == NULL) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the uniform buffer");
        return;
    }
    float first[4];
    float second[4];
    UniformColour(first, ONE);
    UniformColour(second, TWO);

    ClearMain(s);
    GpuBeginList(&s->gpu);
    // No barrier and no invalidation: a pushed update goes through the 3D engine's own constant
    // buffer load, in order with the draws around it. That ordering is what the call is for.
    dkCmdBufPushConstants(s->gpu.cmdbuf, buffer, UNIFORM_SIZE, 0, sizeof(first), first);
    bool recorded = UniformDraw(s, &FIRST, buffer);
    dkCmdBufPushConstants(s->gpu.cmdbuf, buffer, UNIFORM_SIZE, 0, sizeof(second), second);
    recorded = recorded && UniformDraw(s, &SECOND, buffer);
    GpuSubmit(&s->gpu);
    if (!Recorded(s, NAME, recorded)) {
        return;
    }
    JudgeTwoColours(NAME, ONE, TWO, GpuReadBack(&s->gpu));
}

/// Samples a texture, overwrites part of it with the copy engine, samples it again, one list.
/// @p staged is a buffer copy source holding @p source's texels when not NULL, else
/// @p source_image is copied from. @p texture follows the copy.
static void CopyThenSample(Suite *s, const char *name, const DkImage *image, CohImage *texture,
                           const DkCopyBuf *staged, const DkImage *source_image,
                           const CohImage *source, const DkImageRect *from,
                           const DkImageRect *to) {
    scratch = *texture;
    CohMoveRect(&scratch, to->x, to->y, source, from->x, from->y, to->width, to->height,
                COH_MOVE_BYTES);

    DkImageView destination;
    dkImageViewDefaults(&destination, image);

    ClearMain(s);
    GpuBeginList(&s->gpu);
    bool recorded = SampleRegion(s, &FIRST, SLOT_COPIED, &s->sample);
    // The copy engine is not ordered by the 3D pipeline's barriers, so only a full one keeps it
    // from overwriting texels the draw above has yet to read.
    dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_Full, 0);
    if (staged != NULL) {
        dkCmdBufCopyBufferToImage(s->gpu.cmdbuf, staged, &destination, to, 0);
    } else {
        DkImageView view;
        dkImageViewDefaults(&view, source_image);
        dkCmdBufCopyImage(s->gpu.cmdbuf, &view, from, &destination, to, 0);
    }
    // The copy has to finish before the sampler reads, and the texture cache holds what the
    // first draw read.
    dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
    recorded = recorded && SampleRegion(s, &SECOND, SLOT_COPIED, &s->sample);
    GpuSubmit(&s->gpu);
    if (Recorded(s, name, recorded)) {
        ExpectTwoRegions(texture, &scratch, COH_MOVE_BYTES);
        Judge(name, GpuReadBack(&s->gpu), &expected, &stale);
    }
    *texture = scratch;
}

/// Renders into @p rendered, copies @p from of it to @p to of @p copy, and reads the copy back
/// by the ordinary readback. @p held is what @p copy holds before, and after on return;
/// @p drawn is what @p rendered holds before, and after on return.
///
/// Two pictures count as stale: the copy untouched, and the copy made from the render target's
/// previous contents, which is what a render target not flushed before the copy engine reads it
/// leaves.
static void RenderCopyReadBack(Suite *s, const char *name, const DkImage *rendered,
                               const DkImage *copy, uint32_t seed, const DkImageRect *from,
                               const DkImageRect *to, CohImage *held, CohImage *drawn) {
    CohImageInit(&scratch, TARGET_SIZE, TARGET_SIZE);
    CohPaintCells(&scratch, seed, ALL_CELLS);
    expected = *held;
    CohMoveRect(&expected, to->x, to->y, &scratch, from->x, from->y, to->width, to->height,
                COH_MOVE_BYTES);
    stale = *held;
    CohMoveRect(&stale, to->x, to->y, drawn, from->x, from->y, to->width, to->height,
                COH_MOVE_BYTES);

    DkImageView source;
    DkImageView destination;
    dkImageViewDefaults(&source, rendered);
    dkImageViewDefaults(&destination, copy);

    GpuBeginList(&s->gpu);
    GpuSetTarget(&s->gpu, rendered, TARGET_SIZE, TARGET_SIZE);
    const bool recorded = PaintCells(s, seed, ALL_CELLS, TARGET_SIZE);
    // The copy engine reads what the fragments wrote, and only a full barrier waits for them.
    // Nothing to invalidate: rendered pixels sit in the level two cache the copy reads through.
    dkCmdBufBarrier(s->gpu.cmdbuf, DkBarrier_Full, 0);
    dkCmdBufCopyImage(s->gpu.cmdbuf, &source, from, &destination, to, 0);
    GpuSubmit(&s->gpu);
    if (Recorded(s, name, recorded)) {
        const CohImage *const olds[2] = { held, &stale };
        JudgeStale(name, GpuReadBackImage(&s->gpu, copy, TARGET_SIZE, TARGET_SIZE), &expected,
                   olds, 2);
    }
    *held = expected;
    *drawn = scratch;
}

static bool Open(Suite *s) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_SIZE, TARGET_SIZE);
    spec.code_size = 32u * 1024u;
    spec.data_size = 256u * 1024u;
    // Six small images and two target-sized ones, each rounded up to whole tiles.
    spec.image_size = 512u * 1024u;
    spec.cpu_image_size = 64u * 1024u;
    spec.cpu_cached_size = 64u * 1024u;
    const bool opened = GpuOpenSpec(&s->gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the images", CHECK_NONE, opened, "%s",
               s->gpu.failure);
    if (!opened) {
        return false;
    }
    const bool loaded =
        GpuLoadShader(&s->gpu, &s->vertex, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&s->gpu, &s->pattern, ShaderData(SHADER_PATTERN_FRAG),
                      ShaderSize(SHADER_PATTERN_FRAG)) &&
        GpuLoadShader(&s->gpu, &s->sample, ShaderData(SHADER_TEXTURE_FRAG),
                      ShaderSize(SHADER_TEXTURE_FRAG)) &&
        GpuLoadShader(&s->gpu, &s->uniform, ShaderData(SHADER_UNIFORM_FRAG),
                      ShaderSize(SHADER_UNIFORM_FRAG)) &&
        GpuLoadShader(&s->gpu, &s->alias, ShaderData(SHADER_ALIAS_FRAG),
                      ShaderSize(SHADER_ALIAS_FRAG));
    CHECK_TRUE(SUITE, "the shaders load", CHECK_NONE, loaded, "%s", s->gpu.failure);
    return loaded;
}

void RunCoherence(void) {
    static Suite suite;
    Suite *const s = &suite;
    memset(s, 0, sizeof(*s));
    if (!Open(s)) {
        GpuClose(&s->gpu);
        return;
    }
    GpuContext *const gpu = &s->gpu;

    DkImage rendered;      // render then sample
    DkImage pitch;         // processor writes, pitch linear
    DkImage block;         // processor writes, block linear
    DkImage alias_colour;  // rendered as RGBA8_Unorm
    DkImage alias_word;    // the same bytes sampled as R32_Uint
    DkImage copied;        // copy engine then sample
    DkImage copy_source;   // what the image copy reads
    DkImage big_rendered;  // render, copy, read back
    DkImage big_copy;
    DkImage cached;        // processor writes through its data cache, pitch linear
    DkImageLayout layout;
    DkImageLayout block_layout;
    uint8_t *pitch_bytes = NULL;
    uint8_t *block_bytes = NULL;
    uint8_t *cached_bytes = NULL;
    uint32_t cached_stride = 0;
    const bool created =
        GpuCreateImage(gpu, &rendered, &layout, DkImageFormat_RGBA8_Unorm, TEX, TEX,
                       DkImageFlags_UsageRender, 0, NULL) &&
        GpuCreateImage(gpu, &pitch, &layout, DkImageFormat_RGBA8_Unorm, TEX, TEX,
                       DkImageFlags_PitchLinear, TEX * 4u, &pitch_bytes) &&
        GpuCreateImage(gpu, &block, &block_layout, DkImageFormat_RGBA8_Unorm, TEX, TEX,
                       DkImageFlags_CustomTileSize, DkTileSize_OneGob, &block_bytes) &&
        GpuCreateAliasedImages(gpu, &alias_colour, DkImageFormat_RGBA8_Unorm, &alias_word,
                               DkImageFormat_R32_Uint, TEX, TEX, DkImageFlags_UsageRender) &&
        GpuCreateTexture(gpu, &copied, &layout, DkImageFormat_RGBA8_Unorm, TEX, TEX) &&
        GpuCreateTexture(gpu, &copy_source, &layout, DkImageFormat_RGBA8_Unorm, TEX, TEX) &&
        GpuCreateImage(gpu, &big_rendered, &layout, DkImageFormat_RGBA8_Unorm, TARGET_SIZE,
                       TARGET_SIZE, DkImageFlags_UsageRender, 0, NULL) &&
        GpuCreateTexture(gpu, &big_copy, &layout, DkImageFormat_RGBA8_Unorm, TARGET_SIZE,
                         TARGET_SIZE) &&
        GpuCreateCachedLinearImage(gpu, &cached, &layout, DkImageFormat_RGBA8_Unorm, TEX, TEX,
                                   &cached_bytes, &cached_stride);
    // PitchOffset assumes packed rows, which a 32 texel row already is.
    CHECK_TRUE(SUITE, "the images have somewhere to live", CHECK_NONE,
               created && cached_stride == TEX * 4u, "%s (cached rows %u bytes apart, not %u)",
               gpu->failure, (unsigned)cached_stride, (unsigned)(TEX * 4u));
    if (!created || cached_stride != TEX * 4u) {
        GpuClose(gpu);
        return;
    }
    // Eight gobs exactly is what BlockOffset assumes; any padding and the rule names wrong bytes.
    const uint64_t block_size = dkImageLayoutGetSize(&block_layout);
    CHECK_TRUE(SUITE, "the block linear texture is eight gobs of one gob blocks", CHECK_NONE,
               block_size == 8u * 512u, "the layout is %u bytes, not 4096",
               (unsigned)block_size);

    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, SLOT_COUNT * (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT,
        &s->image_set);
    DkSamplerDescriptor *const sampler = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
        &s->sampler_set);
    if (images == NULL || sampler == NULL) {
        ReportCase(SUITE, "the descriptor sets have somewhere to live", CHECK_NONE, false,
                   "the scratch block had no room for the descriptor sets");
        GpuClose(gpu);
        return;
    }
    const DkImage *const sampled[SLOT_COUNT] = {
        [SLOT_RENDERED] = &rendered, [SLOT_PITCH] = &pitch,   [SLOT_BLOCK] = &block,
        [SLOT_ALIAS] = &alias_word,  [SLOT_COPIED] = &copied, [SLOT_CACHED] = &cached,
    };
    for (uint32_t slot = 0; slot < SLOT_COUNT; ++slot) {
        DkImageView view;
        dkImageViewDefaults(&view, sampled[slot]);
        dkImageDescriptorInitialize(&images[slot], &view, false, false);
    }
    DkSampler nearest;
    dkSamplerDefaults(&nearest);
    nearest.minFilter = DkFilter_Nearest;
    nearest.magFilter = DkFilter_Nearest;
    nearest.mipFilter = DkMipFilter_None;
    nearest.wrapMode[0] = DkWrapMode_ClampToEdge;
    nearest.wrapMode[1] = DkWrapMode_ClampToEdge;
    nearest.wrapMode[2] = DkWrapMode_ClampToEdge;
    dkSamplerDescriptorInitialize(sampler, &nearest);

    // Starting contents, each put there before any case and read before being replaced.
    static CohImage rendered_texels;
    static CohImage pitch_texels;
    static CohImage block_texels;
    static CohImage copied_texels;
    static CohImage source_texels;
    static CohImage cached_texels;
    CohImage *const starting[] = { &rendered_texels, &pitch_texels, &block_texels,
                                   &copied_texels, &source_texels, &cached_texels };
    const uint32_t starting_seeds[] = { COH_SEED_RENDERED_START, COH_SEED_PITCH_START,
                                        COH_SEED_BLOCK_START, COH_SEED_COPIED_START,
                                        COH_SEED_COPY_SOURCE, COH_SEED_CACHED_START };
    for (size_t i = 0; i < sizeof(starting) / sizeof(starting[0]); ++i) {
        CohImageInit(starting[i], TEX, TEX);
        CohFillPattern(starting[i], starting_seeds[i]);
    }
    CpuWrite(pitch_bytes, PitchOffset, &pitch_texels, 0, TEX);
    CpuWrite(cached_bytes, PitchOffset, &cached_texels, 0, TEX);
    GpuFlushCachedImage(gpu, cached_bytes, TEX * TEX * 4u);
    // The model's rows are packed, which is what an upload takes.
    const bool uploaded =
        GpuUploadTexture(gpu, &rendered, DkImageFormat_RGBA8_Unorm, TEX, TEX,
                         rendered_texels.texels) &&
        GpuUploadTexture(gpu, &block, DkImageFormat_RGBA8_Unorm, TEX, TEX, block_texels.texels) &&
        GpuUploadTexture(gpu, &copied, DkImageFormat_RGBA8_Unorm, TEX, TEX,
                         copied_texels.texels) &&
        GpuUploadTexture(gpu, &copy_source, DkImageFormat_RGBA8_Unorm, TEX, TEX,
                         source_texels.texels);
    CHECK_TRUE(SUITE, "the starting texels reach the textures", CHECK_NONE, uploaded, "%s",
               gpu->failure);
    if (!uploaded) {
        GpuClose(gpu);
        return;
    }

    // The processor reading what the copy engine wrote, after the wait: the first half of every
    // processor case below, and the proof that the block linear rule names the right bytes.
    {
        static uint8_t seen[TEX * TEX][4];
        for (uint32_t y = 0; y < TEX; ++y) {
            for (uint32_t x = 0; x < TEX; ++x) {
                memcpy(seen[y * TEX + x], block_bytes + BlockOffset(x, y), 4);
            }
        }
        Judge("the processor finds the copy engine's texels where the block linear rule puts them",
              &seen[0][0], &block_texels, NULL);
    }

    RenderThenSample(s, "a texture rendered in the same list is sampled as drawn", &rendered,
                     SLOT_RENDERED, &s->sample, COH_MOVE_BYTES, COH_SEED_RENDER_WHOLE, ALL_CELLS,
                     &rendered_texels);
    RenderThenSample(s, "a partial redraw reaches the sampler and the rest keeps its texels",
                     &rendered, SLOT_RENDERED, &s->sample, COH_MOVE_BYTES, COH_SEED_RENDER_PART,
                     MIDDLE_CELLS, &rendered_texels);

    SampleThenProcessorWrites(s,
                              "texels the processor rewrites in a pitch linear texture reach "
                              "the next sample",
                              pitch_bytes, PitchOffset, false, SLOT_PITCH, &pitch_texels,
                              COH_SEED_PITCH_REWRITE, 0, TEX);
    // From row 12, which is half way down a gob: the write covers part of a gob and whole ones.
    SampleThenProcessorWrites(s,
                              "texels the processor rewrites in part of a block linear texture "
                              "reach the next sample",
                              block_bytes, BlockOffset, false, SLOT_BLOCK, &block_texels,
                              COH_SEED_BLOCK_REWRITE, 12u, TEX - 12u);
    // One row in the middle, through the cached mapping: small, inside a page the texture unit
    // has read, and only there once flushed.
    SampleThenProcessorWrites(s,
                              "one row the processor rewrites through a cached mapping reaches "
                              "the next sample once flushed",
                              cached_bytes, PitchOffset, true, SLOT_CACHED, &cached_texels,
                              COH_SEED_CACHED_ROW, 13u, 1u);

    UniformBetweenSubmits(s);
    UniformOneWordFourSubmits(s);
    UniformPushedBetweenDraws(s);

    // The alias starts as whatever the image memory held, so its first read is after a draw
    // that covers all of it.
    {
        static const char *const NAME =
            "a render target sampled as R32_Uint gives the little endian word of its bytes";
        static CohImage alias_texels;
        CohImageInit(&alias_texels, TEX, TEX);
        CohPaintCells(&alias_texels, COH_SEED_ALIAS_WHOLE, ALL_CELLS);
        ClearMain(s);
        GpuBeginList(gpu);
        GpuSetTarget(gpu, &alias_colour, TEX, TEX);
        bool recorded = PaintCells(s, COH_SEED_ALIAS_WHOLE, ALL_CELLS, TEX);
        // Rendered through one format, sampled through the other: the fragments have to land,
        // and nothing about the second view may come from a cache filled through the first.
        dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
        GpuSetTarget(gpu, NULL, 0, 0);
        recorded = recorded && SampleRegion(s, &FIRST, SLOT_ALIAS, &s->alias);
        GpuSubmit(gpu);
        if (Recorded(s, NAME, recorded)) {
            CohImageInit(&expected, TARGET_SIZE, TARGET_SIZE);
            CohFill(&expected, COH_SENTINEL);
            CohSampleRegion(&expected, &FIRST, &alias_texels, COH_MOVE_ALIASED);
            Judge(NAME, GpuReadBack(gpu), &expected, NULL);
        }
        // The same trap as the plain render case, through the other format.
        RenderThenSample(s, "redrawing a render target reaches its R32_Uint alias in one list",
                         &alias_colour, SLOT_ALIAS, &s->alias, COH_MOVE_ALIASED,
                         COH_SEED_ALIAS_PART, MIDDLE_CELLS, &alias_texels);
    }

    {
        static const char *const WHOLE_NAME =
            "a buffer the copy engine writes into a texture reaches the next sample in one list";
        static CohImage staged_texels;
        CohImageInit(&staged_texels, TEX, TEX);
        CohFillPattern(&staged_texels, COH_SEED_COPY_STAGED);
        DkCopyBuf staged;
        const DkImageRect whole = { 0, 0, 0, TEX, TEX, 1 };
        if (GpuStagePixels(gpu, TEX, TEX, staged_texels.texels, &staged)) {
            CopyThenSample(s, WHOLE_NAME, &copied, &copied_texels, &staged, NULL,
                           &staged_texels, &whole, &whole);
        } else {
            ReportCase(SUITE, WHOLE_NAME, CHECK_NONE, false, gpu->failure);
        }
        // Off the texel grid of both images, so a copy that ignored either offset shows.
        const DkImageRect from = { 5, 3, 0, 12, 10, 1 };
        const DkImageRect to = { 17, 9, 0, 12, 10, 1 };
        CopyThenSample(s, "part of an image the copy engine copies into a texture reaches the "
                          "next sample",
                       &copied, &copied_texels, NULL, &copy_source, &source_texels, &from,
                       &to);
    }

    {
        // The copy destination starts known, and is read back once, so a cache of its contents
        // exists before anything replaces them.
        static CohImage held;
        static CohImage drawn;
        CohImageInit(&held, TARGET_SIZE, TARGET_SIZE);
        CohFillPattern(&held, COH_SEED_HELD);
        // The render target starts known too, so a copy that read it before the render was
        // flushed shows as that pattern rather than as bytes nobody can name.
        CohImageInit(&drawn, TARGET_SIZE, TARGET_SIZE);
        CohFillPattern(&drawn, COH_SEED_RENDERED_BIG_START);
        const bool primed = GpuUploadTexture(gpu, &big_rendered, DkImageFormat_RGBA8_Unorm,
                                             TARGET_SIZE, TARGET_SIZE, drawn.texels) &&
                            GpuUploadTexture(gpu, &big_copy, DkImageFormat_RGBA8_Unorm,
                                             TARGET_SIZE, TARGET_SIZE, held.texels);
        Judge("an image read back before the copies holds what was uploaded",
              primed ? GpuReadBackImage(gpu, &big_copy, TARGET_SIZE, TARGET_SIZE) : NULL, &held,
              NULL);

        const DkImageRect whole = { 0, 0, 0, TARGET_SIZE, TARGET_SIZE, 1 };
        RenderCopyReadBack(s, "a render target copied to an image reads back as drawn",
                           &big_rendered, &big_copy, COH_SEED_READBACK_WHOLE, &whole, &whole,
                           &held, &drawn);
        const DkImageRect from = { 8, 40, 0, 24, 16, 1 };
        const DkImageRect to = { 36, 4, 0, 24, 16, 1 };
        RenderCopyReadBack(s, "part of a render target copied to an image reads back as drawn",
                           &big_rendered, &big_copy, COH_SEED_READBACK_PART, &from, &to, &held,
                           &drawn);
    }

    GpuClose(gpu);
}
