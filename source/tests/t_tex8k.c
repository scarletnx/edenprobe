// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "../check.h"
#include "../coherref.h"
#include "../gpu.h"
#include "../perf.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "../tex8kref.h"
#include "tests.h"

// Textures as large as a game's largest: 8192x8192, which the console's texture unit takes (its
// limit is 16384), in the formats a game ships them in. What each costs is the question: the
// upload, the draw that first samples it, the same draw again, frames that each change a part of
// it, and a copy out of it. Every case judges what it fetched on a 64x64 target, a texel a pixel,
// against tex8kref.c, which says what every texel holds; nothing reads back the large texture.
//
// Each texture has a device of its own, sized for it, so the run never holds two. The copy
// engine uploads it from a staging buffer the processor filled (a game's texture loader), with
// its first eight levels (8192 down to 64), each a square of level 0's staging; one RGBA8
// texture is pitch linear instead, written by the processor where the texture unit reads it.
// The first draw fetches level 7 alone, the size of the target, so a machine that has to make
// all eight levels before it can sample one pays for that there. Then 4096 texels across level 0,
// twice. Then 64 frames, each writing a 256x256 region of level 0 somewhere else and fetching
// it back in the same list, as a game streams; then the 4096 texels again, which now hold each
// frame's region where one wrote and the upload elsewhere; then a 256x256 region copied out to a
// buffer, a quarter in a frame's region, compared byte for byte.
//
// Last, an 8192x4096 RGBA16F target is cleared and drawn, and the processor reads eight of its
// texels through memory.
//
// Untimed by the passes: one pass makes the uploads a game makes once, and a run stays seconds
// long. Each case times its own work on the graphics unit (gpu.c's submits), and its line
// carries that; a frame case carries its median frame, and its detail the first, the 99th
// percentile and the slowest. Where the processor's own work is part of the path (filling the
// staging, writing a region of the pitch linear texture, reading the target through memory) the
// detail gives its time too, read from the system tick, since an emulator can spend there what
// the console does not.

static const char *const SUITE = "tex8k";

#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT
#define MIB (1024u * 1024u)
// Room past the levels' bytes for the library's alignment and the small levels' padding.
#define IMAGE_SLACK (8u * MIB)
#define SCRATCH_SLACK MIB

static bool timing = true;

void Tex8kSetTiming(bool on) {
    timing = on;
}

typedef struct {
    const char *label;
    DkImageFormat format;
    Tex8kFormat ref;
    uint32_t size;
    uint32_t levels;
    /// Pitch linear, in memory the processor writes; no staging and no copy engine.
    bool processor;
    bool frames;
} Entry;

static const Entry ENTRIES[] = {
    { "RGBA8", DkImageFormat_RGBA8_Unorm, { TEX8K_RGBA8, 1, 1, 4, NULL }, 8192, TEX8K_LEVELS,
      false, true },
    { "RGBA8 pitch linear", DkImageFormat_RGBA8_Unorm, { TEX8K_RGBA8, 1, 1, 4, NULL }, 8192, 1,
      true, true },
    { "BC1", DkImageFormat_RGBA_BC1, { TEX8K_BC1, 4, 4, 8, NULL }, 8192, TEX8K_LEVELS, false,
      true },
    { "BC7", DkImageFormat_RGBA_BC7_Unorm, { TEX8K_BC7, 4, 4, 16, NULL }, 8192, TEX8K_LEVELS,
      false, true },
    // The astcimage suite's picture, encoded by astcenc: the first is 4x4, the third 8x8.
    { "ASTC 4x4", DkImageFormat_RGBA_ASTC_4x4, { TEX8K_ASTC, 4, 4, 16, &ASTC_IMAGE[0] }, 8192,
      TEX8K_LEVELS, false, true },
    { "ASTC 8x8", DkImageFormat_RGBA_ASTC_8x8, { TEX8K_ASTC, 8, 8, 16, &ASTC_IMAGE[2] }, 8192,
      TEX8K_LEVELS, false, true },
    // The texture unit's largest, uploaded and sampled.
    { "BC1", DkImageFormat_RGBA_BC1, { TEX8K_BC1, 4, 4, 8, NULL }, 16384, 1, false, false },
};
#define ENTRY_COUNT (sizeof(ENTRIES) / sizeof(ENTRIES[0]))

/// fetch_lod.frag's uniform block.
typedef struct {
    int32_t size[4];
    int32_t offset[4];
} FetchLod;

/// One texture's device and what the cases leave each other.
typedef struct {
    const Entry *e;
    char texture[40];
    GpuContext gpu;
    DkShader shaders[2];
    DkImage image;
    DkImageLayout layout;
    DkGpuAddr image_set;
    DkGpuAddr sampler_set;
    /// Level 0's blocks in a row of blocks after another, staging_pitch bytes apart.
    uint8_t *staging;
    DkGpuAddr staging_addr;
    uint32_t staging_pitch;
    /// A region's blocks per frame, region_pitch bytes a row of blocks, one region after another.
    uint8_t *slots;
    DkGpuAddr slots_addr;
    uint32_t region_pitch;
    uint32_t region_bytes;
    /// Where the copy-out lands.
    uint8_t *out;
    DkGpuAddr out_addr;
    /// The pitch linear texture's texels, as the processor sees them.
    uint8_t *texels;
    bool made;
    /// The frames whose regions are in the texture, which the later judges expect.
    uint32_t frames;
} Run;

static Run g_run;

// The target and the copy-out as the processor read them, before anything judges them: under an
// emulator a read of memory the graphics unit just wrote can cost far more than the copy that
// wrote it, so the read is timed alone (g_read_ns) and the judge reads this copy.
static uint8_t g_pixels[TEX8K_TARGET * TEX8K_TARGET * 4u];
static uint8_t g_copied[TEX8K_REGION * TEX8K_REGION * 4u];
static uint64_t g_read_ns;

