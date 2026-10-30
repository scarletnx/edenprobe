// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "../audio.h"
#include "../audioref.h"
#include "../check.h"
#include "../fsref.h"
#include "../gameref.h"
#include "../gpu.h"
#include "../mode.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "../viewer.h"
#include "fscard.h"
#include "tests.h"

// The game family: one scene that runs everything at once, the way a game's frame loop does,
// where every other family asks one thing at a time.
//
// Four threads over the three cores the program has. The main thread (this one) runs the frame
// loop: each frame it reads the pad, asks hid and the steady clock, steps a world of 1024 cells,
// writes that frame's vertices, texels and 1024 uniform buffers into one of two slots, and hands
// the frame to the render thread and a read to the file thread. The render thread, on core 1,
// takes an image from a swapchain on the default window, records the frame (the fill suite's full
// target quads, depth tested; the texture suite's sampled quad, its texels uploaded that frame;
// the stream suite's one draw per uniform buffer), copies the target out, draws it onto the
// image, submits, presents and hashes the copy. In a run with a screen the window is the
// screen's, and the frames are shown through it instead. The audio thread, on core 2, keeps the
// renderer stepping with a tone on a voice, through the audio family's submix at a gain of
// nought, so it is silent. The file thread, also on core 2 and below the audio thread, reads
// 64 KiB of the fs family's big file each frame.
//
// Nothing a frame holds depends on the time: its number decides its image (gameref.c), and its
// read. So each verdict is the same on a fast machine and a slow one, and the time is measured
// beside it. Taking an image blocks the render thread until the display gives one back, which is
// what paces a frame to the display.

static const char *const SUITE = "game";

#define SWAP_WIDTH 1280u
#define SWAP_HEIGHT 720u
#define SWAP_IMAGES 2u
// The main thread is one frame ahead of the render thread, at most: two slots of what it writes.
#define SLOTS 2u

#define MAIN_CORE 0
#define RENDER_CORE 1
#define AUDIO_CORE 2
#define FILE_CORE 2
// Below the number is above in priority: the audio thread over the file thread on their core.
#define RENDER_PRIORITY 0x2B
#define AUDIO_PRIORITY 0x28
#define FILE_PRIORITY 0x2E
#define STACK_SIZE 0x10000

// The tone fills a 240-sample frame with whole periods and loops over a second, so the aux ring
// holds the same steady wave whenever it is read, as in the audio family; it is read once the
// voice has played 100 renderer frames, whatever the frames took, or 5 s after them at most.
#define TONE_HZ 1000.0
#define TONE_LEVEL 0.5
#define TONE_LOOP 48000u
#define AUDIO_LATE_US 10000u

#define FILL_VERTICES (GAME_FILL_QUADS * 6u)
#define CELL_VERTICES (GAME_DRAWS * 6u)

static uint32_t frames = MODE_FRAMES_DEFAULT;
static bool timing = true;

void GameSetFrames(uint32_t count) {
    frames = count >= 1u && count <= MODE_FRAMES_MAX ? count : MODE_FRAMES_DEFAULT;
}

void GameSetTiming(bool on) {
    timing = on;
}

// The graphics device and everything the frames draw with, made once before the first frame.
static GpuContext gpu;
static DkShader common_vs;
static DkShader flat_fs;
static DkShader texture_fs;
static DkShader position_vs;
static DkShader ubo_fs;
static DkImage texture;
static DkImageLayout texture_layout;
static DkImage swap_images[SWAP_IMAGES];
static DkImageLayout swap_layouts[SWAP_IMAGES];
static DkSwapchain swapchain;
// The program's screen was up when the scene began (a run with a screen): the frames are shown
// through it, composed by the processor, and no swapchain is made on its window.
static bool on_screen;
static DkSwapchainMaker swap_maker;
static DkGpuAddr image_set;
static DkGpuAddr sampler_set;
static DkGpuAddr texture_quad;
static DkGpuAddr compose_quad;
static DkGpuAddr cells;
static DkGpuAddr ubo_count;

// What the main thread writes for a frame, one slot per frame in flight.
static GpuVertex *fill_cpu[SLOTS];
static DkGpuAddr fill_gpu[SLOTS];
static uint8_t *staging_cpu[SLOTS];
static DkGpuAddr staging_gpu[SLOTS];
static uint32_t staging_pitch;
static uint8_t *ubo_cpu[SLOTS];
static DkGpuAddr ubo_gpu[SLOTS];

// The card.
static FsFileSystem card;
static FsFile big;
static uint8_t file_buffer[GAME_FILE_CHUNK] __attribute__((aligned(0x1000)));

// The answers, worked out before the first frame, and what the frames left.
static uint32_t expected[MODE_FRAMES_MAX][GAME_REGIONS];
static uint32_t chunk_hash[GAME_FILE_CHUNKS];
static uint32_t frame_us[MODE_FRAMES_MAX];
static uint32_t sorted_us[MODE_FRAMES_MAX];
// A frame's work with every wait left out, in two parts: the main thread's, written before the
// semaphore that hands the frame over, and the render thread's.
static uint32_t main_us[MODE_FRAMES_MAX];
static uint32_t render_us[MODE_FRAMES_MAX];
static uint8_t wrong_pixels[GAME_IMAGE_BYTES];
static uint8_t want_pixels[GAME_IMAGE_BYTES];
static GameWorld world;

static Thread render_thread;
static Thread audio_thread;
static Thread file_thread;
static Semaphore frame_go;
static Semaphore slot_free;
static Semaphore file_go;
static bool audio_stop;

/// Which core a thread found itself on: the first wrong one, and how many times it looked.
typedef struct {
    uint32_t looks;
    uint32_t wrong;
    uint32_t wrong_at;
    uint32_t wrong_core;
} CoreSeen;

static void Look(CoreSeen *seen, uint32_t want, uint32_t at) {
    const uint32_t core = svcGetCurrentProcessorNumber();
    if (core != want && seen->wrong++ == 0u) {
        seen->wrong_at = at;
        seen->wrong_core = core;
    }
    ++seen->looks;
}

