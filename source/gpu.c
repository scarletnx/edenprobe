// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "gpu.h"

// Far more than any pass here needs, and sized for the case not yet written: a list that runs
// out of room mid-way reads like a driver fault and is really a budget.
#define COMMAND_MEMORY_SIZE 0x10000u

// The largest target this will build. Not a hardware limit: it is the point past which a
// mistyped size stops being a test and starts being an allocation failure with a confusing
// message, and 4096 by 4096 in RGBA8 is already 64 MiB.
#define MAXIMUM_DIMENSION 4096u

// What the scratch allocator lines allocations up to: more than any attribute or descriptor
// needs, and small enough that a dozen draws stay well inside the block. Uniform buffers name
// their own alignment instead.
#define SCRATCH_ALIGNMENT 256u

// The four bytes every format in this suite occupies. Stated once so the readback block, the
// copy pitch and the pixel stepping in every case cannot drift apart.
#define TARGET_BYTES_PER_PIXEL 4u

/// deko3d does not report failure by returning: its constructors call libnx's abort and never
/// come back, and a suite whose entire product is a report cannot afford to die with nothing
/// written. The abort is wrapped at link time and turned into a return, but only while
/// GpuOpenSpec is building things. Jumping out is safe because each create function has already
/// destroyed its own half-built object, and one landing pad is enough because this program has
/// one thread.
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

/// Records why the context is unusable and hands back the answer GpuOpenSpec owes its caller.
static bool Fail(GpuContext *gpu, const char *text) {
    snprintf(gpu->failure, sizeof(gpu->failure), "%s", text);
    gpu->ready = false;
    return false;
}

/// Records why one operation could not be done, and leaves the context usable.
///
/// Separate from Fail, because shutting the device down over one bad shader or one draw with no
/// room for its vertices would turn it into a whole suite of identical failures.
static bool Complain(GpuContext *gpu, const char *text) {
    snprintf(gpu->failure, sizeof(gpu->failure), "%s", text);
    return false;
}

static uint32_t RoundUpTo(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

/// Whether a format is one this file knows how to size, clear and copy.
///
/// Every entry is four bytes wide, which is what lets one readback block and one copy pitch serve
/// every suite; another width would need both reconsidered, not a line added here.
static bool FormatIsFourBytes(DkImageFormat format) {
    switch (format) {
    case DkImageFormat_RGBA8_Unorm:
    case DkImageFormat_RGBA8_Uint:
    case DkImageFormat_RGBA8_Sint:
    case DkImageFormat_R32_Float:
    case DkImageFormat_R32_Uint:
    case DkImageFormat_R32_Sint:
    case DkImageFormat_ZF32:
        return true;
    default:
        return false;
    }
}

void GpuSpecDefaults(GpuSpec *spec, uint32_t width, uint32_t height) {
    memset(spec, 0, sizeof(*spec));
    spec->width = width;
    spec->height = height;
    spec->color_format = DkImageFormat_RGBA8_Unorm;
    spec->depth_format = DkImageFormat_None;
}

/// Lays out one image and gives it memory out of @p block at @p used, which it advances.
static const char *PlaceImage(GpuContext *gpu, DkImage *image, DkImageLayout *layout,
                              DkImageFormat format, uint32_t width, uint32_t height,
                              uint32_t flags, DkMemBlock block, uint32_t *used) {
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, gpu->device);
    maker.type = DkImageType_2D;
    maker.flags = flags;
    maker.format = format;
    maker.dimensions[0] = width;
    maker.dimensions[1] = height;
    maker.mipLevels = 1;
    dkImageLayoutInitialize(layout, &maker);

    const uint64_t size = dkImageLayoutGetSize(layout);
    const uint32_t alignment = dkImageLayoutGetAlignment(layout);
    if (size == 0u || size > (uint64_t)UINT32_MAX / 2u || alignment == 0u) {
        return "an image layout came back with an unusable size";
    }

    const uint32_t offset = RoundUpTo(*used, alignment);
    if ((uint64_t)offset + size > dkMemBlockGetSize(block)) {
        return "the image memory the spec asked for is too small";
    }
    dkImageInitialize(image, layout, block, offset);
    *used = offset + (uint32_t)size;
    return NULL;
}

