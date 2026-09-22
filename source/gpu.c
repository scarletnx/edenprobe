// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "gpu.h"

// How much memory the command buffer gets. One clear or one copy is a handful of words, so 64 KiB
// is two orders of magnitude more than any case here needs. It is sized for the case that has not
// been written yet rather than the three that have, because a list that runs out of room mid-way
// reads like a driver fault and is really a budget.
#define COMMAND_MEMORY_SIZE 0x10000u

// The largest target this will build. Not a hardware limit: it is the point past which a
// mistyped size stops being a test and starts being an allocation failure with a confusing
// message, and 4096 by 4096 in RGBA8 is already 64 MiB.
#define MAXIMUM_DIMENSION 4096u

// Four, and it has to stay in step with the format the layout below asks for. Both appear once,
// next to each other, because a readback block sized for one format and filled with another is a
// bug that reports itself as wrong pixels rather than as an error.
#define TARGET_FORMAT DkImageFormat_RGBA8_Unorm
#define TARGET_BYTES_PER_PIXEL 4u

/// deko3d does not report failure by returning: its constructors call libnx's abort and never
/// come back, and a suite whose entire product is a report cannot afford to die with nothing
/// written. The abort is wrapped at link time and turned into a return, but only while GpuOpen
/// is building things. Jumping out is safe because each create function has already destroyed
/// its own half-built object, and one landing pad is enough because this program has one thread.
static jmp_buf open_failure;
static volatile bool open_failure_armed;
static Result open_failure_result;

void NX_NORETURN __real_diagAbortWithResult(Result result);
void NX_NORETURN __wrap_diagAbortWithResult(Result result);

void NX_NORETURN __wrap_diagAbortWithResult(Result result) {
    if (open_failure_armed) {
        // Disarmed here rather than after the jump, so that an abort raised while unwinding
        // cannot bounce off the same landing pad twice.
        open_failure_armed = false;
        open_failure_result = result;
        longjmp(open_failure, 1);
    }
    __real_diagAbortWithResult(result);
}

/// Records why the context is unusable and hands back the answer GpuOpen owes its caller.
static bool Fail(GpuContext *gpu, const char *text) {
    snprintf(gpu->failure, sizeof(gpu->failure), "%s", text);
    gpu->ready = false;
    return false;
}

