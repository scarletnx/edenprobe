// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "../check.h"
#include "../nvnlayer.h"
#include "../picture.h"
#include "../report.h"
#include "../viewer.h"
#include "tests.h"

// The nvn family: the clear suite's question asked through NVN, as a game asks it, with the
// layer of nvnlayer.c as the driver. On the console and in Eden the layer records deko3d
// commands and the graphics unit answers; under ScarletNX, ScarletNX's own NVN takes every
// function the case looks up and answers instead. Everything goes through the functions the
// lookup handed back, never through the layer's code by name, so the case is the same program on
// every machine and only the NVN under it changes.
//
// Exact tier for the pixels, as in the clear suite: a normalised target, constant clear values,
// no shader. The bring-up and the window are the none tier: they compare nothing, a failure says
// which step stopped and why.

static const char *const SUITE = "nvn";

#define TARGET_SIZE 64u
#define TARGET_BYTES (TARGET_SIZE * TARGET_SIZE * 4u)
// Command memory for every list the suite records; NVN, like deko3d, takes it in a pool that is
// uncached to the processor and cached to the graphics unit.
#define COMMAND_BYTES 0x10000u
// What the processor-side control memory NVN asks of a command buffer comes to here: a page.
#define CONTROL_BYTES 0x1000u
// The window's textures: the console's own screen size, two of them, NVN's least.
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define WINDOW_TEXTURES 2
// A pool's storage is a whole number of these, on a boundary of one: NVN's page and deko3d's.
#define POOL_PAGE 0x1000u
// What the readback holds until the copy writes it: no clear colour of this suite is it.
#define READ_PRESET 0xa5u

// Everything NVN's objects need, in one place that outlives a case: NVN objects are large (the
// device alone is 12 KiB) and must sit on an eight byte boundary, which nvn/nvn.h does not ask
// the compiler for.
static struct {
    NvnProcs nvn;
    bool device_up;
    bool queue_up;
    bool command_up;
    bool target_up;
    _Alignas(8) NVNdevice device;
    _Alignas(8) NVNqueue queue;
    _Alignas(8) NVNmemoryPool command_pool;
    _Alignas(8) NVNmemoryPool target_pool;
    _Alignas(8) NVNmemoryPool read_pool;
    _Alignas(8) NVNcommandBuffer command;
    _Alignas(8) NVNtexture target;
    bool command_pool_up;
    bool target_pool_up;
    bool read_pool_up;
    void *command_storage;
    void *target_storage;
    void *read_storage;
    void *control;
} nvn;

/// The reason a step failed: the layer's, when it gave one, else @p fallback. Under ScarletNX the
/// layer never runs, so the fallback is all a failure there carries.
static const char *Why(const char *fallback) {
    return NvnLayerError()[0] != '\0' ? NvnLayerError() : fallback;
}

static size_t RoundUp(size_t value, size_t to) {
    return (value + to - 1u) / to * to;
}

/// A pool of @p size bytes (a whole number of pages) with @p flags, over storage of its own.
static bool MakePool(NVNmemoryPool *pool, void **storage, size_t size, int flags) {
    *storage = aligned_alloc(POOL_PAGE, size);
    if (*storage == NULL) {
        return false;
    }
    memset(*storage, 0, size);
    _Alignas(8) NVNmemoryPoolBuilder builder;
    nvn.nvn.nvnMemoryPoolBuilderSetDefaults(&builder);
    nvn.nvn.nvnMemoryPoolBuilderSetDevice(&builder, &nvn.device);
    nvn.nvn.nvnMemoryPoolBuilderSetStorage(&builder, *storage, size);
    nvn.nvn.nvnMemoryPoolBuilderSetFlags(&builder, flags);
    if (!nvn.nvn.nvnMemoryPoolInitialize(pool, &builder)) {
        free(*storage);
        *storage = NULL;
        return false;
    }
    return true;
}