/// Creates one memory block of @p size bytes with @p flags, or returns the sentence to report.
static const char *MakeBlock(GpuContext *gpu, DkMemBlock *out, uint32_t size, uint32_t flags,
                             const char *what) {
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, gpu->device, RoundUpTo(size, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    *out = dkMemBlockCreate(&maker);
    return *out != NULL ? NULL : what;
}

/// Builds the device, the queue, the targets and the scratch, in that order.
///
/// Returns NULL when everything came up, or the sentence that goes in the report. Kept separate
/// from GpuOpenSpec so the jump target above has nothing to unwind: every local that matters
/// here dies with this frame.
static const char *BringUp(GpuContext *gpu) {
    DkDeviceMaker device_maker;
    dkDeviceMakerDefaults(&device_maker);
    gpu->device = dkDeviceCreate(&device_maker);
    if (gpu->device == NULL) {
        return "dkDeviceCreate produced no device";
    }

    // Graphics only. Nothing here dispatches compute, and asking for a compute channel would
    // have the driver reserve scratch memory for shaders that do not exist. The other default
    // flags (medium priority, zcull on) are left alone.
    DkQueueMaker queue_maker;
    dkQueueMakerDefaults(&queue_maker, gpu->device);
    queue_maker.flags = DkQueueFlags_Graphics;
    gpu->queue = dkQueueCreate(&queue_maker);
    if (gpu->queue == NULL) {
        return "dkQueueCreate produced no queue";
    }

    // Command memory has to be uncached to the processor and cached to the graphics unit; the
    // driver rejects any other combination. That is also the maker's default, spelled out here
    // because the image blocks below deliberately depart from it.
    const char *problem = MakeBlock(gpu, &gpu->cmd_memory, COMMAND_MEMORY_SIZE,
                                    DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached,
                                    "no memory for the command list");
    if (problem != NULL) {
        return problem;
    }

    // The odd one out, and not belt and braces: dkCmdBufCreate has no error path for the
    // wrapper above to catch, it hands a failed allocation straight back. Without this line a
    // null handle would walk on into dkCmdBufAddMemory.
    DkCmdBufMaker cmdbuf_maker;
    dkCmdBufMakerDefaults(&cmdbuf_maker, gpu->device);
    gpu->cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
    if (gpu->cmdbuf == NULL) {
        return "dkCmdBufCreate produced no command buffer";
    }
    dkCmdBufAddMemory(gpu->cmdbuf, gpu->cmd_memory, 0, COMMAND_MEMORY_SIZE);

    // Block linear, the default and the layout real rendering uses, so a case whose pixels
    // differ by position can catch a readback that walks the blocks wrongly. Not CPU visible,
    // because that flag combination is the one the driver treats as image memory and it decides
    // the memory kind the swizzled target is mapped with.
    {
        // Sized from the layout rather than guessed, so a format whose block linear padding is
        // larger than the pixels cannot overrun.
        DkImageLayoutMaker probe;
        dkImageLayoutMakerDefaults(&probe, gpu->device);
        probe.type = DkImageType_2D;
        probe.flags = DkImageFlags_UsageRender;
        probe.format = gpu->spec.color_format;
        probe.dimensions[0] = gpu->width;
        probe.dimensions[1] = gpu->height;
        probe.mipLevels = 1;
        DkImageLayout sizing;
        dkImageLayoutInitialize(&sizing, &probe);
        const uint64_t needed = dkImageLayoutGetSize(&sizing);
        if (needed == 0u || needed > (uint64_t)UINT32_MAX / 2u) {
            return "the colour target layout came back with an unusable size";
        }
        problem = MakeBlock(gpu, &gpu->image_memory, (uint32_t)needed + DK_MEMBLOCK_ALIGNMENT,
                            DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image,
                            "no memory for the render target");
        if (problem != NULL) {
            return problem;
        }
    }
    uint32_t color_used = 0;
    problem = PlaceImage(gpu, &gpu->image, &gpu->layout, gpu->spec.color_format, gpu->width,
                         gpu->height, DkImageFlags_UsageRender, gpu->image_memory, &color_used);
    if (problem != NULL) {
        return problem;
    }

    if (gpu->spec.depth_format != DkImageFormat_None) {
        DkImageLayoutMaker probe;
        dkImageLayoutMakerDefaults(&probe, gpu->device);
        probe.type = DkImageType_2D;
        probe.flags = DkImageFlags_UsageRender;
        probe.format = gpu->spec.depth_format;
        probe.dimensions[0] = gpu->width;
        probe.dimensions[1] = gpu->height;
        probe.mipLevels = 1;
        DkImageLayout sizing;
        dkImageLayoutInitialize(&sizing, &probe);
        const uint64_t needed = dkImageLayoutGetSize(&sizing);
        if (needed == 0u || needed > (uint64_t)UINT32_MAX / 2u) {
            return "the depth target layout came back with an unusable size";
        }
        problem = MakeBlock(gpu, &gpu->depth_memory, (uint32_t)needed + DK_MEMBLOCK_ALIGNMENT,
                            DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image,
                            "no memory for the depth target");
        if (problem != NULL) {
            return problem;
        }
        uint32_t depth_used = 0;
        problem =
            PlaceImage(gpu, &gpu->depth_image, &gpu->depth_layout, gpu->spec.depth_format,
                       gpu->width, gpu->height, DkImageFlags_UsageRender, gpu->depth_memory,
                       &depth_used);
        if (problem != NULL) {
            return problem;
        }
    }

    // Uncached to the processor on purpose: the driver offers no way to drop a processor read,
    // so a cached block would hand back whatever the data cache held from before the copy, and
    // a stale readback is the one failure this suite must never report as a pass.
    problem = MakeBlock(gpu, &gpu->read_memory, gpu->width * gpu->height * TARGET_BYTES_PER_PIXEL,
                        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached,
                        "no memory for the readback");
    if (problem != NULL) {
        return problem;
    }
    if (dkMemBlockGetCpuAddr(gpu->read_memory) == NULL) {
        return "the readback block is not visible to the processor";
    }

    if (gpu->spec.code_size != 0u) {
        // Shader code lives in its own address range that the driver reserves out of a separate
        // segment, which is what the Code flag asks for. Nothing else may share the block: the
        // offset a shader is loaded at is an offset within that segment, not within memory.
        problem = MakeBlock(gpu, &gpu->code_memory,
                            gpu->spec.code_size + DK_SHADER_CODE_UNUSABLE_SIZE,
                            DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached |
                                DkMemBlockFlags_Code,
                            "no memory for the shader code");
        if (problem != NULL) {
            return problem;
        }
        if (dkMemBlockGetCpuAddr(gpu->code_memory) == NULL) {
            return "the shader code block is not visible to the processor";
        }
    }

    if (gpu->spec.data_size != 0u) {
        problem = MakeBlock(gpu, &gpu->data_memory, gpu->spec.data_size,
                            DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached,
                            "no memory for the scratch buffer");
        if (problem != NULL) {
            return problem;
        }
        if (dkMemBlockGetCpuAddr(gpu->data_memory) == NULL) {
            return "the scratch block is not visible to the processor";
        }
    }

    if (gpu->spec.image_size != 0u) {
        problem = MakeBlock(gpu, &gpu->extra_image_memory, gpu->spec.image_size,
                            DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image,
                            "no memory for the textures");
        if (problem != NULL) {
            return problem;
        }
    }

    return NULL;
}

bool GpuOpenSpec(GpuContext *gpu, const GpuSpec *spec) {
    if (gpu == NULL || spec == NULL) {
        return false;
    }
    memset(gpu, 0, sizeof(*gpu));
    gpu->spec = *spec;
    gpu->width = spec->width;
    gpu->height = spec->height;
    gpu->bytes_per_pixel = TARGET_BYTES_PER_PIXEL;

    if (gpu->width == 0u || gpu->height == 0u || gpu->width > MAXIMUM_DIMENSION ||
        gpu->height > MAXIMUM_DIMENSION) {
        return Fail(gpu, "target size out of range");
    }
    if (!FormatIsFourBytes(spec->color_format)) {
        return Fail(gpu, "colour format is not one of the four byte formats this file handles");
    }
    if (spec->depth_format != DkImageFormat_None && !FormatIsFourBytes(spec->depth_format)) {
        return Fail(gpu, "depth format is not one of the four byte formats this file handles");
    }
    // The copy this file issues has no opinion about the pitch, since the linear stride
    // alignment binds only the 2D engine. Guarded anyway, so that every target stays usable by a
    // copy that is one flip flag away from reaching it.
    if ((gpu->width * TARGET_BYTES_PER_PIXEL) % DK_IMAGE_LINEAR_STRIDE_ALIGNMENT != 0u) {
        return Fail(gpu, "target width would give a pitch a 2D engine copy could not use");
    }

    if (setjmp(open_failure) != 0) {
        // Arrived from the wrapper above. Named loosely on purpose: what it catches is the
        // runtime's abort, which during bring-up is almost always the graphics library raising
        // it but is not only ever that.
        char text[96];
        snprintf(text, sizeof(text), "the graphics library or the runtime aborted (result 0x%08x)",
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

bool GpuOpen(GpuContext *gpu, uint32_t width, uint32_t height) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, width, height);
    return GpuOpenSpec(gpu, &spec);
}

void GpuClose(GpuContext *gpu) {
    if (gpu == NULL) {
        return;
    }
    gpu->ready = false;

    // The queue goes first because destroying it waits for the work already submitted, and that
    // work is reading the command memory and the images that the lines below free.
    if (gpu->queue != NULL) {
        dkQueueDestroy(gpu->queue);
        gpu->queue = NULL;
    }
    if (gpu->cmdbuf != NULL) {
        dkCmdBufDestroy(gpu->cmdbuf);
        gpu->cmdbuf = NULL;
    }
    DkMemBlock *const blocks[] = {
        &gpu->read_memory, &gpu->image_memory, &gpu->depth_memory,
        &gpu->code_memory, &gpu->data_memory,  &gpu->extra_image_memory,
        &gpu->cmd_memory,
    };
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
        if (*blocks[i] != NULL) {
            dkMemBlockDestroy(*blocks[i]);
            *blocks[i] = NULL;
        }
    }
    if (gpu->device != NULL) {
        dkDeviceDestroy(gpu->device);
        gpu->device = NULL;
    }
}

/// Waits for everything submitted to finish, then asks whether it finished badly.
///
/// The wait also asks for the caches to be written back, which is what puts drawn pixels where a
/// later copy can find them. Asking afterwards matters because a faulted queue goes on accepting
/// work and quietly does none of it. Its limit is that it only sees a fault the wait returned
/// from: one that leaves the fence unsignalled hangs here and goes unnamed.
static void Drain(GpuContext *gpu, const char *during) {
    dkQueueWaitIdle(gpu->queue);
    if (dkQueueIsInErrorState(gpu->queue)) {
        snprintf(gpu->failure, sizeof(gpu->failure), "the queue faulted during the %s", during);
        gpu->ready = false;
    }
}

/// Closes the list, sends it and waits, naming what was being done for the report.
static void SubmitAndDrain(GpuContext *gpu, const char *during) {
    dkQueueSubmitCommands(gpu->queue, dkCmdBufFinishList(gpu->cmdbuf));
    Drain(gpu, during);
}

void GpuBeginPass(GpuContext *gpu) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }

    dkCmdBufClear(gpu->cmdbuf);

    // Only the caches that hold copies of memory somebody else wrote. The level two cache is
    // deliberately left out: it holds the render target the previous pass wrote, and dropping
    // those lines would lose a result a later case is about to read.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Shader |
                        DkInvalidateFlags_Descriptors);

    DkImageView color;
    dkImageViewDefaults(&color, &gpu->image);
    if (gpu->spec.depth_format != DkImageFormat_None) {
        DkImageView depth;
        dkImageViewDefaults(&depth, &gpu->depth_image);
        dkCmdBufBindRenderTarget(gpu->cmdbuf, &color, &depth);
    } else {
        dkCmdBufBindRenderTarget(gpu->cmdbuf, &color, NULL);
    }

    // Everything below would otherwise be whatever the queue was left holding, and an inherited
    // scissor or blend would turn a correct machine into a failure nobody could attribute. A
    // case that wants one of these changed says so after this call and before the submit.
    const DkViewport viewport = { 0.0f, 0.0f, (float)gpu->width, (float)gpu->height, 0.0f, 1.0f };
    dkCmdBufSetViewports(gpu->cmdbuf, 0, &viewport, 1);
    const DkScissor scissor = { 0, 0, gpu->width, gpu->height };
    dkCmdBufSetScissors(gpu->cmdbuf, 0, &scissor, 1);

    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    // No culling. Which way round a triangle is wound decides nothing in this suite, and leaving
    // the default on would make every coverage case also a winding case, so a failure would have
    // two explanations instead of one.
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(gpu->cmdbuf, &rasterizer);

    // One sample, at the centre of the pixel: the defaults set every sample location to 8 in
    // sixteenths, which is exactly one half. Every claim this suite makes about which pixels a
    // triangle covers is a claim about the pixel centre, so this is load bearing.
    DkMultisampleState multisample;
    dkMultisampleStateDefaults(&multisample);
    dkCmdBufBindMultisampleState(gpu->cmdbuf, &multisample);
    dkCmdBufSetSampleMask(gpu->cmdbuf, ~0u);

    // Off, and said out loud: a conservative rasteriser covers every pixel a triangle touches
    // rather than every pixel whose centre it contains, which is a different answer to the
    // question the rasterisation suite asks.
    dkCmdBufSetConservativeRasterEnable(gpu->cmdbuf, false);

    DkColorState color_state;
    dkColorStateDefaults(&color_state);
    dkCmdBufBindColorState(gpu->cmdbuf, &color_state);
    DkColorWriteState write_state;
    dkColorWriteStateDefaults(&write_state);
    dkCmdBufBindColorWriteState(gpu->cmdbuf, &write_state);
    DkBlendState blend;
    dkBlendStateDefaults(&blend);
    dkCmdBufBindBlendState(gpu->cmdbuf, 0, &blend);
    dkCmdBufSetBlendConst(gpu->cmdbuf, 0.0f, 0.0f, 0.0f, 0.0f);
    dkCmdBufSetAlphaRef(gpu->cmdbuf, 0.0f);

    DkDepthStencilState depth_state;
    dkDepthStencilStateDefaults(&depth_state);
    depth_state.depthTestEnable = 0;
    depth_state.depthWriteEnable = 0;
    depth_state.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(gpu->cmdbuf, &depth_state);
    dkCmdBufSetDepthBounds(gpu->cmdbuf, false, 0.0f, 1.0f);
}