static uint32_t RoundUpTo(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

/// Builds the device, the queue, the target and the readback block, in that order.
///
/// Returns NULL when everything came up, or the sentence that goes in the report. Kept separate
/// from GpuOpen so the jump target above has nothing to unwind: every local that matters here
/// dies with this frame.
static const char *BringUp(GpuContext *gpu) {
    DkDeviceMaker device_maker;
    dkDeviceMakerDefaults(&device_maker);
    gpu->device = dkDeviceCreate(&device_maker);
    if (gpu->device == NULL) {
        return "dkDeviceCreate produced no device";
    }

    // Graphics only. This target is cleared and copied, never dispatched to, and asking for a
    // compute channel would have the driver reserve scratch memory for shaders that do not
    // exist. The other default flags (medium priority, zcull on) are left alone.
    DkQueueMaker queue_maker;
    dkQueueMakerDefaults(&queue_maker, gpu->device);
    queue_maker.flags = DkQueueFlags_Graphics;
    gpu->queue = dkQueueCreate(&queue_maker);
    if (gpu->queue == NULL) {
        return "dkQueueCreate produced no queue";
    }

    // Command memory has to be uncached to the processor and cached to the graphics unit; the
    // driver rejects any other combination. That is also the maker's default, spelled out here
    // because the next block deliberately departs from it.
    DkMemBlockMaker block_maker;
    dkMemBlockMakerDefaults(&block_maker, gpu->device, COMMAND_MEMORY_SIZE);
    block_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    gpu->cmd_memory = dkMemBlockCreate(&block_maker);
    if (gpu->cmd_memory == NULL) {
        return "no memory for the command list";
    }

    DkCmdBufMaker cmdbuf_maker;
    dkCmdBufMakerDefaults(&cmdbuf_maker, gpu->device);
    gpu->cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
    if (gpu->cmdbuf == NULL) {
        return "dkCmdBufCreate produced no command buffer";
    }
    dkCmdBufAddMemory(gpu->cmdbuf, gpu->cmd_memory, 0, COMMAND_MEMORY_SIZE);

    // Block linear, which is the default and the layout real rendering uses. A pitch linear
    // target would be simpler and would be cheating: the readback would then be a straight
    // memory copy, and half of what a drawing case is for is proving that the target's real
    // storage order survives the trip back. RGBA8_Unorm because the Exact tier needs a format
    // whose values are fixed bit patterns.
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, gpu->device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender;
    layout_maker.format = TARGET_FORMAT;
    layout_maker.dimensions[0] = gpu->width;
    layout_maker.dimensions[1] = gpu->height;
    layout_maker.mipLevels = 1;
    dkImageLayoutInitialize(&gpu->layout, &layout_maker);

    const uint64_t image_size = dkImageLayoutGetSize(&gpu->layout);
    if (image_size == 0u || image_size > (uint64_t)UINT32_MAX - DK_MEMBLOCK_ALIGNMENT) {
        return "the target layout came back with an unusable size";
    }

    // Not CPU visible, and not merely because nothing reads it from here: the flag combination
    // below is the one the driver treats as image memory, which is what decides the memory kind
    // the swizzled target is mapped with.
    dkMemBlockMakerDefaults(&block_maker, gpu->device,
                            RoundUpTo((uint32_t)image_size, DK_MEMBLOCK_ALIGNMENT));
    block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    gpu->image_memory = dkMemBlockCreate(&block_maker);
    if (gpu->image_memory == NULL) {
        return "no memory for the render target";
    }
    // Offset zero, which satisfies the layout's alignment whatever it turns out to be.
    dkImageInitialize(&gpu->image, &gpu->layout, gpu->image_memory, 0);

    // Uncached to the processor on purpose: the driver offers no way to drop a processor read,
    // so a cached block would hand back whatever the data cache held from before the copy, and
    // a stale readback is the one failure this suite must never report as a pass.
    dkMemBlockMakerDefaults(
        &block_maker, gpu->device,
        RoundUpTo(gpu->width * gpu->height * TARGET_BYTES_PER_PIXEL, DK_MEMBLOCK_ALIGNMENT));
    block_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    gpu->read_memory = dkMemBlockCreate(&block_maker);
    if (gpu->read_memory == NULL) {
        return "no memory for the readback";
    }
    if (dkMemBlockGetCpuAddr(gpu->read_memory) == NULL) {
        return "the readback block is not visible to the processor";
    }

    return NULL;
}

bool GpuOpen(GpuContext *gpu, uint32_t width, uint32_t height) {
    if (gpu == NULL) {
        return false;
    }
    memset(gpu, 0, sizeof(*gpu));
    gpu->width = width;
    gpu->height = height;

    if (width == 0u || height == 0u || width > MAXIMUM_DIMENSION || height > MAXIMUM_DIMENSION) {
        return Fail(gpu, "target size out of range");
    }
    // A row of the readback is the width times the format's four bytes, and the block it lands in
    // starts aligned, so a width that makes that row not a multiple of the linear stride alignment
    // hands the copy a pitch it is not allowed. Caught here, where it is one sentence, rather than
    // in a case that would have to report it as pixels in the wrong places.
    if ((width * TARGET_BYTES_PER_PIXEL) % DK_IMAGE_LINEAR_STRIDE_ALIGNMENT != 0u) {
        return Fail(gpu, "target width does not give a readback pitch the copy can use");
    }

    if (setjmp(open_failure) != 0) {
        // Arrived here from the wrapper above: deko3d decided it could not continue. Nothing
        // read below was written between the setjmp and the jump, so it all still holds.
        char text[96];
        snprintf(text, sizeof(text), "deko3d could not start (result 0x%08x)",
                 (unsigned)open_failure_result);
        GpuClose(gpu);
        return Fail(gpu, text);
    }

    open_failure_armed = true;
    const char *problem = BringUp(gpu);
    open_failure_armed = false;

    if (problem != NULL) {
        GpuClose(gpu);
        return Fail(gpu, problem);
    }

    gpu->ready = true;
    return true;
}

void GpuClose(GpuContext *gpu) {
    if (gpu == NULL) {
        return;
    }
    gpu->ready = false;

    // The queue goes first because destroying it waits for the work already submitted, and that
    // work is reading the command memory and the target that the next four lines free.
    if (gpu->queue != NULL) {
        dkQueueDestroy(gpu->queue);
        gpu->queue = NULL;
    }
    if (gpu->cmdbuf != NULL) {
        dkCmdBufDestroy(gpu->cmdbuf);
        gpu->cmdbuf = NULL;
    }
    if (gpu->read_memory != NULL) {
        dkMemBlockDestroy(gpu->read_memory);
        gpu->read_memory = NULL;
    }
    if (gpu->image_memory != NULL) {
        dkMemBlockDestroy(gpu->image_memory);
        gpu->image_memory = NULL;
    }
    if (gpu->cmd_memory != NULL) {
        dkMemBlockDestroy(gpu->cmd_memory);
        gpu->cmd_memory = NULL;
    }
    if (gpu->device != NULL) {
        dkDeviceDestroy(gpu->device);
        gpu->device = NULL;
    }
}

void GpuClear(GpuContext *gpu, float red, float green, float blue, float alpha) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }

    dkCmdBufClear(gpu->cmdbuf);

    DkImageView target;
    dkImageViewDefaults(&target, &gpu->image);
    dkCmdBufBindRenderTarget(gpu->cmdbuf, &target, NULL);

    // The scissor is not decoration. A clear is a rectangle drawn by the rasteriser and it is
    // cut by whatever scissor the queue is holding, which on a queue that has drawn nothing is
    // whatever the driver left there. Stating it means the case tests the clear rather than the
    // default state, and means a partial result is a real finding instead of a scissor.
    const DkViewport viewport = { 0.0f, 0.0f, (float)gpu->width, (float)gpu->height, 0.0f, 1.0f };
    dkCmdBufSetViewports(gpu->cmdbuf, 0, &viewport, 1);
    const DkScissor scissor = { 0, 0, gpu->width, gpu->height };
    dkCmdBufSetScissors(gpu->cmdbuf, 0, &scissor, 1);

    dkCmdBufClearColorFloat(gpu->cmdbuf, 0, DkColorMask_RGBA, red, green, blue, alpha);

    dkQueueSubmitCommands(gpu->queue, dkCmdBufFinishList(gpu->cmdbuf));

    // Drains rather than flushes: this also signals a fence asking for the caches to be written
    // back, which is what puts the cleared pixels somewhere the copy below can find them.
    dkQueueWaitIdle(gpu->queue);
}