// What each thread saw, written by that thread and read by this one after it is joined.
static struct {
    uint32_t presented;
    uint32_t wrong_frames;
    uint32_t first_wrong;
    uint32_t got[GAME_REGIONS];
    CoreSeen core;
} render;

static struct {
    uint32_t steps;
    uint32_t longest;
    uint32_t late;
    bool failed;
    /// The voice had not played GAME_AUDIO_SETTLE samples GAME_AUDIO_LIMIT_MS after the frames,
    /// and the samples it had.
    bool timed_out;
    uint64_t played;
    CoreSeen core;
} sound;
// When the frames ended, for the audio thread's limit: nought until they have.
static uint64_t frames_over_tick;

static struct {
    uint32_t reads;
    uint32_t longest;
    uint32_t failures;
    uint32_t failed_at;
    uint32_t failed_rc;
    uint64_t failed_got;
    uint32_t wrong;
    uint32_t wrong_at;
    CoreSeen core;
} reads;

static struct {
    uint32_t first_style;
    uint32_t style_wrong;
    uint32_t style_wrong_at;
    uint32_t style_wrong_rc;
    uint32_t style_wrong_value;
    uint32_t clock_failures;
    uint32_t clock_back;
    uint32_t clock_source_changes;
    uint32_t clock_wrong_at;
    uint32_t clock_wrong_rc;
    int64_t clock_before;
    int64_t clock_after;
    /// Whether this thread could be put on core 0 alone for the frames, the results of the two
    /// calls that do it and of the one that puts it back, and where it ran.
    bool pinned;
    uint32_t get_mask_rc;
    uint32_t set_mask_rc;
    uint32_t restore_rc;
    int32_t ideal;
    uint64_t mask;
    CoreSeen core;
} asked;

static uint32_t Microseconds(uint64_t ticks) {
    const uint64_t us = armTicksToNs(ticks) / 1000u;
    return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

static GpuVertex Corner(float x, float y, float z, float u, float v, const uint8_t colour[4]) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.u = u;
    vertex.v = v;
    vertex.r = (float)colour[0] / 255.0f;
    vertex.g = (float)colour[1] / 255.0f;
    vertex.b = (float)colour[2] / 255.0f;
    vertex.a = (float)colour[3] / 255.0f;
    return vertex;
}

/// Six vertices, two triangles, over the clip space rectangle from @p x0, @p y0 (its top left)
/// to @p x1, @p y1, carrying the texture coordinates from nought to one across it.
static void Rectangle(GpuVertex out[6], float x0, float y0, float x1, float y1, float z,
                      const uint8_t colour[4]) {
    const GpuVertex top_left = Corner(x0, y0, z, 0.0f, 0.0f, colour);
    const GpuVertex top_right = Corner(x1, y0, z, 1.0f, 0.0f, colour);
    const GpuVertex bottom_right = Corner(x1, y1, z, 1.0f, 1.0f, colour);
    const GpuVertex bottom_left = Corner(x0, y1, z, 0.0f, 1.0f, colour);
    out[0] = top_left;
    out[1] = top_right;
    out[2] = bottom_right;
    out[3] = top_left;
    out[4] = bottom_right;
    out[5] = bottom_left;
}

/// Everything frame @p world is at reads from the processor: the fill quads, the texels and the
/// uniform buffers, into @p slot.
static void WriteSlot(uint32_t slot, const GameWorld *at) {
    for (uint32_t q = 0; q < GAME_FILL_QUADS; ++q) {
        uint8_t colour[4];
        GameFillColour(at->frame, q, colour);
        Rectangle(fill_cpu[slot] + q * 6u, -1.0f, 1.0f, 1.0f, -1.0f,
                  GameFillDepth(at->frame, q), colour);
    }
    for (uint32_t y = 0; y < GAME_TEXTURE; ++y) {
        for (uint32_t x = 0; x < GAME_TEXTURE; ++x) {
            GameTexel(at->frame, x, y, staging_cpu[slot] + (size_t)y * staging_pitch + x * 4u);
        }
    }
    for (uint32_t d = 0; d < GAME_DRAWS; ++d) {
        const uint32_t word = GameStreamWord(at, d);
        uint32_t *const entries = (uint32_t *)(ubo_cpu[slot] + (size_t)d * GAME_UBO_BYTES);
        for (uint32_t e = 0; e < GAME_UBO_ENTRIES; ++e) {
            entries[e * 4u] = word;
        }
    }
}

static void BindDepth(bool test, bool write) {
    DkDepthStencilState state;
    dkDepthStencilStateDefaults(&state);
    state.depthTestEnable = test ? 1u : 0u;
    state.depthWriteEnable = write ? 1u : 0u;
    state.stencilTestEnable = 0;
    state.depthCompareOp = DkCompareOp_Less;
    dkCmdBufBindDepthStencilState(gpu.cmdbuf, &state);
}