void GpuSubmit(GpuContext *gpu) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }
    SubmitAndDrain(gpu, "pass");
}

void GpuClear(GpuContext *gpu, float red, float green, float blue, float alpha) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }
    GpuClearRect(gpu, 0, 0, gpu->width, gpu->height, red, green, blue, alpha);
}

void GpuClearRect(GpuContext *gpu, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  float red, float green, float blue, float alpha) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }
    // Subtraction rather than addition, so a width larger than the target cannot wrap its way
    // past the comparison.
    if (width == 0u || height == 0u || width > gpu->width || height > gpu->height ||
        x > gpu->width - width || y > gpu->height - height) {
        snprintf(gpu->failure, sizeof(gpu->failure),
                 "clear rectangle %ux%u at (%u,%u) does not fit a %ux%u target", (unsigned)width,
                 (unsigned)height, (unsigned)x, (unsigned)y, (unsigned)gpu->width,
                 (unsigned)gpu->height);
        gpu->ready = false;
        return;
    }

    GpuBeginPass(gpu);
    // The scissor is not decoration here, it is the bound. A clear is a rectangle the rasteriser
    // draws, and the graphics library asks the hardware to cut it to the scissor, so narrowing
    // the one GpuBeginPass just set to the whole target is what makes this clear a rectangle.
    const DkScissor scissor = { x, y, width, height };
    dkCmdBufSetScissors(gpu->cmdbuf, 0, &scissor, 1);
    dkCmdBufClearColorFloat(gpu->cmdbuf, 0, DkColorMask_RGBA, red, green, blue, alpha);
    SubmitAndDrain(gpu, "clear");
}