static void DropPool(NVNmemoryPool *pool, void **storage, bool *up) {
    if (*up) {
        nvn.nvn.nvnMemoryPoolFinalize(pool);
        *up = false;
    }
    free(*storage);
    *storage = NULL;
}

/// A builder for a 2D RGBA8 texture of @p width by @p height with @p flags, on the device.
static void TextureBuilder(NVNtextureBuilder *builder, int width, int height, int flags) {
    nvn.nvn.nvnTextureBuilderSetDevice(builder, &nvn.device);
    nvn.nvn.nvnTextureBuilderSetDefaults(builder);
    nvn.nvn.nvnTextureBuilderSetFlags(builder, flags);
    nvn.nvn.nvnTextureBuilderSetTarget(builder, NVN_TEXTURE_TARGET_2D);
    nvn.nvn.nvnTextureBuilderSetFormat(builder, NVN_FORMAT_RGBA8);
    nvn.nvn.nvnTextureBuilderSetSize2D(builder, width, height);
}

/// The queue, the pools, the command buffer and the 64 by 64 target, in that order; false with
/// the step in @p why.
static bool BringUp(char *why, size_t why_size) {
    _Alignas(8) NVNqueueBuilder queue_builder;
    nvn.nvn.nvnQueueBuilderSetDevice(&queue_builder, &nvn.device);
    nvn.nvn.nvnQueueBuilderSetDefaults(&queue_builder);
    if (!(nvn.queue_up = nvn.nvn.nvnQueueInitialize(&nvn.queue, &queue_builder))) {
        snprintf(why, why_size, "nvnQueueInitialize: %s", Why("false"));
        return false;
    }
    nvn.command_pool_up =
        MakePool(&nvn.command_pool, &nvn.command_storage, COMMAND_BYTES,
                 NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    if (!nvn.command_pool_up) {
        snprintf(why, why_size, "the command pool: %s", Why("no memory, or false"));
        return false;
    }
    nvn.read_pool_up =
        MakePool(&nvn.read_pool, &nvn.read_storage, RoundUp(TARGET_BYTES, POOL_PAGE),
                 NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    if (!nvn.read_pool_up) {
        snprintf(why, why_size, "the readback pool: %s", Why("no memory, or false"));
        return false;
    }

    _Alignas(8) NVNtextureBuilder texture_builder;
    memset(&texture_builder, 0, sizeof(texture_builder));
    TextureBuilder(&texture_builder, TARGET_SIZE, TARGET_SIZE, 0);
    const size_t size = nvn.nvn.nvnTextureBuilderGetStorageSize(&texture_builder);
    const size_t alignment = nvn.nvn.nvnTextureBuilderGetStorageAlignment(&texture_builder);
    if (size == 0u || alignment == 0u || alignment > POOL_PAGE) {
        snprintf(why, why_size, "the target's storage is %zu bytes at an alignment of %zu: %s",
                 size, alignment, Why("an answer no pool here can hold"));
        return false;
    }
    nvn.target_pool_up =
        MakePool(&nvn.target_pool, &nvn.target_storage, RoundUp(size, POOL_PAGE),
                 NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    if (!nvn.target_pool_up) {
        snprintf(why, why_size, "the target's pool: %s", Why("no memory, or false"));
        return false;
    }
    nvn.nvn.nvnTextureBuilderSetStorage(&texture_builder, &nvn.target_pool, 0);
    if (!(nvn.target_up = nvn.nvn.nvnTextureInitialize(&nvn.target, &texture_builder))) {
        snprintf(why, why_size, "nvnTextureInitialize: %s", Why("false"));
        return false;
    }

    if (!(nvn.command_up = nvn.nvn.nvnCommandBufferInitialize(&nvn.command, &nvn.device))) {
        snprintf(why, why_size, "nvnCommandBufferInitialize: %s", Why("false"));
        return false;
    }
    nvn.control = aligned_alloc(8u, CONTROL_BYTES);
    if (nvn.control == NULL) {
        snprintf(why, why_size, "no memory for the command buffer's control memory");
        return false;
    }
    nvn.nvn.nvnCommandBufferAddCommandMemory(&nvn.command, &nvn.command_pool, 0, COMMAND_BYTES);
    nvn.nvn.nvnCommandBufferAddControlMemory(&nvn.command, nvn.control, CONTROL_BYTES);
    return true;
}

static void TearDown(void) {
    if (nvn.queue_up) {
        nvn.nvn.nvnQueueFinish(&nvn.queue);
    }
    if (nvn.command_up) {
        nvn.nvn.nvnCommandBufferFinalize(&nvn.command);
        nvn.command_up = false;
    }
    free(nvn.control);
    nvn.control = NULL;
    if (nvn.target_up) {
        nvn.nvn.nvnTextureFinalize(&nvn.target);
        nvn.target_up = false;
    }
    DropPool(&nvn.target_pool, &nvn.target_storage, &nvn.target_pool_up);
    DropPool(&nvn.read_pool, &nvn.read_storage, &nvn.read_pool_up);
    DropPool(&nvn.command_pool, &nvn.command_storage, &nvn.command_pool_up);
    if (nvn.queue_up) {
        nvn.nvn.nvnQueueFinalize(&nvn.queue);
        nvn.queue_up = false;
    }
    if (nvn.device_up) {
        nvn.nvn.nvnDeviceFinalize(&nvn.device);
        nvn.device_up = false;
    }
}

/// Records into the open command buffer: @p texture as the only colour target, with a viewport
/// and a scissor over all @p width by @p height of it, cleared to @p rgba.
static void RecordClear(const NVNtexture *texture, int width, int height, const float rgba[4]) {
    const NVNtexture *const colors[1] = { texture };
    nvn.nvn.nvnCommandBufferSetRenderTargets(&nvn.command, 1, colors, NULL, NULL, NULL);
    nvn.nvn.nvnCommandBufferSetViewport(&nvn.command, 0, 0, width, height);
    nvn.nvn.nvnCommandBufferSetScissor(&nvn.command, 0, 0, width, height);
    nvn.nvn.nvnCommandBufferClearColor(&nvn.command, 0, rgba, NVN_CLEAR_COLOR_MASK_RGBA);
}

/// Clears the target to @p rgba, copies it to the readback pool and waits: the pool's bytes, or
/// NULL with the reason in @p why.
static const uint8_t *ClearAndRead(const float rgba[4], char *why, size_t why_size) {
    uint8_t *const pixels = nvn.nvn.nvnMemoryPoolMap(&nvn.read_pool);
    if (pixels == NULL) {
        snprintf(why, why_size, "nvnMemoryPoolMap gave no address for the readback pool: %s",
                 Why("NULL"));
        return NULL;
    }
    // A copy that never lands leaves these, which no clear colour here produces.
    memset(pixels, READ_PRESET, TARGET_BYTES);

    nvn.nvn.nvnCommandBufferBeginRecording(&nvn.command);
    RecordClear(&nvn.target, TARGET_SIZE, TARGET_SIZE, rgba);
    // The clear ran on the graphics engine and the copy reads its result: an NVN program orders
    // the two, and asks for the texture cache to be dropped too.
    nvn.nvn.nvnCommandBufferBarrier(&nvn.command, NVN_BARRIER_ORDER_FRAGMENTS_BIT |
                                                      NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    const NVNcopyRegion region = { 0, 0, 0, TARGET_SIZE, TARGET_SIZE, 1 };
    nvn.nvn.nvnCommandBufferCopyTextureToBuffer(&nvn.command, &nvn.target, NULL, &region,
                                                nvn.nvn.nvnMemoryPoolGetBufferAddress(
                                                    &nvn.read_pool),
                                                NVN_COPY_FLAGS_NONE);
    const NVNcommandHandle list = nvn.nvn.nvnCommandBufferEndRecording(&nvn.command);
    if (list == 0u) {
        snprintf(why, why_size, "nvnCommandBufferEndRecording gave no list: %s", Why("0"));
        return NULL;
    }
    nvn.nvn.nvnQueueSubmitCommands(&nvn.queue, 1, &list);
    nvn.nvn.nvnQueueFinish(&nvn.queue);
    if (NvnLayerError()[0] != '\0') {
        snprintf(why, why_size, "%s", NvnLayerError());
        return NULL;
    }
    return pixels;
}

/// One case: the target cleared to @p rgba must read back as @p expected in every pixel.
static void ClearCase(const char *name, const float rgba[4], const uint8_t expected[4]) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    NvnLayerClearError();
    char why[256];
    const uint8_t *const pixels = ClearAndRead(rgba, why, sizeof(why));
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false, why);
        return;
    }
    Picture *const picture = PictureBegin(TARGET_SIZE, TARGET_SIZE, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t first[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < TARGET_SIZE; ++y) {
        for (uint32_t x = 0; x < TARGET_SIZE; ++x) {
            const uint8_t *const pixel = pixels + ((size_t)y * TARGET_SIZE + x) * 4u;
            const bool right = memcmp(pixel, expected, 4) == 0;
            PictureSet(picture, x, y, expected, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right && wrong++ == 0u) {
                first_x = x;
                first_y = y;
                memcpy(first, pixel, sizeof(first));
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u,
               "%u of %u pixels wrong, first at (%u,%u) is %02x%02x%02x%02x, expected "
               "%02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)(TARGET_SIZE * TARGET_SIZE), (unsigned)first_x,
               (unsigned)first_y, first[0], first[1], first[2], first[3], expected[0],
               expected[1], expected[2], expected[3]);
}

/// Two textures on the default window, each acquired, cleared and presented once. Nothing is read
/// back: what reaches the screen is the compositor's, and the case says only whether every step
/// came back as NVN promises. False with the step in @p why.
static bool Present(char *why, size_t why_size) {
    _Alignas(8) NVNtextureBuilder builder;
    memset(&builder, 0, sizeof(builder));
    TextureBuilder(&builder, WINDOW_WIDTH, WINDOW_HEIGHT, NVN_TEXTURE_FLAGS_DISPLAY);
    const size_t size = nvn.nvn.nvnTextureBuilderGetStorageSize(&builder);
    const size_t alignment = nvn.nvn.nvnTextureBuilderGetStorageAlignment(&builder);
    if (size == 0u || alignment == 0u || alignment > POOL_PAGE) {
        snprintf(why, why_size, "a window texture's storage is %zu bytes at an alignment of %zu: "
                 "%s",
                 size, alignment, Why("an answer no pool here can hold"));
        return false;
    }
    const size_t stride = RoundUp(size, alignment);
    static _Alignas(8) NVNmemoryPool pool;
    static _Alignas(8) NVNtexture textures[WINDOW_TEXTURES];
    static _Alignas(8) NVNwindow window;
    void *storage = NULL;
    bool pool_up = MakePool(&pool, &storage, RoundUp(stride * WINDOW_TEXTURES, POOL_PAGE),
                            NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS |
                                NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    if (!pool_up) {
        snprintf(why, why_size, "the window's pool: %s", Why("no memory, or false"));
        return false;
    }
    bool ok = true;
    int made = 0;
    for (; made < WINDOW_TEXTURES; ++made) {
        nvn.nvn.nvnTextureBuilderSetStorage(&builder, &pool, (ptrdiff_t)(stride * (size_t)made));
        if (!nvn.nvn.nvnTextureInitialize(&textures[made], &builder)) {
            snprintf(why, why_size, "window texture %d: nvnTextureInitialize: %s", made,
                     Why("false"));
            ok = false;
            break;
        }
    }
    bool window_up = false;
    if (ok) {
        NVNtexture *const list[WINDOW_TEXTURES] = { &textures[0], &textures[1] };
        _Alignas(8) NVNwindowBuilder window_builder;
        nvn.nvn.nvnWindowBuilderSetDefaults(&window_builder);
        nvn.nvn.nvnWindowBuilderSetDevice(&window_builder, &nvn.device);
        nvn.nvn.nvnWindowBuilderSetNativeWindow(&window_builder, nwindowGetDefault());
        nvn.nvn.nvnWindowBuilderSetTextures(&window_builder, WINDOW_TEXTURES, list);
        window_up = ok = nvn.nvn.nvnWindowInitialize(&window, &window_builder);
        if (!ok) {
            snprintf(why, why_size, "nvnWindowInitialize: %s", Why("false"));
        }
    }
    // Two frames, so that both textures go round once: a cleared green, then a cleared blue.
    static const float colours[WINDOW_TEXTURES][4] = { { 0.0f, 1.0f, 0.0f, 1.0f },
                                                       { 0.0f, 0.0f, 1.0f, 1.0f } };
    for (int frame = 0; ok && frame < WINDOW_TEXTURES; ++frame) {
        int index = -1;
        const NVNqueueAcquireTextureResult got =
            nvn.nvn.nvnQueueAcquireTexture(&nvn.queue, &window, &index);
        if (got != NVN_QUEUE_ACQUIRE_TEXTURE_RESULT_SUCCESS || index < 0 ||
            index >= WINDOW_TEXTURES) {
            snprintf(why, why_size, "frame %d: nvnQueueAcquireTexture answered %d with texture "
                     "%d of %d: %s",
                     frame, (int)got, index, WINDOW_TEXTURES, Why("no reason given"));
            ok = false;
            break;
        }
        nvn.nvn.nvnCommandBufferBeginRecording(&nvn.command);
        RecordClear(&textures[index], WINDOW_WIDTH, WINDOW_HEIGHT, colours[frame]);
        const NVNcommandHandle list = nvn.nvn.nvnCommandBufferEndRecording(&nvn.command);
        nvn.nvn.nvnQueueSubmitCommands(&nvn.queue, 1, &list);
        nvn.nvn.nvnQueuePresentTexture(&nvn.queue, &window, index);
    }
    nvn.nvn.nvnQueueFinish(&nvn.queue);
    if (ok && NvnLayerError()[0] != '\0') {
        snprintf(why, why_size, "%s", NvnLayerError());
        ok = false;
    }
    if (window_up) {
        nvn.nvn.nvnWindowFinalize(&window);
    }
    for (int i = 0; i < made; ++i) {
        nvn.nvn.nvnTextureFinalize(&textures[i]);
    }
    DropPool(&pool, &storage, &pool_up);
    return ok;
}

void RunNvn(void) {
    static const char *const RESOLVE = "the program's NVN functions resolve";
    static const char *const DEVICE = "the device initializes";
    static const char *const VERSION = "the device answers the layer's API version";
    static const char *const TARGET = "a queue, its pools and a 64x64 target come up";
    static const char *const RED = "clear to opaque red";
    static const char *const MID = "clear to a mid channel value";
    static const char *const WINDOW = "a window acquires and presents both its textures";
    const char *const AFTER_DEVICE[] = { VERSION, TARGET, RED, MID, WINDOW };
    const size_t after_count = sizeof(AFTER_DEVICE) / sizeof(AFTER_DEVICE[0]);

    memset(&nvn, 0, sizeof(nvn));
    NvnLayerClearError();
    if (!ReportStart(SUITE, RESOLVE)) {
        return;
    }
    char missing[64] = "";
    const bool resolved = NvnLoadProcs(&nvn.nvn, missing, sizeof(missing));
    CHECK_TRUE(SUITE, RESOLVE, CHECK_NONE, resolved, "the lookup gave no %s", missing);
    if (!resolved) {
        ReportSkip(SUITE, DEVICE, REPORT_REASON_DEPENDS,
                   "the program's NVN functions did not resolve");
        for (size_t i = 0; i < after_count; ++i) {
            ReportSkip(SUITE, AFTER_DEVICE[i], REPORT_REASON_DEPENDS,
                       "the program's NVN functions did not resolve");
        }
        return;
    }

    if (!ReportStart(SUITE, DEVICE)) {
        return;
    }
    _Alignas(8) NVNdeviceBuilder device_builder;
    nvn.nvn.nvnDeviceBuilderSetDefaults(&device_builder);
    nvn.nvn.nvnDeviceBuilderSetFlags(&device_builder, 0);
    nvn.device_up = nvn.nvn.nvnDeviceInitialize(&nvn.device, &device_builder);
    CHECK_TRUE(SUITE, DEVICE, CHECK_NONE, nvn.device_up, "nvnDeviceInitialize: %s",
               Why("false"));
    if (!nvn.device_up) {
        for (size_t i = 0; i < after_count; ++i) {
            ReportSkip(SUITE, AFTER_DEVICE[i], REPORT_REASON_DEPENDS,
                       "the device did not initialize");
        }
        return;
    }

    // Read from the layer's own code under ScarletNX, as a game's driver's version is: its own
    // table answers 55.15, so the layer's 55.8 coming back says the program's code was read.
    if (ReportStart(SUITE, VERSION)) {
        int major = -1;
        int minor = -1;
        nvn.nvn.nvnDeviceGetInteger(&nvn.device, NVN_DEVICE_INFO_API_MAJOR_VERSION, &major);
        nvn.nvn.nvnDeviceGetInteger(&nvn.device, NVN_DEVICE_INFO_API_MINOR_VERSION, &minor);
        CHECK_TRUE(SUITE, VERSION, CHECK_EXACT,
                   major == NVN_LAYER_API_MAJOR && minor == NVN_LAYER_API_MINOR,
                   "API %d.%d, where the layer answers %d.%d", major, minor, NVN_LAYER_API_MAJOR,
                   NVN_LAYER_API_MINOR);
    }

    if (!ReportStart(SUITE, TARGET)) {
        TearDown();
        return;
    }
    NvnLayerClearError();
    char why[256] = "";
    const bool up = BringUp(why, sizeof(why));
    CHECK_TRUE(SUITE, TARGET, CHECK_NONE, up, "%s", why);
    if (!up) {
        for (size_t i = 2; i < after_count; ++i) {
            ReportSkip(SUITE, AFTER_DEVICE[i], REPORT_REASON_DEPENDS,
                       "the queue, its pools or the target did not come up");
        }
        TearDown();
        return;
    }

    // The clear suite's colours, whose bytes the conversion to eight bits cannot round two ways.
    static const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const uint8_t red_bytes[4] = { 0xff, 0x00, 0x00, 0xff };
    static const float mid[4] = { 128.0f / 255.0f, 0.0f, 0.0f, 1.0f };
    static const uint8_t mid_bytes[4] = { 0x80, 0x00, 0x00, 0xff };
    ClearCase(RED, red, red_bytes);
    ClearCase(MID, mid, mid_bytes);

    if (ViewerScreenUp()) {
        // The program's own screen holds the default window, and a swapchain made on it after
        // the screen is what Eden never came back from (the game suite says the same). The start
        // screen leaves it up to the run's end, noprogress or not; a quick run from the mode file
        // never opens it.
        ReportSkip(SUITE, WINDOW, REPORT_REASON_UNAVAILABLE,
                   "the program's screen holds the window: a run begun on the start screen keeps "
                   "it up to its end, noprogress or not; quick in the mode file runs this case");
    } else if (ReportStart(SUITE, WINDOW)) {
        NvnLayerClearError();
        const bool presented = Present(why, sizeof(why));
        CHECK_TRUE(SUITE, WINDOW, CHECK_NONE, presented, "%s", why);
    }
    TearDown();
}