/// Records, submits and presents frame @p k from @p slot, then judges its copy. Hands back the
/// render thread's part of the frame's work: from the image in hand to the queue standing idle
/// after the frame's submit, so the graphics unit's work is in it and the wait for an image is
/// not; the present comes after it.
static uint64_t DrawFrame(uint32_t k, uint32_t slot) {
    const int image = on_screen ? 0 : dkQueueAcquireImage(gpu.queue, swapchain);
    const uint64_t began = armGetSystemTick();
    GpuBeginPass(&gpu);
    // The processor rewrote this slot after the graphics unit last read it, through memory the
    // unit caches in its level two cache: what the previous frame left there is dropped. The
    // previous frame's wait for idle wrote everything it drew back first.
    dkCmdBufBarrier(gpu.cmdbuf, DkBarrier_None, DkInvalidateFlags_L2Cache);

    DkImageView texture_view;
    dkImageViewDefaults(&texture_view, &texture);
    const DkImageRect texels = { 0, 0, 0, GAME_TEXTURE, GAME_TEXTURE, 1 };
    const DkCopyBuf source = { staging_gpu[slot], staging_pitch, 0 };
    dkCmdBufCopyBufferToImage(gpu.cmdbuf, &source, &texture_view, &texels, 0);
    dkCmdBufBarrier(gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    dkCmdBufClearColorFloat(gpu.cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    BindDepth(false, true);
    dkCmdBufClearDepthStencil(gpu.cmdbuf, true, 1.0f, 0, 0);

    BindDepth(true, true);
    GpuBindVertices(&gpu, &common_vs, &flat_fs, fill_gpu[slot],
                    FILL_VERTICES * (uint32_t)sizeof(GpuVertex));
    dkCmdBufDraw(gpu.cmdbuf, DkPrimitive_Triangles, FILL_VERTICES, 1, 0, 0);

    BindDepth(false, false);
    dkCmdBufBindImageDescriptorSet(gpu.cmdbuf, image_set, 2);
    dkCmdBufBindSamplerDescriptorSet(gpu.cmdbuf, sampler_set, 1);
    dkCmdBufBindTexture(gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    GpuBindVertices(&gpu, &common_vs, &texture_fs, texture_quad, 6u * (uint32_t)sizeof(GpuVertex));
    dkCmdBufDraw(gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);

    GpuBindPositions(&gpu, &position_vs, &ubo_fs, cells, CELL_VERTICES * 16u);
    dkCmdBufBindUniformBuffer(gpu.cmdbuf, DkStage_Fragment, 1, ubo_count,
                              DK_UNIFORM_BUF_ALIGNMENT);
    for (uint32_t d = 0; d < GAME_DRAWS; ++d) {
        dkCmdBufBindUniformBuffer(gpu.cmdbuf, DkStage_Fragment, 0,
                                  ubo_gpu[slot] + (DkGpuAddr)d * GAME_UBO_BYTES, GAME_UBO_BYTES);
        dkCmdBufDraw(gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, d * 6u, 0);
    }

    // The copy the judge reads, then the target drawn onto the image the display shows.
    dkCmdBufBarrier(gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
    DkImageView target_view;
    dkImageViewDefaults(&target_view, &gpu.image);
    const DkImageRect whole = { 0, 0, 0, GAME_TARGET, GAME_TARGET, 1 };
    const DkCopyBuf readback = { dkMemBlockGetGpuAddr(gpu.read_memory), 0, 0 };
    dkCmdBufCopyImageToBuffer(gpu.cmdbuf, &target_view, &whole, &readback, 0);
    if (!on_screen) {
        GpuSetTarget(&gpu, &swap_images[image], SWAP_WIDTH, SWAP_HEIGHT);
        dkCmdBufClearColorFloat(gpu.cmdbuf, 0, DkColorMask_RGBA, 0.06f, 0.06f, 0.08f, 1.0f);
        dkCmdBufBindTexture(gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(1, 0));
        GpuBindVertices(&gpu, &common_vs, &texture_fs, compose_quad,
                        6u * (uint32_t)sizeof(GpuVertex));
        dkCmdBufDraw(gpu.cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
    }
    GpuSubmit(&gpu);
    const uint64_t worked = armGetSystemTick() - began;
    // Presented whatever the submit came to: an image taken and never queued back would leave
    // the swapchain one short for the destroy that follows a fault.
    if (!on_screen) {
        dkQueuePresentImage(gpu.queue, swapchain, image);
    }
    if (!gpu.ready) {
        return worked;
    }

    uint32_t got[GAME_REGIONS];
    const uint8_t *const pixels = (const uint8_t *)dkMemBlockGetCpuAddr(gpu.read_memory);
    GameRegionHashes(pixels, got);
    if (memcmp(got, expected[k], sizeof(got)) != 0 && render.wrong_frames++ == 0u) {
        render.first_wrong = k;
        memcpy(render.got, got, sizeof(got));
        memcpy(wrong_pixels, pixels, GAME_IMAGE_BYTES);
    }
    // Counted once shown: through the swapchain, queued above; through the screen, here.
    if (!on_screen || ViewerShowImage(pixels, GAME_TARGET, GAME_TARGET)) {
        ++render.presented;
    }
    return worked;
}

static void RenderMain(void *argument) {
    (void)argument;
    uint64_t previous = 0;
    for (uint32_t k = 0; k < frames; ++k) {
        semaphoreWait(&frame_go);
        if (k == 0u) {
            previous = armGetSystemTick();
        }
        Look(&render.core, RENDER_CORE, k);
        const uint64_t worked = gpu.ready ? DrawFrame(k, k % SLOTS) : 0u;
        // After the present was queued: the wait for the display is in the next frame's acquire.
        const uint64_t now = armGetSystemTick();
        frame_us[k] = Microseconds(now - previous);
        render_us[k] = Microseconds(worked);
        previous = now;
        semaphoreSignal(&slot_free);
    }
}

static void AudioMain(void *argument) {
    (void)argument;
    uint64_t previous = 0;
    for (;;) {
        if (!AudioStep()) {
            sound.failed = true;
            return;
        }
        const uint64_t now = armGetSystemTick();
        // The first wait is for the output to open, which a real output can take seconds over.
        if (sound.steps > 0u) {
            const uint32_t gap = Microseconds(now - previous);
            sound.longest = gap > sound.longest ? gap : sound.longest;
            sound.late += gap > AUDIO_LATE_US ? 1u : 0u;
        }
        previous = now;
        Look(&sound.core, AUDIO_CORE, sound.steps);
        ++sound.steps;
        const bool over = __atomic_load_n(&audio_stop, __ATOMIC_ACQUIRE);
        const uint64_t over_at = __atomic_load_n(&frames_over_tick, __ATOMIC_ACQUIRE);
        sound.played = AudioVoicePlayed(0);
        const GameAudioNext next = GameAudioAfterStep(
            over, sound.played, over ? armTicksToNs(now - over_at) / 1000000u : 0u);
        if (next != GAME_AUDIO_GO_ON) {
            sound.timed_out = next == GAME_AUDIO_LATE;
            return;
        }
    }
}

static void FileMain(void *argument) {
    (void)argument;
    for (uint32_t k = 0; k < frames; ++k) {
        semaphoreWait(&file_go);
        Look(&reads.core, FILE_CORE, k);
        const uint32_t chunk = GameFileChunk(k);
        u64 got = 0;
        const uint64_t start = armGetSystemTick();
        const Result rc = fsFileRead(&big, (s64)chunk * GAME_FILE_CHUNK, file_buffer,
                                     GAME_FILE_CHUNK, FsReadOption_None, &got);
        const uint32_t took = Microseconds(armGetSystemTick() - start);
        reads.longest = took > reads.longest ? took : reads.longest;
        ++reads.reads;
        if (R_FAILED(rc) || got != GAME_FILE_CHUNK) {
            if (reads.failures++ == 0u) {
                reads.failed_at = k;
                reads.failed_rc = rc;
                reads.failed_got = got;
            }
        } else if (GameHashWords(file_buffer, GAME_FILE_CHUNK) != chunk_hash[chunk] &&
                   reads.wrong++ == 0u) {
            reads.wrong_at = k;
        }
    }
}

/// What the main thread asks the system each frame, as a game does, and what it judges of it:
/// hid's style set the same as it first said, holding the styles the program set, and a steady
/// clock that never goes back and never changes source.
static void AskServices(uint32_t k, PadState *pad, TimeSteadyClockTimePoint *last) {
    padUpdate(pad);
    u32 style = 0;
    const Result style_rc = hidGetSupportedNpadStyleSet(&style);
    if (k == 0u) {
        asked.first_style = style;
    }
    const bool style_right =
        R_SUCCEEDED(style_rc) && style == asked.first_style &&
        (style & HidNpadStyleSet_NpadStandard) == HidNpadStyleSet_NpadStandard;
    if (!style_right && asked.style_wrong++ == 0u) {
        asked.style_wrong_at = k;
        asked.style_wrong_rc = style_rc;
        asked.style_wrong_value = style;
    }
    TimeSteadyClockTimePoint point;
    memset(&point, 0, sizeof(point));
    const Result clock_rc = timeGetStandardSteadyClockTimePoint(&point);
    const bool failed = R_FAILED(clock_rc);
    const bool back = !failed && k > 0u && point.time_point < last->time_point;
    const bool moved_source =
        !failed && k > 0u && memcmp(&point.source_id, &last->source_id, sizeof(point.source_id));
    if ((failed || back || moved_source) &&
        asked.clock_failures + asked.clock_back + asked.clock_source_changes == 0u) {
        asked.clock_wrong_at = k;
        asked.clock_wrong_rc = clock_rc;
        asked.clock_before = last->time_point;
        asked.clock_after = point.time_point;
    }
    asked.clock_failures += failed ? 1u : 0u;
    asked.clock_back += back ? 1u : 0u;
    asked.clock_source_changes += moved_source ? 1u : 0u;
    if (!failed) {
        *last = point;
    }
    Look(&asked.core, MAIN_CORE, k);
}

static void MakeSwapchain(void) {
    swapchain = dkSwapchainCreate(&swap_maker);
}

/// Carves @p size bytes of scratch or says which part did not fit.
static void *Scratch(uint32_t size, uint32_t alignment, DkGpuAddr *address, const char *what,
                     char *why, size_t why_size) {
    const uint32_t used = gpu.data_used;
    void *const bytes = GpuAllocData(&gpu, size, alignment, address);
    if (bytes == NULL && why[0] == '\0') {
        snprintf(why, why_size, "no scratch left for %s: %u bytes asked, %u of the %u used",
                 what, (unsigned)size, (unsigned)used, (unsigned)gpu.spec.data_size);
    }
    return bytes;
}

/// The graphics side: shaders, the texture, the presented images, the scratch every frame reads.
static bool BringUpDrawing(char *why, size_t why_size) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, GAME_TARGET, GAME_TARGET);
    spec.depth_format = DkImageFormat_ZF32;
    spec.code_size = 64u * 1024u;
    spec.data_size = 4u * 1024u * 1024u;
    spec.image_size = 10u * 1024u * 1024u;
    spec.command_size = 512u * 1024u;
    if (!GpuOpenSpec(&gpu, &spec)) {
        snprintf(why, why_size, "the graphics device: %s", gpu.failure);
        return false;
    }
    const bool loaded =
        GpuLoadShader(&gpu, &common_vs, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&gpu, &flat_fs, ShaderData(SHADER_FLAT_FRAG), ShaderSize(SHADER_FLAT_FRAG)) &&
        GpuLoadShader(&gpu, &texture_fs, ShaderData(SHADER_TEXTURE_FRAG),
                      ShaderSize(SHADER_TEXTURE_FRAG)) &&
        GpuLoadShader(&gpu, &position_vs, ShaderData(SHADER_POSITION_VERT),
                      ShaderSize(SHADER_POSITION_VERT)) &&
        GpuLoadShader(&gpu, &ubo_fs, ShaderData(SHADER_VALUE_UBO_ALL_FRAG),
                      ShaderSize(SHADER_VALUE_UBO_ALL_FRAG));
    if (!loaded) {
        snprintf(why, why_size, "the shaders: %s", gpu.failure);
        return false;
    }
    bool images = GpuCreateTexture(&gpu, &texture, &texture_layout, DkImageFormat_RGBA8_Unorm,
                                   GAME_TEXTURE, GAME_TEXTURE);
    for (uint32_t i = 0; i < SWAP_IMAGES && images; ++i) {
        images = GpuCreateImage(&gpu, &swap_images[i], &swap_layouts[i],
                                DkImageFormat_RGBA8_Unorm, SWAP_WIDTH, SWAP_HEIGHT,
                                DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                                    DkImageFlags_HwCompression,
                                0, NULL);
    }
    if (!images) {
        snprintf(why, why_size, "the texture and the presented images: %s", gpu.failure);
        return false;
    }

    why[0] = '\0';
    staging_pitch = (GAME_TEXTURE * 4u + DK_IMAGE_LINEAR_STRIDE_ALIGNMENT - 1u) &
                    ~(DK_IMAGE_LINEAR_STRIDE_ALIGNMENT - 1u);
    for (uint32_t s = 0; s < SLOTS; ++s) {
        fill_cpu[s] = (GpuVertex *)Scratch(FILL_VERTICES * (uint32_t)sizeof(GpuVertex), 256u,
                                           &fill_gpu[s], "the fill quads", why, why_size);
        staging_cpu[s] = (uint8_t *)Scratch(staging_pitch * GAME_TEXTURE, DK_MEMBLOCK_ALIGNMENT,
                                            &staging_gpu[s], "the texels", why, why_size);
        ubo_cpu[s] = (uint8_t *)Scratch(GAME_DRAWS * GAME_UBO_BYTES, DK_UNIFORM_BUF_ALIGNMENT,
                                        &ubo_gpu[s], "the uniform buffers", why, why_size);
    }
    GpuVertex *const quad = (GpuVertex *)Scratch(6u * (uint32_t)sizeof(GpuVertex), 256u,
                                                 &texture_quad, "the texture quad", why, why_size);
    GpuVertex *const compose = (GpuVertex *)Scratch(
        6u * (uint32_t)sizeof(GpuVertex), 256u, &compose_quad, "the quad shown", why, why_size);
    float *const cell_positions = (float *)Scratch(CELL_VERTICES * 16u, 256u, &cells,
                                                   "the cells", why, why_size);
    uint32_t *const count = (uint32_t *)Scratch(DK_UNIFORM_BUF_ALIGNMENT,
                                                DK_UNIFORM_BUF_ALIGNMENT, &ubo_count,
                                                "the entry count", why, why_size);
    DkImageDescriptor *const image_descriptors = (DkImageDescriptor *)Scratch(
        2u * (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set,
        "the image descriptors", why, why_size);
    DkSamplerDescriptor *const sampler_descriptor = (DkSamplerDescriptor *)Scratch(
        (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set,
        "the sampler descriptor", why, why_size);
    if (why[0] != '\0') {
        return false;
    }

    static const uint8_t WHITE[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    // The top left quarter, one texel to four by four pixels; and the target shown as a square
    // in the middle of the display, 720 / 1280 of its width.
    Rectangle(quad, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, WHITE);
    Rectangle(compose, -0.5625f, 1.0f, 0.5625f, -1.0f, 0.0f, WHITE);
    for (uint32_t d = 0; d < GAME_DRAWS; ++d) {
        // A cell's edges are pixel edges, binary fractions of clip space, so no pixel centre is
        // near one and which pixels a cell covers is exact.
        const float x0 = (float)(d % GAME_CELLS) / 32.0f;
        const float x1 = x0 + 1.0f / 32.0f;
        const float y0 = -(float)(d / GAME_CELLS) / 32.0f;
        const float y1 = y0 - 1.0f / 32.0f;
        const float corners[6][4] = { { x0, y0, 0, 1 }, { x1, y0, 0, 1 }, { x1, y1, 0, 1 },
                                      { x0, y0, 0, 1 }, { x1, y1, 0, 1 }, { x0, y1, 0, 1 } };
        memcpy(cell_positions + d * 24u, corners, sizeof(corners));
    }
    memset(count, 0, DK_UNIFORM_BUF_ALIGNMENT);
    count[0] = GAME_UBO_ENTRIES;

    DkImageView view;
    dkImageViewDefaults(&view, &texture);
    dkImageDescriptorInitialize(&image_descriptors[0], &view, false, false);
    dkImageViewDefaults(&view, &gpu.image);
    dkImageDescriptorInitialize(&image_descriptors[1], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_None;
    sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
    dkSamplerDescriptorInitialize(sampler_descriptor, &sampler);
    return true;
}

/// The card's big file, made the first time as the fs suite makes it, and opened to read. What
/// opened is said in @p card_open and @p file_open, for the close.
static bool BringUpCard(char *why, size_t why_size, bool *card_open, bool *file_open) {
    Result rc = fsOpenSdCardFileSystem(&card);
    if (R_FAILED(rc)) {
        snprintf(why, why_size, "the memory card: 0x%x", (unsigned)rc);
        return false;
    }
    *card_open = true;
    if (!FsPrepareCard(&card, why, why_size)) {
        return false;
    }
    rc = fsFsOpenFile(&card, FS_BIG_PATH, FsOpenMode_Read, &big);
    if (R_FAILED(rc)) {
        snprintf(why, why_size, "opening %s: 0x%x", FS_BIG_PATH, (unsigned)rc);
        return false;
    }
    *file_open = true;
    return true;
}

/// The renderer, with the tone on voice 0 into the submix the aux records.
static bool BringUpSound(char *why, size_t why_size) {
    char error[160] = "";
    if (!AudioOpen(error, sizeof(error))) {
        snprintf(why, why_size, "the audio renderer: %s", error);
        return false;
    }
    int16_t *const tone = AudioAlloc(TONE_LOOP);
    if (tone == NULL) {
        snprintf(why, why_size, "no room in the renderer's memory pool for the tone's %u samples",
                 (unsigned)TONE_LOOP);
        AudioClose();
        return false;
    }
    AudioRefTone(tone, TONE_LOOP, TONE_HZ, TONE_LEVEL, 0u);
    AudioSync(tone, TONE_LOOP * sizeof(int16_t));
    AudioSubmixGain(0.0f);
    AudioCapture(true);
    AudioPlay(0, tone, TONE_LOOP, true, AUDIO_QUALITY_DEFAULT, AUDIO_SUBMIX, 1.0f, 0.0f);
    return true;
}

/// Each frame's four quarters and each chunk's bytes, before the first frame, so the render and
/// file threads only compare.
static void WorkOutAnswers(void) {
    static uint8_t pixels[GAME_IMAGE_BYTES];
    GameWorld *const at = &world;
    GameWorldStart(at);
    for (uint32_t k = 0; k < frames; ++k) {
        if (k > 0u) {
            GameWorldStep(at);
        }
        GameExpectedImage(at, pixels);
        GameRegionHashes(pixels, expected[k]);
    }
    const uint32_t used = frames < GAME_FILE_CHUNKS ? frames : GAME_FILE_CHUNKS;
    for (uint32_t k = 0; k < used; ++k) {
        chunk_hash[GameFileChunk(k)] = GameFileChunkHash(GameFileChunk(k));
    }
}

static Result StartThread(Thread *thread, ThreadFunc entry, int priority, int core) {
    Result rc = threadCreate(thread, entry, NULL, NULL, STACK_SIZE, priority, core);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(thread);
        if (R_FAILED(rc)) {
            threadClose(thread);
        }
    }
    return rc;
}

static void Finish(Thread *thread) {
    threadWaitForExit(thread);
    threadClose(thread);
}

/// Runs the frames: starts the three threads, drives the loop, and waits for all three to end.
/// False with the reason when a thread did not start, and then no frame ran.
static bool RunFrames(char *why, size_t why_size) {
    memset(&render, 0, sizeof(render));
    memset(&sound, 0, sizeof(sound));
    memset(&reads, 0, sizeof(reads));
    memset(&asked, 0, sizeof(asked));
    __atomic_store_n(&frames_over_tick, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&audio_stop, false, __ATOMIC_RELEASE);
    semaphoreInit(&frame_go, 0);
    semaphoreInit(&slot_free, SLOTS);
    semaphoreInit(&file_go, 0);

    Result rc = StartThread(&audio_thread, AudioMain, AUDIO_PRIORITY, AUDIO_CORE);
    if (R_FAILED(rc)) {
        snprintf(why, why_size, "the audio thread did not start: 0x%x", (unsigned)rc);
        return false;
    }
    rc = StartThread(&file_thread, FileMain, FILE_PRIORITY, FILE_CORE);
    if (R_FAILED(rc)) {
        snprintf(why, why_size, "the file thread did not start: 0x%x", (unsigned)rc);
        __atomic_store_n(&audio_stop, true, __ATOMIC_RELEASE);
        Finish(&audio_thread);
        return false;
    }
    rc = StartThread(&render_thread, RenderMain, RENDER_PRIORITY, RENDER_CORE);
    if (R_FAILED(rc)) {
        snprintf(why, why_size, "the render thread did not start: 0x%x", (unsigned)rc);
        for (uint32_t k = 0; k < frames; ++k) {
            semaphoreSignal(&file_go);
        }
        Finish(&file_thread);
        __atomic_store_n(&audio_stop, true, __ATOMIC_RELEASE);
        Finish(&audio_thread);
        return false;
    }

    // This thread on core 0 alone for the frames, whatever core it was on, and back after.
    s32 ideal = 0;
    u64 mask = 0;
    asked.get_mask_rc = svcGetThreadCoreMask(&ideal, &mask, CUR_THREAD_HANDLE);
    asked.set_mask_rc = R_SUCCEEDED(asked.get_mask_rc)
                            ? svcSetThreadCoreMask(CUR_THREAD_HANDLE, MAIN_CORE, 1u << MAIN_CORE)
                            : asked.get_mask_rc;
    asked.pinned = R_SUCCEEDED(asked.set_mask_rc);
    asked.ideal = ideal;
    asked.mask = mask;
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    TimeSteadyClockTimePoint last;
    memset(&last, 0, sizeof(last));
    GameWorldStart(&world);
    for (uint32_t k = 0; k < frames; ++k) {
        semaphoreWait(&slot_free);
        const uint64_t began = armGetSystemTick();
        AskServices(k, &pad, &last);
        if (k > 0u) {
            GameWorldStep(&world);
        }
        WriteSlot(k % SLOTS, &world);
        semaphoreSignal(&file_go);
        main_us[k] = Microseconds(armGetSystemTick() - began);
        semaphoreSignal(&frame_go);
    }
    Finish(&render_thread);
    Finish(&file_thread);
    __atomic_store_n(&frames_over_tick, armGetSystemTick(), __ATOMIC_RELEASE);
    __atomic_store_n(&audio_stop, true, __ATOMIC_RELEASE);
    Finish(&audio_thread);
    if (asked.pinned) {
        asked.restore_rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, ideal, (u32)mask);
        if (R_FAILED(asked.restore_rc)) {
            ReportNote("game: the main thread could not be put back on core %d, mask 0x%llx: "
                       "0x%x, so it stays on core %d alone",
                       (int)ideal, (unsigned long long)mask, (unsigned)asked.restore_rc,
                       MAIN_CORE);
        }
    }
    return true;
}

/// The picture of frame @p k, the first wrong one: what it drew against what it should have.
static void ShowFrame(uint32_t k) {
    GameWorld *const at = &world;
    GameWorldStart(at);
    for (uint32_t i = 0; i < k; ++i) {
        GameWorldStep(at);
    }
    GameExpectedImage(at, want_pixels);
    Picture *const picture = PictureBegin(GAME_TARGET, GAME_TARGET, PICTURE_COLOUR, wrong_pixels);
    for (uint32_t y = 0; y < GAME_TARGET; ++y) {
        for (uint32_t x = 0; x < GAME_TARGET; ++x) {
            const size_t at_byte = ((size_t)y * GAME_TARGET + x) * 4u;
            PictureSet(picture, x, y, want_pixels + at_byte,
                       memcmp(wrong_pixels + at_byte, want_pixels + at_byte, 4u) == 0
                           ? PICTURE_MATCHED
                           : PICTURE_WRONG);
        }
    }
}

static void JudgeImage(const char *name, bool ran, const char *why) {
    if (!ran) {
        ReportCase(SUITE, name, CHECK_NONE, false, why);
        return;
    }
    if (render.presented < frames) {
        char detail[320];
        snprintf(detail, sizeof(detail), "%u of %u frames were presented; %s",
                 (unsigned)render.presented, (unsigned)frames,
                 gpu.failure[0] != '\0' ? gpu.failure : "the graphics queue reported no fault");
        ReportCase(SUITE, name, CHECK_NONE, false, detail);
        return;
    }
    if (render.wrong_frames == 0u) {
        ReportCase(SUITE, name, CHECK_EXACT, true, "");
        return;
    }
    ShowFrame(render.first_wrong);
    char wrong[160] = "";
    for (uint32_t r = 0; r < GAME_REGIONS; ++r) {
        if (render.got[r] == expected[render.first_wrong][r]) {
            continue;
        }
        const size_t used = strlen(wrong);
        snprintf(wrong + used, sizeof(wrong) - used, "%s%s", used > 0u ? ", " : "",
                 GameRegionName((GameRegion)r));
    }
    char detail[320];
    snprintf(detail, sizeof(detail),
             "%u of %u frames drew another image; the first, frame %u, in %s (its picture is "
             "that frame)",
             (unsigned)render.wrong_frames, (unsigned)frames, (unsigned)render.first_wrong, wrong);
    ReportCase(SUITE, name, CHECK_EXACT, false, detail);
}

static void JudgeSound(const char *name) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const double want = AudioRefResampledLevel(4u, 1.0, TONE_HZ, TONE_LEVEL);
    if (sound.failed) {
        ReportCase(SUITE, name, CHECK_NONE, false, AudioError());
        return;
    }
    if (sound.timed_out) {
        char detail[240];
        snprintf(detail, sizeof(detail),
                 "the voice had played %llu samples, short of the %u the level is read after, "
                 "%u ms after the frames ended, over %u renderer frames",
                 (unsigned long long)sound.played, (unsigned)GAME_AUDIO_SETTLE,
                 (unsigned)GAME_AUDIO_LIMIT_MS, (unsigned)sound.steps);
        ReportCase(SUITE, name, CHECK_NONE, false, detail);
        return;
    }
    static int32_t captured[40u * AUDIO_FRAME];
    size_t ring_frames = 0;
    const int32_t *const ring = AudioCaptureRing(&ring_frames);
    AudioRefChannel32(ring, ring_frames, 2u, AUDIO_FRAME, 0u, captured);
    const double level = AudioRefLevel32(captured, ring_frames * AUDIO_FRAME, TONE_HZ);
    CHECK_TRUE(SUITE, name, CHECK_LEVEL, AudioRefWithin(level, want, 0.03),
               "1 kHz at %.1f of full scale came out at %.4f (%+.2f dB), the model gives %.4f; "
               "the voice played %llu samples over %u renderer frames",
               TONE_LEVEL, level, AudioRefDecibels(level, want), want,
               (unsigned long long)AudioVoicePlayed(0), (unsigned)sound.steps);
}

static void JudgeReads(const char *name) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    char failed[160] = "none";
    if (reads.failures > 0u) {
        snprintf(failed, sizeof(failed), "%u, the first at frame %u: 0x%x with %llu bytes",
                 (unsigned)reads.failures, (unsigned)reads.failed_at, (unsigned)reads.failed_rc,
                 (unsigned long long)reads.failed_got);
    }
    char wrong[160] = "none";
    if (reads.wrong > 0u) {
        const uint32_t chunk = GameFileChunk(reads.wrong_at);
        snprintf(wrong, sizeof(wrong), "%u, the first at frame %u, offset 0x%x",
                 (unsigned)reads.wrong, (unsigned)reads.wrong_at,
                 (unsigned)(chunk * GAME_FILE_CHUNK));
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT,
               reads.reads == frames && reads.failures == 0u && reads.wrong == 0u,
               "%u reads of %u; failed reads %s; reads with other bytes %s", (unsigned)reads.reads,
               (unsigned)frames, failed, wrong);
}

static void JudgeServices(const char *name) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    char style[200] = "";
    if (asked.style_wrong > 0u) {
        snprintf(style, sizeof(style),
                 "; %u frames another answer, the first frame %u: 0x%x, rc 0x%x",
                 (unsigned)asked.style_wrong, (unsigned)asked.style_wrong_at,
                 (unsigned)asked.style_wrong_value, (unsigned)asked.style_wrong_rc);
    }
    char clock[240] = "";
    if (asked.clock_failures + asked.clock_back + asked.clock_source_changes > 0u) {
        snprintf(clock, sizeof(clock),
                 "; %u failed, %u went back, %u on another source, the first at frame %u: rc "
                 "0x%x, %lld s after %lld s",
                 (unsigned)asked.clock_failures, (unsigned)asked.clock_back,
                 (unsigned)asked.clock_source_changes, (unsigned)asked.clock_wrong_at,
                 (unsigned)asked.clock_wrong_rc, (long long)asked.clock_after,
                 (long long)asked.clock_before);
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT,
               asked.style_wrong == 0u &&
                   asked.clock_failures + asked.clock_back + asked.clock_source_changes == 0u,
               "hid's style set 0x%x the first frame, the program set 0x%x%s; the steady "
               "clock%s",
               (unsigned)asked.first_style, (unsigned)HidNpadStyleSet_NpadStandard, style,
               clock[0] != '\0' ? clock : " never failed, went back or changed source");
}

static void DescribeCore(char *out, size_t out_size, const char *thread, const CoreSeen *seen,
                         uint32_t core) {
    const size_t used = strlen(out);
    if (seen->wrong == 0u) {
        snprintf(out + used, out_size - used, "%s%s on core %u all %u times", used > 0u ? "; " : "",
                 thread, (unsigned)core, (unsigned)seen->looks);
        return;
    }
    snprintf(out + used, out_size - used, "%s%s on core %u in %u of %u looks, the first at %u",
             used > 0u ? "; " : "", thread, (unsigned)seen->wrong_core, (unsigned)seen->wrong,
             (unsigned)seen->looks, (unsigned)seen->wrong_at);
}

static void JudgeCores(const char *name) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    char detail[400] = "";
    if (!asked.pinned) {
        snprintf(detail, sizeof(detail),
                 "the main thread could not be put on core 0: svcGetThreadCoreMask 0x%x, "
                 "svcSetThreadCoreMask 0x%x",
                 (unsigned)asked.get_mask_rc, (unsigned)asked.set_mask_rc);
    }
    DescribeCore(detail, sizeof(detail), "main", &asked.core, MAIN_CORE);
    DescribeCore(detail, sizeof(detail), "render", &render.core, RENDER_CORE);
    DescribeCore(detail, sizeof(detail), "audio", &sound.core, AUDIO_CORE);
    DescribeCore(detail, sizeof(detail), "file", &reads.core, FILE_CORE);
    CHECK_TRUE(SUITE, name, CHECK_EXACT,
               asked.pinned && asked.core.wrong == 0u && render.core.wrong == 0u &&
                   sound.core.wrong == 0u && reads.core.wrong == 0u &&
                   asked.core.looks == frames && render.core.looks == frames &&
                   reads.core.looks == frames && sound.core.looks > 0u,
               "%s", detail);
}

static GameTimeCase time_cases[GAME_TIME_CASES];

/// The timed cases and the note: frame 0 left out as the warm-up, each measure's cases carrying
/// its number as their time. A run of one frame has none past the warm-up, and its timed cases
/// are skipped saying so.
static void Times(void) {
    static GameTimes times;
    GameTimesOf(&times, frames, frame_us, main_us, render_us, sorted_us);
    const GameThreadTimes threads = { sound.steps, sound.longest, sound.late, reads.reads,
                                      reads.longest };
    times.threads = threads;
    const uint32_t timed = GameTimeCases(&times, frames, time_cases);
    char detail[160];
    snprintf(detail, sizeof(detail), "%u frames past the warm-up; %u of %u presented",
             (unsigned)timed, (unsigned)render.presented, (unsigned)frames);
    for (uint32_t i = 0; i < GAME_TIME_CASES; ++i) {
        if (timed == 0u) {
            ReportSkip(SUITE, time_cases[i].name, REPORT_REASON_DEPENDS,
                       "the run's one frame is the warm-up, which the times leave out");
        } else if (ReportStart(SUITE, time_cases[i].name)) {
            ReportCaseTime(SUITE, time_cases[i].name, CHECK_NONE, render.presented == frames,
                           detail, time_cases[i].us);
        }
    }
    char note[REPORT_NOTE_MAX + 1];
    GameDescribeFrames(note, sizeof(note), &times);
    ReportNote("%s", note);
    GameDescribeWork(note, sizeof(note), &times);
    ReportNote("%s", note);
}

/// Every case after the bring-up, skipped as depending on what did not happen: @p why, which
/// names it.
static void SkipFrom(const char *const names[], size_t count, const char *why) {
    for (size_t i = 0; i < count; ++i) {
        ReportSkip(SUITE, names[i], REPORT_REASON_DEPENDS, why);
    }
    for (uint32_t i = 0; timing && i < GAME_TIME_CASES; ++i) {
        ReportSkip(SUITE, time_cases[i].name, REPORT_REASON_DEPENDS, why);
    }
}

void RunGame(void) {
    static char bring_up[] = "a graphics device, a way to the display, the renderer and the "
                             "card's file come up";
    static char image_name[96];
    static char reads_name[96];
    static char services_name[96];
    static const char SOUND_NAME[] = "the audio thread's tone keeps the level the renderer's "
                                     "model gives";
    static const char CORES_NAME[] = "the main, render, audio and file threads run on the cores "
                                     "they were given";
    snprintf(image_name, sizeof(image_name),
             "%u frames draw the image the processor works out, every frame", (unsigned)frames);
    snprintf(reads_name, sizeof(reads_name),
             "the file thread's %u reads of 64 KiB hold the file's bytes", (unsigned)frames);
    snprintf(services_name, sizeof(services_name),
             "hid and the steady clock answer on each of %u frames", (unsigned)frames);
    (void)GameTimeCases(NULL, frames, time_cases);
    const char *const all[] = { image_name, SOUND_NAME, reads_name, services_name, CORES_NAME };
    const size_t after_count = sizeof(all) / sizeof(all[0]) - 1u;

    if (!ReportStart(SUITE, bring_up)) {
        // Its skip line says why; nothing after it has a scene to judge.
        SkipFrom(all, sizeof(all) / sizeof(all[0]),
                 "the scene's bring-up did not run, so neither did its frames");
        return;
    }
    char why[400] = "";
    bool card_open = false;
    bool file_open = false;
    bool sound_open = false;
    bool up = BringUpDrawing(why, sizeof(why)) &&
              BringUpCard(why, sizeof(why), &card_open, &file_open);
    if (up) {
        sound_open = up = BringUpSound(why, sizeof(why));
    }
    if (up) {
        // The window is the program's screen's while it is up, and stays so: closing it before
        // a swapchain, or making it again after one, is what Eden never came back from.
        on_screen = ViewerScreenUp();
        if (on_screen) {
            ReportNote("game: the program's screen is up, so the frames are shown through it, "
                       "composed by the processor, rather than through a swapchain; present to "
                       "present is that screen's, not a quick run's");
        } else {
            const DkImage *const presented[SWAP_IMAGES] = { &swap_images[0], &swap_images[1] };
            dkSwapchainMakerDefaults(&swap_maker, gpu.device, nwindowGetDefault(), presented,
                                     SWAP_IMAGES);
            uint32_t aborted = 0;
            up = AbortGuarded(MakeSwapchain, &aborted) && swapchain != NULL;
            if (!up) {
                snprintf(why, sizeof(why), "the swapchain on the default window: 0x%x",
                         (unsigned)aborted);
            }
        }
    }
    if (up) {
        WorkOutAnswers();
    }
    CHECK_TRUE(SUITE, bring_up, CHECK_NONE, up, "%s", why);

    bool ran = false;
    if (!up) {
        SkipFrom(all, sizeof(all) / sizeof(all[0]),
                 "the scene did not come up, so its frames did not run");
    } else if (ReportStart(SUITE, image_name)) {
        ran = RunFrames(why, sizeof(why));
        JudgeImage(image_name, ran, why);
        if (ran) {
            JudgeSound(SOUND_NAME);
            JudgeReads(reads_name);
            JudgeServices(services_name);
            JudgeCores(CORES_NAME);
            if (timing) {
                Times();
            }
        } else {
            SkipFrom(all + 1, after_count, why);
        }
    } else {
        // The frames case is skipped, its line says why (it ended the program before).
        SkipFrom(all + 1, after_count,
                 "the frames' case did not run, so there are no frames to judge");
    }

    if (swapchain != NULL) {
        dkQueueWaitIdle(gpu.queue);
        dkSwapchainDestroy(swapchain);
        swapchain = NULL;
    }
    GpuClose(&gpu);
    if (sound_open) {
        AudioCapture(false);
        AudioStop(0);
        AudioStep();
        if (AudioError()[0] != '\0') {
            ReportNote("game: the renderer said: %s", AudioError());
        }
        AudioClose();
    }
    if (file_open) {
        fsFileClose(&big);
    }
    if (card_open) {
        fsFsClose(&card);
    }
}