void GpuClearUint(GpuContext *gpu, uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }
    GpuBeginPass(gpu);
    dkCmdBufClearColorUint(gpu->cmdbuf, 0, DkColorMask_RGBA, red, green, blue, alpha);
    SubmitAndDrain(gpu, "clear");
}

void GpuClearDepth(GpuContext *gpu, float depth) {
    if (gpu == NULL || !gpu->ready || gpu->spec.depth_format == DkImageFormat_None) {
        return;
    }
    GpuBeginPass(gpu);

    // A clear goes through the same unit an ordinary fragment does, and the graphics library
    // leaves the depth write state alone, so the mask GpuBeginPass closed has to be opened here
    // or the clear never lands. The test stays off, so nothing the buffer already held can
    // reject it.
    DkDepthStencilState writing;
    dkDepthStencilStateDefaults(&writing);
    writing.depthTestEnable = 0;
    writing.depthWriteEnable = 1;
    writing.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(gpu->cmdbuf, &writing);

    // Depth only: the stencil mask is zero, so a format that carries stencil keeps whatever it
    // had and no case here can accidentally be reading a stencil clear instead of a depth one.
    dkCmdBufClearDepthStencil(gpu->cmdbuf, true, depth, 0, 0);
    SubmitAndDrain(gpu, "depth clear");
}