static uint32_t Microseconds(uint64_t nanoseconds) {
    const uint64_t us = (nanoseconds + 500u) / 1000u;
    return us == 0u ? 1u : us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

/// The end of a case whose own work on the graphics unit was @p work nanoseconds: its line carries
/// that time when the run times.
static void End(const char *name, CheckTier tier, bool passed, const char *detail, uint64_t work) {
    if (timing) {
        ReportCaseTime(SUITE, name, tier, passed, detail, Microseconds(work));
    } else {
        ReportCase(SUITE, name, tier, passed, detail);
    }
}

static uint64_t TickNs(uint64_t since) {
    return armTicksToNs(armGetSystemTick() - since);
}

/// Copies @p size bytes the graphics unit wrote at @p from into @p to, timing the read into
/// g_read_ns; NULL in, NULL out.
static const uint8_t *Take(uint8_t *to, const uint8_t *from, size_t size) {
    if (from == NULL) {
        return NULL;
    }
    const uint64_t reading = armGetSystemTick();
    memcpy(to, from, size);
    g_read_ns = TickNs(reading);
    return to;
}

static uint32_t LevelSide(const Entry *e, uint32_t level) {
    const uint32_t side = e->size >> level;
    return side != 0u ? side : 1u;
}

static uint32_t LevelBytes(const Entry *e, uint32_t level) {
    const uint32_t side = e->size >> level;
    return (side / e->ref.block_width) * (side / e->ref.block_height) * e->ref.block_bytes;
}

/// One draw onto the target: pixel (x0 + i, y0 + j) fetches texel (tx0 + i * step_x,
/// ty0 + j * step_y) of the level.
typedef struct {
    uint32_t x0;
    uint32_t y0;
    uint32_t w;
    uint32_t h;
    uint32_t level;
    uint32_t tx0;
    uint32_t ty0;
    uint32_t step_x;
    uint32_t step_y;
} Window;

static Window WholeWindow(uint32_t level, uint32_t tx0, uint32_t ty0, uint32_t step_x,
                          uint32_t step_y) {
    const Window w = { 0, 0, TEX8K_TARGET, TEX8K_TARGET, level, tx0, ty0, step_x, step_y };
    return w;
}

/// The 4096 texels spread over level 0, every size / 64th.
static Window SpreadWindow(const Entry *e) {
    const uint32_t step = e->size / TEX8K_TARGET;
    return WholeWindow(0, TEX8K_SPREAD_X, TEX8K_SPREAD_Y, step, step);
}

/// Frame @p f's cell, every 32nd texel of its region.
static Window CellWindow(uint32_t f) {
    uint32_t rx = 0;
    uint32_t ry = 0;
    Tex8kRegion(f, &rx, &ry);
    const uint32_t step = TEX8K_REGION / TEX8K_CELL;
    const Window w = { (f % (TEX8K_TARGET / TEX8K_CELL)) * TEX8K_CELL,
                       (f / (TEX8K_TARGET / TEX8K_CELL)) * TEX8K_CELL,
                       TEX8K_CELL,
                       TEX8K_CELL,
                       0,
                       rx + TEX8K_CELL_X,
                       ry + TEX8K_CELL_Y,
                       step,
                       step };
    return w;
}

/// Records the draw @p w into the open pass.
static bool RecordWindow(Run *r, const Window *w) {
    GpuContext *const gpu = &r->gpu;
    DkGpuAddr address = 0;
    uint8_t *const uniform =
        (uint8_t *)GpuAllocData(gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &address);
    if (uniform == NULL) {
        snprintf(gpu->failure, sizeof(gpu->failure), "no scratch left for a draw's uniforms");
        return false;
    }
    // The shader fetches pixel * step + offset, from the target's corner, not the window's.
    const int32_t side = (int32_t)LevelSide(r->e, w->level);
    const FetchLod s = { { side, side, (int32_t)w->step_x, (int32_t)w->step_y },
                         { (int32_t)w->tx0 - (int32_t)(w->x0 * w->step_x),
                           (int32_t)w->ty0 - (int32_t)(w->y0 * w->step_y), (int32_t)w->level,
                           0 } };
    memset(uniform, 0, UNIFORM_SIZE);
    memcpy(uniform, &s, sizeof(s));
    dkCmdBufBindImageDescriptorSet(gpu->cmdbuf, r->image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(gpu->cmdbuf, r->sampler_set, 1);
    dkCmdBufBindTexture(gpu->cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, address, UNIFORM_SIZE);
    // Clip space y is +1 at the target's first row.
    const float left = (float)w->x0 / TEX8K_TARGET * 2.0f - 1.0f;
    const float right = (float)(w->x0 + w->w) / TEX8K_TARGET * 2.0f - 1.0f;
    const float top = 1.0f - (float)w->y0 / TEX8K_TARGET * 2.0f;
    const float bottom = 1.0f - (float)(w->y0 + w->h) / TEX8K_TARGET * 2.0f;
    const GpuVertex quad[6] = {
        { left, bottom, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { right, bottom, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { right, top, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { left, bottom, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { right, top, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { left, top, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    return GpuDrawTriangles(gpu, &r->shaders[0], &r->shaders[1], quad, 6);
}

/// The salt texel (@p tx, @p ty) of @p level holds after everything the run wrote so far.
static uint32_t SaltOf(const Run *r, uint32_t level, uint32_t tx, uint32_t ty) {
    return level != 0u ? 0u : Tex8kSaltAt(tx, ty, r->frames);
}

typedef struct {
    uint32_t wrong;
    /// The first wrong pixel: where and what it fetched.
    bool drawn;
    uint32_t x;
    uint32_t y;
    uint32_t level;
    uint32_t tx;
    uint32_t ty;
    uint32_t salt;
    uint8_t got[4];
    uint8_t want[4];
} Judged;

/// Judges the target against what the @p count draws fetched, the clear where none drew, and
/// pictures it.
static void Judge(const Run *r, const uint8_t *pixels, const Window *windows, uint32_t count,
                  Judged *out) {
    memset(out, 0, sizeof(*out));
    const Entry *const e = r->e;
    Picture *const picture = PictureBegin(TEX8K_TARGET, TEX8K_TARGET, PICTURE_COLOUR, pixels);
    for (uint32_t y = 0; y < TEX8K_TARGET; ++y) {
        for (uint32_t x = 0; x < TEX8K_TARGET; ++x) {
            const Window *w = NULL;
            for (uint32_t i = 0; i < count && w == NULL; ++i) {
                if (x >= windows[i].x0 && x < windows[i].x0 + windows[i].w &&
                    y >= windows[i].y0 && y < windows[i].y0 + windows[i].h) {
                    w = &windows[i];
                }
            }
            uint8_t answers[3][4];
            memset(answers, 0, sizeof(answers));
            uint32_t tx = 0;
            uint32_t ty = 0;
            uint32_t ox = 0;
            uint32_t oy = 0;
            uint32_t salt = 0;
            if (w != NULL) {
                tx = w->tx0 + (x - w->x0) * w->step_x;
                ty = w->ty0 + (y - w->y0) * w->step_y;
                salt = SaltOf(r, w->level, tx, ty);
                Tex8kLevelOrigin(e->size, w->level, &ox, &oy);
                Tex8kTexel(&e->ref, ox + tx, oy + ty, salt, answers);
            }
            const uint8_t *const pixel = GpuPixel(pixels, TEX8K_TARGET, x, y);
            const bool right = Tex8kMatches(pixel, answers);
            PictureSet(picture, x, y, answers[0], right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (right) {
                continue;
            }
            if (out->wrong++ == 0u) {
                out->drawn = w != NULL;
                out->x = x;
                out->y = y;
                out->level = w != NULL ? w->level : 0u;
                out->tx = tx;
                out->ty = ty;
                out->salt = salt;
                memcpy(out->got, pixel, 4);
                memcpy(out->want, answers[0], 4);
            }
        }
    }
}

/// The detail of a judged target: the wrong pixels and the first, and @p times after them.
static void Describe(char *out, size_t size, const Judged *j, const char *times) {
    char wrong[224] = "";
    if (j->wrong != 0u && j->drawn) {
        snprintf(wrong, sizeof(wrong),
                 "%u of %u pixels wrong; pixel %u,%u fetched texel %u,%u of level %u, salt %u, as "
                 "%02x%02x%02x%02x, it holds %02x%02x%02x%02x",
                 (unsigned)j->wrong, (unsigned)(TEX8K_TARGET * TEX8K_TARGET), (unsigned)j->x,
                 (unsigned)j->y, (unsigned)j->tx, (unsigned)j->ty, (unsigned)j->level,
                 (unsigned)j->salt, j->got[0], j->got[1], j->got[2], j->got[3], j->want[0],
                 j->want[1], j->want[2], j->want[3]);
    } else if (j->wrong != 0u) {
        snprintf(wrong, sizeof(wrong),
                 "%u of %u pixels wrong; pixel %u,%u, where no draw fetched, reads "
                 "%02x%02x%02x%02x, the clear is 00000000",
                 (unsigned)j->wrong, (unsigned)(TEX8K_TARGET * TEX8K_TARGET), (unsigned)j->x,
                 (unsigned)j->y, j->got[0], j->got[1], j->got[2], j->got[3]);
    }
    snprintf(out, size, "%s%s%s", wrong, wrong[0] != '\0' && times[0] != '\0' ? "; " : "",
             times);
}

/// Clears the target, records @p count draws in one list, submits it and reads the target back;
/// @p draw_ns gets the submit alone.
static const uint8_t *Fetch(Run *r, const Window *windows, uint32_t count, uint64_t *draw_ns) {
    GpuContext *const gpu = &r->gpu;
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(gpu);
    for (uint32_t i = 0; i < count; ++i) {
        if (!RecordWindow(r, &windows[i])) {
            return NULL;
        }
    }
    const uint64_t before = PerfCaseWork();
    GpuSubmit(gpu);
    *draw_ns = PerfCaseWork() - before;
    return gpu->ready ? Take(g_pixels, GpuReadBack(gpu), sizeof(g_pixels)) : NULL;
}

static const char *Why(const GpuContext *gpu, const char *otherwise) {
    return gpu->failure[0] != '\0' ? gpu->failure : otherwise;
}

/// Fetches @p count draws and judges them, ending case @p name; @p before goes ahead of the times
/// in the detail.
static void FetchAndJudge(Run *r, const char *name, const Window *windows, uint32_t count,
                          const char *before) {
    uint64_t draw_ns = 0;
    const uint8_t *const pixels = Fetch(r, windows, count, &draw_ns);
    const uint64_t work = PerfCaseWork();
    if (pixels == NULL) {
        End(name, CHECK_NONE, false, Why(&r->gpu, "the draw or the readback did not happen"),
            work);
        return;
    }
    Judged j;
    Judge(r, pixels, windows, count, &j);
    char times[192] = "";
    if (timing) {
        snprintf(times, sizeof(times),
                 "%s%sthe draw %u us; the processor's read of the target %u us", before,
                 before[0] != '\0' ? ", " : "", (unsigned)Microseconds(draw_ns),
                 (unsigned)Microseconds(g_read_ns));
    }
    char detail[512];
    Describe(detail, sizeof(detail), &j, times);
    End(name, CHECK_EXACT, j.wrong == 0u, detail, work);
}

/// Skips every case of the texture after the one that could not leave what they need.
static void SkipRest(const char *const names[], size_t count, const char *why) {
    for (size_t i = 0; i < count; ++i) {
        if (names[i] != NULL) {
            ReportSkip(SUITE, names[i], REPORT_REASON_DEPENDS, why);
        }
    }
}

/// Points the first image descriptor at @p image, the whole of it, and the first sampler at the
/// nearest texel of the nearest level.
static void Descriptors(DkImageDescriptor *images, DkSamplerDescriptor *samplers,
                       const DkImage *image, bool pitch_linear, uint32_t pitch) {
    DkImageView view;
    dkImageViewDefaults(&view, image);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    if (pitch_linear) {
        // A pitch linear header's word 3 holds the pitch over 32 in its low 16 bits, and the
        // library writes the sparse tile width (bits 10 to 12) after it: a pitch of 32 KiB or
        // more loses its bits 15 to 17 (deko3d source/maxwell/tic_generate.cpp, pitch_5_20 then
        // sparse_tile_width_gobs_log2, at 373bfd4), so 32768 reaches the texture unit as nought.
        uint32_t words[8];
        memcpy(words, &images[0], sizeof(words));
        words[3] = (words[3] & 0xFFFF0000u) | pitch >> 5;
        memcpy(&images[0], words, sizeof(words));
    }
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_Nearest;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
}

static bool Open(Run *r, const char *name) {
    const Entry *const e = r->e;
    GpuSpec spec;
    GpuSpecDefaults(&spec, TEX8K_TARGET, TEX8K_TARGET);
    spec.code_size = 32u * 1024u;
    r->region_pitch = TEX8K_REGION / e->ref.block_width * e->ref.block_bytes;
    r->region_bytes = r->region_pitch * (TEX8K_REGION / e->ref.block_height);
    r->staging_pitch = e->size / e->ref.block_width * e->ref.block_bytes;
    uint64_t data = SCRATCH_SLACK + r->region_bytes;
    uint64_t image = 0;
    if (e->processor) {
        spec.cpu_image_size = (uint32_t)((uint64_t)e->size * e->size * 4u + MIB);
    } else {
        data += (uint64_t)LevelBytes(e, 0) +
                (e->frames ? (uint64_t)TEX8K_FRAMES * r->region_bytes : 0u);
        for (uint32_t level = 0; level < e->levels; ++level) {
            image += LevelBytes(e, level);
        }
        image += IMAGE_SLACK;
        spec.image_size = (uint32_t)image;
    }
    spec.data_size = (uint32_t)data;

    const bool opened = GpuOpenSpec(&r->gpu, &spec);
    if (!opened && r->gpu.out_of_memory) {
        char why[384];
        snprintf(why, sizeof(why),
                 "%s, asking for %u MiB of textures and %u MiB of staging and scratch: the "
                 "machine refuses the size",
                 r->gpu.failure,
                 (unsigned)((image + spec.cpu_image_size + MIB - 1u) / MIB),
                 (unsigned)((data + MIB - 1u) / MIB));
        ReportSkip(SUITE, name, REPORT_REASON_UNAVAILABLE, why);
        return false;
    }
    bool loaded = opened &&
                  GpuLoadShader(&r->gpu, &r->shaders[0], ShaderData(SHADER_COMMON_VERT),
                                ShaderSize(SHADER_COMMON_VERT)) &&
                  GpuLoadShader(&r->gpu, &r->shaders[1], ShaderData(SHADER_FETCH_LOD_FRAG),
                                ShaderSize(SHADER_FETCH_LOD_FRAG));
    DkImageDescriptor *images = NULL;
    DkSamplerDescriptor *samplers = NULL;
    if (loaded) {
        images = (DkImageDescriptor *)GpuAllocData(&r->gpu, (uint32_t)sizeof(DkImageDescriptor),
                                                   DK_IMAGE_DESCRIPTOR_ALIGNMENT, &r->image_set);
        samplers = (DkSamplerDescriptor *)GpuAllocData(
            &r->gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT,
            &r->sampler_set);
        r->out = (uint8_t *)GpuAllocData(&r->gpu, r->region_bytes, DK_MEMBLOCK_ALIGNMENT,
                                         &r->out_addr);
        if (!e->processor) {
            r->staging = (uint8_t *)GpuAllocData(&r->gpu, LevelBytes(e, 0), DK_MEMBLOCK_ALIGNMENT,
                                                 &r->staging_addr);
            if (e->frames) {
                r->slots = (uint8_t *)GpuAllocData(&r->gpu, TEX8K_FRAMES * r->region_bytes,
                                                   DK_MEMBLOCK_ALIGNMENT, &r->slots_addr);
            }
        }
        loaded = images != NULL && samplers != NULL && r->out != NULL &&
                 (e->processor || r->staging != NULL) &&
                 (e->processor || !e->frames || r->slots != NULL);
        if (!loaded) {
            snprintf(r->gpu.failure, sizeof(r->gpu.failure),
                     "the scratch block has no room for the staging the spec sized it for");
        }
    }
    if (loaded) {
        r->made = e->processor
                      ? GpuCreateImage(&r->gpu, &r->image, &r->layout, e->format, e->size,
                                       e->size, DkImageFlags_PitchLinear, e->size * 4u,
                                       &r->texels)
                      : GpuCreateMipTexture(&r->gpu, &r->image, &r->layout, e->format, e->size,
                                            e->size, e->levels);
        loaded = r->made;
    }
    if (loaded) {
        Descriptors(images, samplers, &r->image, e->processor, e->size * 4u);
    }
    CHECK_TRUE(SUITE, name, CHECK_NONE, loaded, "%s", Why(&r->gpu, "no reason given"));
    if (opened && !loaded) {
        GpuClose(&r->gpu);
    }
    return loaded;
}

/// The copy engine's upload of every level, each a square of level 0's staging.
static void RecordLevels(Run *r) {
    const Entry *const e = r->e;
    for (uint32_t level = 0; level < e->levels; ++level) {
        const uint32_t side = LevelSide(e, level);
        uint32_t ox = 0;
        uint32_t oy = 0;
        Tex8kLevelOrigin(e->size, level, &ox, &oy);
        DkImageView view;
        dkImageViewDefaults(&view, &r->image);
        view.mipLevelOffset = (uint8_t)level;
        view.mipLevelCount = 1;
        const DkCopyBuf source = { r->staging_addr + (DkGpuAddr)(oy / e->ref.block_height) *
                                                         r->staging_pitch +
                                       (ox / e->ref.block_width) * e->ref.block_bytes,
                                   r->staging_pitch, 0 };
        const DkImageRect rect = { 0, 0, 0, side, side, 1 };
        dkCmdBufCopyBufferToImage(r->gpu.cmdbuf, &source, &view, &rect, 0);
    }
}

static bool Upload(Run *r, const char *name) {
    const Entry *const e = r->e;
    if (!ReportStart(SUITE, name)) {
        return false;
    }
    GpuContext *const gpu = &r->gpu;
    const uint64_t filling = armGetSystemTick();
    uint64_t copies_ns = 0;
    if (e->processor) {
        Tex8kFillBlocks(&e->ref, r->texels, (size_t)e->size * 4u, 0, 0, e->size, e->size, 0);
    } else {
        Tex8kFillBlocks(&e->ref, r->staging, r->staging_pitch, 0, 0, e->size / e->ref.block_width,
                        e->size / e->ref.block_height, 0);
    }
    const uint64_t fill_ns = TickNs(filling);
    if (!e->processor) {
        dkCmdBufClear(gpu->cmdbuf);
        dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);
        RecordLevels(r);
        const uint64_t before = PerfCaseWork();
        GpuSubmit(gpu);
        copies_ns = PerfCaseWork() - before;
    }
    uint64_t draw_ns = 0;
    const bool level7 = e->levels > 7u;
    const Window window = level7 ? WholeWindow(7, 0, 0, 1, 1) : SpreadWindow(e);
    const uint8_t *const pixels = Fetch(r, &window, 1, &draw_ns);
    const uint64_t work = PerfCaseWork();
    if (pixels == NULL) {
        End(name, CHECK_NONE, false, Why(gpu, "the copies, the draw or the readback did not happen"),
            work);
        r->made = false;
        return false;
    }
    Judged j;
    Judge(r, pixels, &window, 1, &j);
    char times[192] = "";
    if (timing && e->processor) {
        snprintf(times, sizeof(times),
                 "the processor wrote its %u MiB in %u us; the draw %u us; the processor's read of "
                 "the target %u us",
                 (unsigned)(LevelBytes(e, 0) / MIB), (unsigned)Microseconds(fill_ns),
                 (unsigned)Microseconds(draw_ns), (unsigned)Microseconds(g_read_ns));
    } else if (timing) {
        snprintf(times, sizeof(times),
                 "the processor filled %u MiB of staging in %u us; the copies %u us, the draw %u "
                 "us; the processor's read of the target %u us",
                 (unsigned)(LevelBytes(e, 0) / MIB), (unsigned)Microseconds(fill_ns),
                 (unsigned)Microseconds(copies_ns), (unsigned)Microseconds(draw_ns),
                 (unsigned)Microseconds(g_read_ns));
    }
    char detail[512];
    Describe(detail, sizeof(detail), &j, times);
    End(name, CHECK_EXACT, j.wrong == 0u, detail, work);
    return true;
}

/// A fetch of 4096 texels across level 0, judged against everything written so far.
static void Spread(Run *r, const char *name) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const Window window = SpreadWindow(r->e);
    FetchAndJudge(r, name, &window, 1, "");
}

static void SortTimes(uint64_t *values, uint32_t count) {
    for (uint32_t i = 1; i < count; ++i) {
        const uint64_t value = values[i];
        uint32_t k = i;
        while (k > 0u && values[k - 1u] > value) {
            values[k] = values[k - 1u];
            --k;
        }
        values[k] = value;
    }
}

/// Sixty four frames, each writing region f of level 0 (the copy engine from a staging slot the
/// processor fills that frame, or the processor straight into the pitch linear texture) and
/// fetching every 32nd texel of it onto cell f, in one list; then the target is read back.
static void Frames(Run *r, const char *name) {
    const Entry *const e = r->e;
    if (!ReportStart(SUITE, name)) {
        return;
    }
    GpuContext *const gpu = &r->gpu;
    static uint64_t frame_ns[TEX8K_FRAMES];
    static Window cells[TEX8K_FRAMES];
    uint64_t processor_ns = 0;
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    bool recorded = gpu->ready;
    for (uint32_t f = 0; f < TEX8K_FRAMES && recorded; ++f) {
        uint32_t rx = 0;
        uint32_t ry = 0;
        Tex8kRegion(f, &rx, &ry);
        const uint32_t across = TEX8K_REGION / e->ref.block_width;
        const uint32_t rows = TEX8K_REGION / e->ref.block_height;
        const uint64_t writing = armGetSystemTick();
        if (e->processor) {
            Tex8kFillBlocks(&e->ref, r->texels + (size_t)ry * e->size * 4u + (size_t)rx * 4u,
                            (size_t)e->size * 4u, rx, ry, across, rows, f + 1u);
        } else {
            Tex8kFillBlocks(&e->ref, r->slots + (size_t)f * r->region_bytes, r->region_pitch,
                            rx / e->ref.block_width, ry / e->ref.block_height, across, rows,
                            f + 1u);
        }
        processor_ns += TickNs(writing);
        GpuBeginPass(gpu);
        if (!e->processor) {
            // The copy engine is ordered by full barriers alone, and the texture cache may hold
            // what an earlier draw read of the region.
            dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);
            DkImageView view;
            dkImageViewDefaults(&view, &r->image);
            view.mipLevelCount = 1;
            const DkCopyBuf source = { r->slots_addr + (DkGpuAddr)f * r->region_bytes,
                                       r->region_pitch, 0 };
            const DkImageRect rect = { rx, ry, 0, TEX8K_REGION, TEX8K_REGION, 1 };
            dkCmdBufCopyBufferToImage(gpu->cmdbuf, &source, &view, &rect, 0);
            dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
        }
        cells[f] = CellWindow(f);
        recorded = RecordWindow(r, &cells[f]);
        const uint64_t before = PerfCaseWork();
        GpuSubmit(gpu);
        frame_ns[f] = PerfCaseWork() - before;
        recorded = recorded && gpu->ready;
        if (recorded) {
            r->frames = f + 1u;
        }
    }
    const uint8_t *const pixels =
        recorded ? Take(g_pixels, GpuReadBack(gpu), sizeof(g_pixels)) : NULL;
    const uint64_t work = PerfCaseWork();
    if (pixels == NULL) {
        End(name, CHECK_NONE, false,
            Why(gpu, "a frame's copy or draw, or the readback, did not happen"), work);
        return;
    }
    Judged j;
    Judge(r, pixels, cells, TEX8K_FRAMES, &j);
    static uint64_t sorted[TEX8K_FRAMES];
    memcpy(sorted, frame_ns, sizeof(sorted));
    SortTimes(sorted, TEX8K_FRAMES);
    const uint64_t median = Tex8kPercentile(sorted, TEX8K_FRAMES, 50);
    char times[224] = "";
    if (timing) {
        snprintf(times, sizeof(times),
                 "a frame's median %u us, its 99th percentile %u us, the slowest %u us, the first "
                 "%u us; the processor's writes %u us in all, its read of the target %u us",
                 (unsigned)Microseconds(median),
                 (unsigned)Microseconds(Tex8kPercentile(sorted, TEX8K_FRAMES, 99)),
                 (unsigned)Microseconds(sorted[TEX8K_FRAMES - 1u]),
                 (unsigned)Microseconds(frame_ns[0]), (unsigned)Microseconds(processor_ns),
                 (unsigned)Microseconds(g_read_ns));
    }
    char detail[512];
    Describe(detail, sizeof(detail), &j, times);
    End(name, CHECK_EXACT, j.wrong == 0u, detail, median);
}

/// A 256x256 region of level 0, a quarter in frame 3's region, copied out to a buffer and held to
/// the blocks written there, byte for byte.
static void CopyOut(Run *r, const char *name) {
    const Entry *const e = r->e;
    if (!ReportStart(SUITE, name)) {
        return;
    }
    GpuContext *const gpu = &r->gpu;
    uint32_t x0 = 0;
    uint32_t y0 = 0;
    Tex8kCopyOutOrigin(&x0, &y0);
    memset(r->out, 0, r->region_bytes);
    dkCmdBufClear(gpu->cmdbuf);
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);
    DkImageView view;
    dkImageViewDefaults(&view, &r->image);
    view.mipLevelCount = 1;
    const DkImageRect rect = { x0, y0, 0, TEX8K_REGION, TEX8K_REGION, 1 };
    const DkCopyBuf destination = { r->out_addr, r->region_pitch, 0 };
    dkCmdBufCopyImageToBuffer(gpu->cmdbuf, &view, &rect, &destination, 0);
    GpuSubmit(gpu);
    const uint64_t work = PerfCaseWork();
    if (!gpu->ready) {
        End(name, CHECK_NONE, false, Why(gpu, "the copy did not happen"), work);
        return;
    }
    const uint8_t *const copied = Take(g_copied, r->out, r->region_bytes);
    const uint32_t bw = e->ref.block_width;
    const uint32_t bh = e->ref.block_height;
    uint32_t wrong = 0;
    uint32_t first_bx = 0;
    uint32_t first_by = 0;
    uint32_t first_salt = 0;
    uint8_t got[16];
    uint8_t want[16];
    uint32_t frame_blocks = 0;
    for (uint32_t by = 0; by < TEX8K_REGION / bh; ++by) {
        for (uint32_t bx = 0; bx < TEX8K_REGION / bw; ++bx) {
            const uint32_t gx = x0 / bw + bx;
            const uint32_t gy = y0 / bh + by;
            const uint32_t salt = Tex8kSaltAt(gx * bw, gy * bh, r->frames);
            uint8_t block[16];
            Tex8kBlock(&e->ref, gx, gy, salt, block);
            const uint8_t *const bytes =
                copied + (size_t)by * r->region_pitch + (size_t)bx * e->ref.block_bytes;
            if (memcmp(bytes, block, e->ref.block_bytes) == 0) {
                continue;
            }
            frame_blocks += salt != 0u ? 1u : 0u;
            if (wrong++ == 0u) {
                first_bx = gx;
                first_by = gy;
                first_salt = salt;
                memcpy(got, bytes, e->ref.block_bytes);
                memcpy(want, block, e->ref.block_bytes);
            }
        }
    }
    char times[64] = "";
    if (timing) {
        snprintf(times, sizeof(times), "the processor's read of the buffer %u us",
                 (unsigned)Microseconds(g_read_ns));
    }
    char detail[512];
    if (wrong == 0u) {
        snprintf(detail, sizeof(detail), "%s", times);
    } else {
        char got_hex[33];
        char want_hex[33];
        for (uint32_t i = 0; i < e->ref.block_bytes; ++i) {
            snprintf(got_hex + 2u * i, 3, "%02x", got[i]);
            snprintf(want_hex + 2u * i, 3, "%02x", want[i]);
        }
        snprintf(detail, sizeof(detail),
                 "%u of %u %s wrong (%u of them a frame's); %s %u,%u of salt %u reads %s, it "
                 "holds %s%s%s",
                 (unsigned)wrong, (unsigned)((TEX8K_REGION / bw) * (TEX8K_REGION / bh)),
                 bw == 1u ? "texels" : "blocks", (unsigned)frame_blocks,
                 bw == 1u ? "texel" : "block", (unsigned)first_bx, (unsigned)first_by,
                 (unsigned)first_salt, got_hex, want_hex, times[0] != '\0' ? "; " : "", times);
    }
    End(name, CHECK_EXACT, wrong == 0u, detail, work);
}

static void OneTexture(const Entry *e) {
    Run *const r = &g_run;
    memset(r, 0, sizeof(*r));
    r->e = e;
    snprintf(r->texture, sizeof(r->texture), "%ux%u %s", (unsigned)e->size, (unsigned)e->size,
             e->label);
    char open_name[160];
    char upload[160];
    char spread[160];
    char again[160];
    char frames[160];
    char after[160];
    char copy_out[160];
    snprintf(open_name, sizeof(open_name), "graphics device opens with room for the %s texture",
             r->texture);
    if (e->processor) {
        snprintf(upload, sizeof(upload),
                 "the processor writes the %s texture, and a draw fetches 4096 texels across it",
                 r->texture);
    } else if (e->levels > 7u) {
        snprintf(upload, sizeof(upload),
                 "the copy engine uploads the %s texture's %u levels, and a draw fetches level 7 "
                 "alone",
                 r->texture, (unsigned)e->levels);
    } else {
        snprintf(upload, sizeof(upload),
                 "the copy engine uploads the %s texture, and a draw fetches 4096 texels across it",
                 r->texture);
    }
    snprintf(spread, sizeof(spread), "a draw fetches 4096 texels across level 0 of the %s texture",
             r->texture);
    snprintf(again, sizeof(again), "the same draw again over the %s texture", r->texture);
    snprintf(frames, sizeof(frames), "%u frames each %s a 256x256 region of the %s texture and fetch it",
             (unsigned)TEX8K_FRAMES, e->processor ? "write" : "copy", r->texture);
    snprintf(after, sizeof(after),
             "after the frames the %s texture holds each frame's region and the upload elsewhere",
             r->texture);
    snprintf(copy_out, sizeof(copy_out),
             "a 256x256 region of the %s texture copied out to a buffer holds its bytes",
             r->texture);
    const char *const rest[] = {
        upload,
        e->levels > 7u ? spread : NULL,
        again,
        e->frames ? frames : NULL,
        e->frames ? after : NULL,
        copy_out,
    };
    const size_t rest_count = sizeof(rest) / sizeof(rest[0]);

    if (!ReportStart(SUITE, open_name)) {
        SkipRest(rest, rest_count, "the case that opens the device did not run");
        return;
    }
    if (!Open(r, open_name)) {
        SkipRest(rest, rest_count,
                 "the device for the texture did not open, or the texture has no memory");
        return;
    }
    if (!Upload(r, upload) || !r->made) {
        SkipRest(rest + 1, rest_count - 1u,
                 "the texture was not uploaded: the case that uploads it did not run or failed "
                 "before its draw");
        GpuClose(&r->gpu);
        return;
    }
    if (e->levels > 7u) {
        Spread(r, spread);
    }
    Spread(r, again);
    if (e->frames) {
        Frames(r, frames);
        Spread(r, after);
    }
    CopyOut(r, copy_out);
    GpuClose(&r->gpu);
}

// The large render target: an RGBA16F 8192x4096, 256 MiB, in memory the processor reads.
#define RT_WIDTH 8192u
#define RT_HEIGHT 4096u
#define RT_GOBS 16u
#define RT_TILE 4u

typedef struct {
    uint32_t x;
    uint32_t y;
    bool drawn;
} RtTexel;

// The drawn rectangle is x 2048 to 6143, y 1024 to 3071; four texels inside, four outside, two
// of them its neighbours across an edge and two the target's far corners.
static const RtTexel RT_TEXELS[] = {
    { 2048, 1024, true }, { 6143, 3071, true }, { 4000, 2000, true }, { 2048, 3071, true },
    { 0, 0, false },      { 8191, 4095, false }, { 2047, 1024, false }, { 6144, 3071, false },
};

static void RenderTarget(void) {
    static const char *const OPEN = "graphics device opens with room for an 8192x4096 RGBA16F "
                                    "target the processor reads";
    static const char *const NAME = "an 8192x4096 RGBA16F target cleared and drawn reads right "
                                    "through memory at eight texels";
    if (!ReportStart(SUITE, OPEN)) {
        ReportSkip(SUITE, NAME, REPORT_REASON_DEPENDS, "the case that opens the device did not run");
        return;
    }
    GpuSpec spec;
    GpuSpecDefaults(&spec, TEX8K_TARGET, TEX8K_TARGET);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = RT_WIDTH * RT_HEIGHT * 8u + MIB;
    GpuContext *const gpu = &g_run.gpu;
    const bool opened = GpuOpenSpec(gpu, &spec);
    if (!opened && gpu->out_of_memory) {
        char why[320];
        snprintf(why, sizeof(why), "%s, asking for %u MiB: the machine refuses the size",
                 gpu->failure, (unsigned)(spec.cpu_image_size / MIB));
        ReportSkip(SUITE, OPEN, REPORT_REASON_UNAVAILABLE, why);
        ReportSkip(SUITE, NAME, REPORT_REASON_DEPENDS, "the device did not open");
        return;
    }
    DkShader shaders[2];
    DkImage target;
    DkImageLayout layout;
    uint8_t *bytes = NULL;
    bool ready = opened &&
                 GpuLoadShader(gpu, &shaders[0], ShaderData(SHADER_COMMON_VERT),
                               ShaderSize(SHADER_COMMON_VERT)) &&
                 GpuLoadShader(gpu, &shaders[1], ShaderData(SHADER_FLAT_FRAG),
                               ShaderSize(SHADER_FLAT_FRAG)) &&
                 GpuCreateImage(gpu, &target, &layout, DkImageFormat_RGBA16_Float, RT_WIDTH,
                                RT_HEIGHT, DkImageFlags_UsageRender | DkImageFlags_CustomTileSize,
                                RT_TILE, &bytes);
    if (ready && dkImageLayoutGetSize(&layout) != (uint64_t)RT_WIDTH * RT_HEIGHT * 8u) {
        snprintf(gpu->failure, sizeof(gpu->failure),
                 "the library laid the target out in %llu bytes, the block linear rule says %llu",
                 (unsigned long long)dkImageLayoutGetSize(&layout),
                 (unsigned long long)((uint64_t)RT_WIDTH * RT_HEIGHT * 8u));
        ready = false;
    }
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, ready, "%s", Why(gpu, "no reason given"));
    if (!ready) {
        ReportSkip(SUITE, NAME, REPORT_REASON_DEPENDS, "the device or the target did not come up");
        if (opened) {
            GpuClose(gpu);
        }
        return;
    }
    if (!ReportStart(SUITE, NAME)) {
        GpuClose(gpu);
        return;
    }
    // Clear 0.25, 0.5, 0.75, 1; drawn 1, 0.125, 2, 0.5: every value a half float holds exactly.
    static const uint16_t CLEARED[4] = { 0x3400, 0x3800, 0x3A00, 0x3C00 };
    static const uint16_t DRAWN[4] = { 0x3C00, 0x3000, 0x4000, 0x3800 };
    GpuBeginPass(gpu);
    GpuSetTarget(gpu, &target, RT_WIDTH, RT_HEIGHT);
    dkCmdBufClearColorFloat(gpu->cmdbuf, 0, DkColorMask_RGBA, 0.25f, 0.5f, 0.75f, 1.0f);
    const float left = 2048.0f / RT_WIDTH * 2.0f - 1.0f;
    const float right = 6144.0f / RT_WIDTH * 2.0f - 1.0f;
    const float top = 1.0f - 1024.0f / RT_HEIGHT * 2.0f;
    const float bottom = 1.0f - 3072.0f / RT_HEIGHT * 2.0f;
    const GpuVertex quad[6] = {
        { left, bottom, 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 2.0f, 0.5f },
        { right, bottom, 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 2.0f, 0.5f },
        { right, top, 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 2.0f, 0.5f },
        { left, bottom, 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 2.0f, 0.5f },
        { right, top, 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 2.0f, 0.5f },
        { left, top, 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 2.0f, 0.5f },
    };
    const bool drew = GpuDrawTriangles(gpu, &shaders[0], &shaders[1], quad, 6);
    // What the processor reads next comes from memory, so everything drawn has to be there.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);
    GpuSubmit(gpu);
    const uint64_t work = PerfCaseWork();
    if (!drew || !gpu->ready) {
        End(NAME, CHECK_NONE, false, Why(gpu, "the clear or the draw did not happen"), work);
        GpuClose(gpu);
        return;
    }
    const uint64_t reading = armGetSystemTick();
    uint16_t got[sizeof(RT_TEXELS) / sizeof(RT_TEXELS[0])][4];
    for (size_t i = 0; i < sizeof(RT_TEXELS) / sizeof(RT_TEXELS[0]); ++i) {
        const volatile uint8_t *const texel =
            bytes + CohBlockLinearOffset(RT_TEXELS[i].x * 8u, RT_TEXELS[i].y, RT_WIDTH * 8u,
                                         RT_GOBS);
        for (uint32_t c = 0; c < 4u; ++c) {
            got[i][c] = (uint16_t)(texel[2u * c] | texel[2u * c + 1u] << 8);
        }
    }
    const uint64_t read_ns = TickNs(reading);
    uint32_t wrong = 0;
    size_t first = 0;
    for (size_t i = 0; i < sizeof(RT_TEXELS) / sizeof(RT_TEXELS[0]); ++i) {
        const uint16_t *const want = RT_TEXELS[i].drawn ? DRAWN : CLEARED;
        if (memcmp(got[i], want, sizeof(got[i])) != 0 && wrong++ == 0u) {
            first = i;
        }
    }
    char times[96] = "";
    if (timing) {
        snprintf(times, sizeof(times), "the processor's reads %u us",
                 (unsigned)Microseconds(read_ns));
    }
    char detail[512];
    if (wrong == 0u) {
        snprintf(detail, sizeof(detail), "%s", times);
    } else {
        const uint16_t *const want = RT_TEXELS[first].drawn ? DRAWN : CLEARED;
        snprintf(detail, sizeof(detail),
                 "%u of %u texels wrong; texel %u,%u reads %04x %04x %04x %04x, it holds %04x "
                 "%04x %04x %04x (%s)%s%s",
                 (unsigned)wrong, (unsigned)(sizeof(RT_TEXELS) / sizeof(RT_TEXELS[0])),
                 (unsigned)RT_TEXELS[first].x, (unsigned)RT_TEXELS[first].y, got[first][0],
                 got[first][1], got[first][2], got[first][3], want[0], want[1], want[2], want[3],
                 RT_TEXELS[first].drawn ? "drawn" : "cleared", times[0] != '\0' ? "; " : "",
                 times);
    }
    End(NAME, CHECK_EXACT, wrong == 0u, detail, work);
    GpuClose(gpu);
}

void RunTex8k(void) {
    for (size_t i = 0; i < ENTRY_COUNT; ++i) {
        OneTexture(&ENTRIES[i]);
    }
    RenderTarget();
}