const uint8_t *GpuReadBack(GpuContext *gpu) {
    if (gpu == NULL || !gpu->ready) {
        return NULL;
    }

    dkCmdBufClear(gpu->cmdbuf);

    // The clear ran on the graphics engine and this copy runs on the copy engine. The drain at
    // the end of GpuClear already separates them, so this barrier is for the caller that later
    // draws and reads back in one go; it orders, it does not invalidate, because dropping a
    // cache line here would throw away the very pixels being collected.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);

    DkImageView source;
    dkImageViewDefaults(&source, &gpu->image);
    const DkImageRect rect = { 0, 0, 0, gpu->width, gpu->height, 1 };

    // A row length and an image height of zero mean tightly packed, so the destination pitch is
    // the rect's width times four bytes and the pixels land in row order with nothing between
    // them, which is exactly what this function promises to return. No flip flag: with one set,
    // the driver would route the copy through the 2D engine instead of the copy engine, which
    // is a different path with its own requirements on the image.
    const DkCopyBuf destination = { dkMemBlockGetGpuAddr(gpu->read_memory), 0, 0 };
    dkCmdBufCopyImageToBuffer(gpu->cmdbuf, &source, &rect, &destination, 0);

    dkQueueSubmitCommands(gpu->queue, dkCmdBufFinishList(gpu->cmdbuf));
    dkQueueWaitIdle(gpu->queue);

    return (const uint8_t *)dkMemBlockGetCpuAddr(gpu->read_memory);
}