/// Copies @p view into the readback block and hands back the processor's view of it.
static const uint8_t *CopyToReadback(GpuContext *gpu, const DkImageView *view) {
    dkCmdBufClear(gpu->cmdbuf);

    // The draw ran on the graphics engine and this copy runs on the copy engine. It orders, it
    // does not invalidate, because dropping a cache line here would throw away the very pixels
    // being collected.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);

    const DkImageRect rect = { 0, 0, 0, gpu->width, gpu->height, 1 };

    // Zeroes mean tightly packed, so the pixels land in row order with nothing between them,
    // which is what this function promises to return. No DkBlitFlag_FlipX: that flag alone
    // routes the copy through the 2D engine, which has its own requirements on the image.
    const DkCopyBuf destination = { dkMemBlockGetGpuAddr(gpu->read_memory), 0, 0 };
    dkCmdBufCopyImageToBuffer(gpu->cmdbuf, view, &rect, &destination, 0);

    SubmitAndDrain(gpu, "readback");
    if (!gpu->ready) {
        return NULL;
    }

    return (const uint8_t *)dkMemBlockGetCpuAddr(gpu->read_memory);
}

const uint8_t *GpuReadBack(GpuContext *gpu) {
    if (gpu == NULL || !gpu->ready) {
        return NULL;
    }
    DkImageView source;
    dkImageViewDefaults(&source, &gpu->image);
    return CopyToReadback(gpu, &source);
}

const uint8_t *GpuReadBackDepth(GpuContext *gpu) {
    if (gpu == NULL || !gpu->ready || gpu->spec.depth_format == DkImageFormat_None) {
        return NULL;
    }
    DkImageView source;
    dkImageViewDefaults(&source, &gpu->depth_image);
    // The default source is the depth half of the image, which for a format that carries only
    // depth is the whole of it. Said here because the field exists and its other value would
    // quietly hand back stencil.
    source.dsSource = DkDsSource_Depth;
    return CopyToReadback(gpu, &source);
}

// The header every module the shader compiler emits begins with. Written out rather than
// included, because the toolchain ships no header for it.
typedef struct {
    uint32_t magic; ///< 'DKSH', little endian
    uint32_t header_size;
    uint32_t control_size; ///< the control section, this header included, at the front of the file
    uint32_t code_size;    ///< the machine code, immediately after the control section
} ShaderModuleHeader;

#define SHADER_MODULE_MAGIC 0x48534B44u

bool GpuLoadShader(GpuContext *gpu, DkShader *out, const void *dksh, uint32_t size) {
    if (gpu == NULL || !gpu->ready || out == NULL) {
        return false;
    }
    if (gpu->code_memory == NULL) {
        return Complain(gpu, "a shader was loaded into a context whose spec asked for no code memory");
    }
    if (dksh == NULL || size < sizeof(ShaderModuleHeader)) {
        return Complain(gpu, "the shader module is too small to hold its own header");
    }

    ShaderModuleHeader header;
    memcpy(&header, dksh, sizeof(header));
    if (header.magic != SHADER_MODULE_MAGIC || header.control_size < sizeof(header) ||
        header.code_size == 0u || (uint64_t)header.control_size + header.code_size > size) {
        return Complain(gpu, "the shader module's header does not describe the bytes after it");
    }

    // Only the machine code is copied into code memory, which puts its alignment under this
    // function's control rather than at the mercy of the control section's length. The first
    // DK_SHADER_CODE_UNUSABLE_SIZE bytes are skipped because the graphics unit fetches
    // instructions ahead of the entry point.
    uint32_t offset = RoundUpTo(gpu->code_used, DK_SHADER_CODE_ALIGNMENT);
    if (offset < DK_SHADER_CODE_UNUSABLE_SIZE) {
        offset = DK_SHADER_CODE_UNUSABLE_SIZE;
    }
    if ((uint64_t)offset + header.code_size > dkMemBlockGetSize(gpu->code_memory)) {
        return Complain(gpu, "the code memory the spec asked for is too small for the shaders");
    }

    uint8_t *const code = (uint8_t *)dkMemBlockGetCpuAddr(gpu->code_memory);
    memcpy(code + offset, (const uint8_t *)dksh + header.control_size, header.code_size);
    gpu->code_used = offset + header.code_size;

    DkShaderMaker maker;
    dkShaderMakerDefaults(&maker, gpu->code_memory, offset);
    maker.control = dksh;
    dkShaderInitialize(out, &maker);

    if (!dkShaderIsValid(out)) {
        return Complain(gpu, "deko3d refused the shader module");
    }
    return true;
}

void *GpuAllocData(GpuContext *gpu, uint32_t size, uint32_t alignment, DkGpuAddr *gpu_addr) {
    if (gpu == NULL || !gpu->ready || gpu->data_memory == NULL || size == 0u || alignment == 0u) {
        return NULL;
    }
    const uint32_t offset = RoundUpTo(gpu->data_used, alignment);
    if ((uint64_t)offset + size > dkMemBlockGetSize(gpu->data_memory)) {
        return NULL;
    }
    gpu->data_used = offset + size;
    if (gpu_addr != NULL) {
        *gpu_addr = dkMemBlockGetGpuAddr(gpu->data_memory) + offset;
    }
    return (uint8_t *)dkMemBlockGetCpuAddr(gpu->data_memory) + offset;
}

uint32_t GpuDataMark(const GpuContext *gpu) {
    return gpu != NULL ? gpu->data_used : 0u;
}

void GpuDataRewind(GpuContext *gpu, uint32_t mark) {
    if (gpu != NULL && mark <= gpu->data_used) {
        gpu->data_used = mark;
    }
}

bool GpuCreateTexture(GpuContext *gpu, DkImage *out, DkImageLayout *layout, DkImageFormat format,
                      uint32_t width, uint32_t height) {
    if (gpu == NULL || !gpu->ready || gpu->extra_image_memory == NULL) {
        return false;
    }
    const char *problem = PlaceImage(gpu, out, layout, format, width, height, 0,
                                     gpu->extra_image_memory, &gpu->extra_image_used);
    if (problem != NULL) {
        return Complain(gpu, problem);
    }
    return true;
}

void GpuUploadTexture(GpuContext *gpu, const DkImage *image, DkImageFormat format, uint32_t width,
                      uint32_t height, const void *pixels) {
    if (gpu == NULL || !gpu->ready || !FormatIsFourBytes(format)) {
        return;
    }

    // The copy engine wants the linear side's pitch aligned, and four texels of four bytes is
    // half of that alignment. So the staging rows are padded out to a pitch the copy is allowed
    // and the row length is stated rather than left at the tightly packed default. Passing the
    // texels packed would hand the engine a pitch of sixteen bytes, which it does not take.
    const uint32_t row_bytes = width * TARGET_BYTES_PER_PIXEL;
    const uint32_t pitch = RoundUpTo(row_bytes, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    DkGpuAddr staging_addr = 0;
    uint8_t *const staging = (uint8_t *)GpuAllocData(gpu, pitch * height, DK_MEMBLOCK_ALIGNMENT,
                                                     &staging_addr);
    if (staging == NULL) {
        return;
    }
    memset(staging, 0, pitch * height);
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(staging + (size_t)y * pitch, (const uint8_t *)pixels + (size_t)y * row_bytes,
               row_bytes);
    }

    dkCmdBufClear(gpu->cmdbuf);
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);

    DkImageView destination;
    dkImageViewDefaults(&destination, image);
    const DkImageRect rect = { 0, 0, 0, width, height, 1 };
    const DkCopyBuf source = { staging_addr, pitch, height };
    dkCmdBufCopyBufferToImage(gpu->cmdbuf, &source, &destination, &rect, 0);

    SubmitAndDrain(gpu, "texture upload");
}

bool GpuDrawTriangles(GpuContext *gpu, const DkShader *vertex_shader,
                      const DkShader *fragment_shader, const GpuVertex *vertices,
                      uint32_t vertex_count) {
    if (gpu == NULL || !gpu->ready || vertices == NULL || vertex_count == 0u ||
        vertex_count % 3u != 0u) {
        return false;
    }

    const uint32_t bytes = vertex_count * (uint32_t)sizeof(GpuVertex);
    DkGpuAddr address = 0;
    void *const destination = GpuAllocData(gpu, bytes, SCRATCH_ALIGNMENT, &address);
    if (destination == NULL) {
        return Complain(gpu, "the scratch block ran out of room for a draw's vertices");
    }
    memcpy(destination, vertices, bytes);

    const DkShader *const shaders[] = { vertex_shader, fragment_shader };
    dkCmdBufBindShaders(gpu->cmdbuf, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);

    // One attribute state for the whole suite, matching GpuVertex field for field. The offsets
    // come from the structure rather than from constants so the two cannot drift.
    static const DkVtxAttribState attributes[] = {
        { .bufferId = 0,
          .isFixed = 0,
          .offset = offsetof(GpuVertex, x),
          .size = DkVtxAttribSize_3x32,
          .type = DkVtxAttribType_Float,
          .isBgra = 0 },
        { .bufferId = 0,
          .isFixed = 0,
          .offset = offsetof(GpuVertex, u),
          .size = DkVtxAttribSize_2x32,
          .type = DkVtxAttribType_Float,
          .isBgra = 0 },
        { .bufferId = 0,
          .isFixed = 0,
          .offset = offsetof(GpuVertex, r),
          .size = DkVtxAttribSize_4x32,
          .type = DkVtxAttribType_Float,
          .isBgra = 0 },
    };
    static const DkVtxBufferState buffer_state = { sizeof(GpuVertex), 0 };
    dkCmdBufBindVtxAttribState(gpu->cmdbuf, attributes,
                               sizeof(attributes) / sizeof(attributes[0]));
    dkCmdBufBindVtxBufferState(gpu->cmdbuf, &buffer_state, 1);
    dkCmdBufBindVtxBuffer(gpu->cmdbuf, 0, address, bytes);

    dkCmdBufDraw(gpu->cmdbuf, DkPrimitive_Triangles, vertex_count, 1, 0, 0);
    return true;
}
