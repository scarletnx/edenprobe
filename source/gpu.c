// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "gpu.h"
#include "perf.h"

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

// The four bytes the formats the older suites use occupy: the staging for texture uploads and
// the pitch of the processor-written images are sized off it. The format suite's targets name
// their own size through GpuFormatBytes.
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

uint32_t GpuFormatBytes(DkImageFormat format) {
    switch (format) {
    case DkImageFormat_R8_Unorm:
    case DkImageFormat_R8_Snorm:
    case DkImageFormat_R8_Uint:
    case DkImageFormat_R8_Sint:
        return 1u;
    case DkImageFormat_R16_Float:
    case DkImageFormat_R16_Unorm:
    case DkImageFormat_R16_Snorm:
    case DkImageFormat_R16_Uint:
    case DkImageFormat_R16_Sint:
    case DkImageFormat_RG8_Unorm:
    case DkImageFormat_RG8_Snorm:
    case DkImageFormat_RG8_Uint:
    case DkImageFormat_RG8_Sint:
    case DkImageFormat_BGR565_Unorm:
    case DkImageFormat_BGR5_Unorm:
    case DkImageFormat_BGR5A1_Unorm:
        return 2u;
    case DkImageFormat_R32_Float:
    case DkImageFormat_R32_Uint:
    case DkImageFormat_R32_Sint:
    case DkImageFormat_RG16_Float:
    case DkImageFormat_RG16_Unorm:
    case DkImageFormat_RG16_Snorm:
    case DkImageFormat_RG16_Uint:
    case DkImageFormat_RG16_Sint:
    case DkImageFormat_RGBA8_Unorm:
    case DkImageFormat_RGBA8_Snorm:
    case DkImageFormat_RGBA8_Uint:
    case DkImageFormat_RGBA8_Sint:
    case DkImageFormat_RGBX8_Unorm:
    case DkImageFormat_RGBX8_Snorm:
    case DkImageFormat_RGBX8_Uint:
    case DkImageFormat_RGBX8_Sint:
    case DkImageFormat_RGBX8_Unorm_sRGB:
    case DkImageFormat_RGBA8_Unorm_sRGB:
    case DkImageFormat_RGB10A2_Unorm:
    case DkImageFormat_RGB10A2_Uint:
    case DkImageFormat_RG11B10_Float:
    case DkImageFormat_BGRX8_Unorm:
    case DkImageFormat_BGRA8_Unorm:
    case DkImageFormat_BGRX8_Unorm_sRGB:
    case DkImageFormat_BGRA8_Unorm_sRGB:
    case DkImageFormat_ZF32:
        return 4u;
    case DkImageFormat_RG32_Float:
    case DkImageFormat_RG32_Uint:
    case DkImageFormat_RG32_Sint:
    case DkImageFormat_RGBA16_Float:
    case DkImageFormat_RGBA16_Unorm:
    case DkImageFormat_RGBA16_Snorm:
    case DkImageFormat_RGBA16_Uint:
    case DkImageFormat_RGBA16_Sint:
    case DkImageFormat_RGBX16_Float:
    case DkImageFormat_RGBX16_Unorm:
    case DkImageFormat_RGBX16_Snorm:
    case DkImageFormat_RGBX16_Uint:
    case DkImageFormat_RGBX16_Sint:
        return 8u;
    case DkImageFormat_RGBA32_Float:
    case DkImageFormat_RGBA32_Uint:
    case DkImageFormat_RGBA32_Sint:
    case DkImageFormat_RGBX32_Float:
    case DkImageFormat_RGBX32_Uint:
    case DkImageFormat_RGBX32_Sint:
        return 16u;
    default:
        return 0u;
    }
}

void GpuSpecDefaults(GpuSpec *spec, uint32_t width, uint32_t height) {
    memset(spec, 0, sizeof(*spec));
    spec->width = width;
    spec->height = height;
    spec->color_format = DkImageFormat_RGBA8_Unorm;
    spec->depth_format = DkImageFormat_None;
}

/// Lays out one 2D image, @p pitch_or_tile going into the maker's union.
static void LayOut(GpuContext *gpu, DkImageLayout *layout, DkImageFormat format, uint32_t width,
                   uint32_t height, uint32_t flags, uint32_t pitch_or_tile) {
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, gpu->device);
    maker.type = DkImageType_2D;
    maker.flags = flags;
    maker.format = format;
    maker.dimensions[0] = width;
    maker.dimensions[1] = height;
    maker.mipLevels = 1;
    maker.pitchStride = pitch_or_tile;
    dkImageLayoutInitialize(layout, &maker);
}

/// Lays out one image and gives it memory out of @p block at @p used, which it advances. The
/// offset it landed at goes to @p placed_at when that is not NULL.
static const char *PlaceImageAt(GpuContext *gpu, DkImage *image, DkImageLayout *layout,
                                DkImageFormat format, uint32_t width, uint32_t height,
                                uint32_t flags, uint32_t pitch_or_tile, DkMemBlock block,
                                uint32_t *used, uint32_t *placed_at) {
    LayOut(gpu, layout, format, width, height, flags, pitch_or_tile);

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
    if (placed_at != NULL) {
        *placed_at = offset;
    }
    return NULL;
}

static const char *PlaceImage(GpuContext *gpu, DkImage *image, DkImageLayout *layout,
                              DkImageFormat format, uint32_t width, uint32_t height,
                              uint32_t flags, DkMemBlock block, uint32_t *used) {
    return PlaceImageAt(gpu, image, layout, format, width, height, flags, 0, block, used, NULL);
}

/// The command memory the spec asked for, or the default.
static uint32_t CommandSize(const GpuContext *gpu) {
    return RoundUpTo(gpu->spec.command_size != 0u ? gpu->spec.command_size : COMMAND_MEMORY_SIZE,
                     DK_MEMBLOCK_ALIGNMENT);
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

    // Graphics only unless the spec asks for compute: a compute channel makes the driver
    // reserve scratch memory for shaders the drawing suites never run. The other default flags
    // (medium priority, zcull on) are left alone.
    DkQueueMaker queue_maker;
    dkQueueMakerDefaults(&queue_maker, gpu->device);
    queue_maker.flags =
        DkQueueFlags_Graphics | (gpu->spec.compute ? (uint32_t)DkQueueFlags_Compute : 0u);
    gpu->queue = dkQueueCreate(&queue_maker);
    if (gpu->queue == NULL) {
        return "dkQueueCreate produced no queue";
    }

    // Command memory has to be uncached to the processor and cached to the graphics unit; the
    // driver rejects any other combination. That is also the maker's default, spelled out here
    // because the image blocks below deliberately depart from it.
    const char *problem = MakeBlock(gpu, &gpu->cmd_memory, CommandSize(gpu),
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
    dkCmdBufAddMemory(gpu->cmdbuf, gpu->cmd_memory, 0, CommandSize(gpu));

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
    // As large as the widest of the main target and whatever the spec says it will read back
    // from its own images, which is what lets one block serve every format a suite renders to.
    gpu->read_size = gpu->width * gpu->height *
                     (gpu->spec.readback_bytes_per_pixel > gpu->bytes_per_pixel
                          ? gpu->spec.readback_bytes_per_pixel
                          : gpu->bytes_per_pixel);
    problem = MakeBlock(gpu, &gpu->read_memory, gpu->read_size,
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

    // The one image block the processor can reach. Uncached on its side, so a write through the
    // pointer is in memory when the store retires and dkMemBlockFlushCpuCache is never owed.
    if (gpu->spec.cpu_image_size != 0u) {
        problem = MakeBlock(gpu, &gpu->cpu_image_memory, gpu->spec.cpu_image_size,
                            DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached |
                                DkMemBlockFlags_Image,
                            "no memory for the processor-visible textures");
        if (problem != NULL) {
            return problem;
        }
        if (dkMemBlockGetCpuAddr(gpu->cpu_image_memory) == NULL) {
            return "the processor-visible texture block is not visible to the processor";
        }
    }

    // Pitch memory, not image memory: a pitch linear image needs no swizzled mapping, and the
    // point of this block is the processor's data cache between its writes and the memory.
    if (gpu->spec.cpu_cached_size != 0u) {
        problem = MakeBlock(gpu, &gpu->cpu_cached_memory, gpu->spec.cpu_cached_size,
                            DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached,
                            "no memory for the processor-cached textures");
        if (problem != NULL) {
            return problem;
        }
        if (dkMemBlockGetCpuAddr(gpu->cpu_cached_memory) == NULL) {
            return "the processor-cached texture block is not visible to the processor";
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
    gpu->bytes_per_pixel = GpuFormatBytes(spec->color_format);

    if (gpu->width == 0u || gpu->height == 0u || gpu->width > MAXIMUM_DIMENSION ||
        gpu->height > MAXIMUM_DIMENSION) {
        return Fail(gpu, "target size out of range");
    }
    if (gpu->bytes_per_pixel == 0u) {
        return Fail(gpu, "colour format is not one this file knows the size of");
    }
    if (spec->depth_format != DkImageFormat_None && GpuFormatBytes(spec->depth_format) != 4u) {
        return Fail(gpu, "depth format is not one of the four byte formats this file handles");
    }
    // The copy this file issues has no opinion about the pitch, since the linear stride
    // alignment binds only the 2D engine. Guarded anyway, so that every target stays usable by a
    // copy that is one flip flag away from reaching it.
    if ((gpu->width * gpu->bytes_per_pixel) % DK_IMAGE_LINEAR_STRIDE_ALIGNMENT != 0u) {
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
        &gpu->cpu_image_memory, &gpu->cpu_cached_memory, &gpu->cmd_memory,
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
///
/// Every piece of work any case puts on the graphics unit comes through here, which makes it the
/// one place to time: from closing the list to the queue standing idle again. Reading the clock
/// either side changes nothing the list does.
static void SubmitAndDrain(GpuContext *gpu, const char *during) {
    const uint64_t start = armGetSystemTick();
    dkQueueSubmitCommands(gpu->queue, dkCmdBufFinishList(gpu->cmdbuf));
    Drain(gpu, during);
    PerfAddWork(armTicksToNs(armGetSystemTick() - start));
}

/// Binds @p image, or the main target and its depth when NULL, with a viewport and a scissor
/// over all of it.
static void BindTarget(GpuContext *gpu, const DkImage *image, uint32_t width, uint32_t height) {
    DkImageView color;
    if (image == NULL) {
        dkImageViewDefaults(&color, &gpu->image);
        width = gpu->width;
        height = gpu->height;
    } else {
        dkImageViewDefaults(&color, image);
    }
    if (image == NULL && gpu->spec.depth_format != DkImageFormat_None) {
        DkImageView depth;
        dkImageViewDefaults(&depth, &gpu->depth_image);
        dkCmdBufBindRenderTarget(gpu->cmdbuf, &color, &depth);
    } else {
        dkCmdBufBindRenderTarget(gpu->cmdbuf, &color, NULL);
    }

    // Everything below would otherwise be whatever the queue was left holding, and an inherited
    // scissor or blend would turn a correct machine into a failure nobody could attribute. A
    // case that wants one of these changed says so after this call and before the submit.
    const DkViewport viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    dkCmdBufSetViewports(gpu->cmdbuf, 0, &viewport, 1);
    const DkScissor scissor = { 0, 0, width, height };
    dkCmdBufSetScissors(gpu->cmdbuf, 0, &scissor, 1);
}

/// The state every pass starts from, after its target.
static void BindSharedState(GpuContext *gpu) {
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

// The control lists GpuRawMethods hands the library: one header naming one command entry, the
// entry, and a return, which is the shape deko3d's own lists take (source/dk_ctrlcmd.h,
// CtrlCmdHeader and CtrlCmdGpfifoEntry, deko3d 350f2b0). The queue walks them when the list is
// submitted, so they only have to outlive the submit; a new list starts them over.
#define RAW_LISTS 64u
typedef struct {
    uint64_t header;      // type 3 (GpfifoList) in bits 0-7, one entry in bits 32-63
    uint64_t entry_iova;  // CtrlCmdGpfifoEntry: where the words are
    uint32_t entry_words; // how many
    uint32_t entry_flags; // none: prefetch allowed, no kick
    uint64_t end;         // type 0 (Return)
} RawCtrlList;
_Static_assert(sizeof(RawCtrlList) == 32, "a raw control list is four 64-bit words");
static RawCtrlList raw_lists[RAW_LISTS];
static uint32_t raw_lists_used;

void GpuBeginPass(GpuContext *gpu) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }

    dkCmdBufClear(gpu->cmdbuf);
    raw_lists_used = 0;

    // Only the caches that hold copies of memory somebody else wrote. The level two cache is
    // deliberately left out: it holds the render target the previous pass wrote, and dropping
    // those lines would lose a result a later case is about to read.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Shader |
                        DkInvalidateFlags_Descriptors);

    BindTarget(gpu, NULL, 0, 0);
    BindSharedState(gpu);
}

void GpuBeginList(GpuContext *gpu) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }
    dkCmdBufClear(gpu->cmdbuf);
    raw_lists_used = 0;
    BindTarget(gpu, NULL, 0, 0);
    BindSharedState(gpu);
}

void GpuSetTarget(GpuContext *gpu, const DkImage *image, uint32_t width, uint32_t height) {
    if (gpu == NULL || !gpu->ready) {
        return;
    }
    BindTarget(gpu, image, width, height);
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
static const uint8_t *CopyToReadback(GpuContext *gpu, const DkImageView *view, uint32_t width,
                                     uint32_t height) {
    dkCmdBufClear(gpu->cmdbuf);

    // The draw ran on the graphics engine and this copy runs on the copy engine. It orders, it
    // does not invalidate, because dropping a cache line here would throw away the very pixels
    // being collected.
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);

    const DkImageRect rect = { 0, 0, 0, width, height, 1 };

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
    return CopyToReadback(gpu, &source, gpu->width, gpu->height);
}

const uint8_t *GpuReadBackImage(GpuContext *gpu, const DkImage *image, uint32_t width,
                                uint32_t height) {
    return GpuReadBackImageBytes(gpu, image, width, height, TARGET_BYTES_PER_PIXEL);
}

const uint8_t *GpuReadBackImageBytes(GpuContext *gpu, const DkImage *image, uint32_t width,
                                     uint32_t height, uint32_t bytes_per_pixel) {
    if (gpu == NULL || !gpu->ready || image == NULL) {
        return NULL;
    }
    if (width == 0u || height == 0u || bytes_per_pixel == 0u || width > gpu->width ||
        height > gpu->height ||
        (uint64_t)width * height * bytes_per_pixel > (uint64_t)gpu->read_size) {
        Complain(gpu, "an image larger than the readback block was read back");
        return NULL;
    }
    DkImageView source;
    dkImageViewDefaults(&source, image);
    return CopyToReadback(gpu, &source, width, height);
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
    return CopyToReadback(gpu, &source, gpu->width, gpu->height);
}

// The header every module the shader compiler emits begins with. Written out rather than
// included, because the toolchain ships no header for it.
typedef struct {
    uint32_t magic; ///< 'DKSH', little endian
    uint32_t header_size;
    uint32_t control_size; ///< the control section, this header included, at the front of the file
    uint32_t code_size;    ///< the machine code, immediately after the control section
    uint32_t programs_offset; ///< where the table of programs starts, inside the control section
    uint32_t program_count;
} ShaderModuleHeader;

#define SHADER_MODULE_MAGIC 0x48534B44u

// How many bytes deko3d reads out of the table for one program: it copies 64 bytes into the
// DkShader and indexes the table by the program id times that.
#define SHADER_PROGRAM_ENTRY_SIZE 64u

bool GpuLoadShader(GpuContext *gpu, DkShader *out, const void *dksh, uint32_t size) {
    if (gpu == NULL || !gpu->ready || out == NULL) {
        return false;
    }
    if (gpu->code_memory == NULL) {
        return Complain(gpu,
                        "a shader was loaded into a context whose spec asked for no code memory");
    }
    if (dksh == NULL || size < sizeof(ShaderModuleHeader)) {
        return Complain(gpu, "the shader module is too small to hold its own header");
    }

    ShaderModuleHeader header;
    memcpy(&header, dksh, sizeof(header));
    if (header.magic != SHADER_MODULE_MAGIC || header.header_size < sizeof(header) ||
        header.control_size < header.header_size || header.code_size == 0u ||
        (uint64_t)header.control_size + header.code_size > size) {
        return Complain(gpu, "the shader module's header does not describe the bytes after it");
    }
    // deko3d does not check and reads 64 bytes out of the table, so without this a truncated
    // module is a read past the end of the blob rather than a reported case.
    if (header.program_count == 0u ||
        (uint64_t)header.programs_offset + SHADER_PROGRAM_ENTRY_SIZE > header.control_size) {
        return Complain(gpu, "the shader module's program table is not inside its control "
                             "section");
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

    // No check on the way out, and not an omission: dkShaderIsValid only compares back a magic
    // that dkShaderInitialize wrote unconditionally, so it would answer yes whatever it was
    // handed. Everything worth refusing about a module is refused above.
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

bool GpuCreateTexture(GpuContext *gpu, DkImage *out, DkImageLayout *layout, DkImageFormat format,
                      uint32_t width, uint32_t height) {
    return GpuCreateImage(gpu, out, layout, format, width, height, 0, 0, NULL);
}

bool GpuCreateImage(GpuContext *gpu, DkImage *out, DkImageLayout *layout, DkImageFormat format,
                    uint32_t width, uint32_t height, uint32_t flags, uint32_t pitch_or_tile,
                    uint8_t **cpu_bytes) {
    if (gpu == NULL || !gpu->ready) {
        return false;
    }
    DkMemBlock block = cpu_bytes != NULL ? gpu->cpu_image_memory : gpu->extra_image_memory;
    uint32_t *used = cpu_bytes != NULL ? &gpu->cpu_image_used : &gpu->extra_image_used;
    if (block == NULL) {
        return Complain(gpu, cpu_bytes != NULL
                                 ? "an image the processor writes, and the spec asked for no "
                                   "processor-visible image memory"
                                 : "an image, and the spec asked for no image memory");
    }
    uint32_t offset = 0;
    const char *problem = PlaceImageAt(gpu, out, layout, format, width, height, flags,
                                       pitch_or_tile, block, used, &offset);
    if (problem != NULL) {
        return Complain(gpu, problem);
    }
    if (cpu_bytes != NULL) {
        *cpu_bytes = (uint8_t *)dkMemBlockGetCpuAddr(block) + offset;
    }
    return true;
}

bool GpuCreateCachedLinearImage(GpuContext *gpu, DkImage *out, DkImageLayout *layout,
                                DkImageFormat format, uint32_t width, uint32_t height,
                                uint8_t **cpu_bytes, uint32_t *stride) {
    if (gpu == NULL || !gpu->ready || cpu_bytes == NULL || stride == NULL) {
        return false;
    }
    if (gpu->cpu_cached_memory == NULL) {
        return Complain(gpu, "a processor-cached image, and the spec asked for no "
                             "processor-cached memory");
    }
    // Stated rather than left to the library, so the processor writes rows at a distance the
    // caller can be told.
    *stride = RoundUpTo(width * TARGET_BYTES_PER_PIXEL, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    uint32_t offset = 0;
    const char *problem =
        PlaceImageAt(gpu, out, layout, format, width, height, DkImageFlags_PitchLinear, *stride,
                     gpu->cpu_cached_memory, &gpu->cpu_cached_used, &offset);
    if (problem != NULL) {
        return Complain(gpu, problem);
    }
    *cpu_bytes = (uint8_t *)dkMemBlockGetCpuAddr(gpu->cpu_cached_memory) + offset;
    return true;
}

void GpuFlushCachedImage(GpuContext *gpu, const uint8_t *bytes, uint32_t size) {
    if (gpu == NULL || gpu->cpu_cached_memory == NULL || bytes == NULL) {
        return;
    }
    const uint8_t *const base = (const uint8_t *)dkMemBlockGetCpuAddr(gpu->cpu_cached_memory);
    dkMemBlockFlushCpuCache(gpu->cpu_cached_memory, (uint32_t)(bytes - base), size);
}

bool GpuCreateAliasedImages(GpuContext *gpu, DkImage *first, DkImageFormat first_format,
                            DkImage *second, DkImageFormat second_format, uint32_t width,
                            uint32_t height, uint32_t flags) {
    if (gpu == NULL || !gpu->ready || gpu->extra_image_memory == NULL) {
        return false;
    }
    DkImageLayout first_layout;
    DkImageLayout second_layout;
    uint32_t offset = 0;
    const char *problem =
        PlaceImageAt(gpu, first, &first_layout, first_format, width, height, flags, 0,
                     gpu->extra_image_memory, &gpu->extra_image_used, &offset);
    if (problem != NULL) {
        return Complain(gpu, problem);
    }
    LayOut(gpu, &second_layout, second_format, width, height, flags, 0);
    if (dkImageLayoutGetSize(&second_layout) != dkImageLayoutGetSize(&first_layout) ||
        dkImageLayoutGetAlignment(&second_layout) != dkImageLayoutGetAlignment(&first_layout)) {
        return Complain(gpu, "the two formats lay the same image out differently, so one is not "
                             "a view of the other");
    }
    dkImageInitialize(second, &second_layout, gpu->extra_image_memory, offset);
    return true;
}

bool GpuStagePixels(GpuContext *gpu, uint32_t width, uint32_t height, const void *pixels,
                    DkCopyBuf *out) {
    if (gpu == NULL || !gpu->ready || out == NULL) {
        return false;
    }
    // The copy engine wants the linear side's pitch aligned, and a four texel row is half of
    // that alignment, so the staging rows are padded out and the pitch is stated rather than
    // left at the packed default. That field is a byte count and not a pixel count, which the
    // header does not say.
    const uint32_t row_bytes = width * TARGET_BYTES_PER_PIXEL;
    const uint32_t pitch = RoundUpTo(row_bytes, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    DkGpuAddr staging_addr = 0;
    uint8_t *const staging = (uint8_t *)GpuAllocData(gpu, pitch * height, DK_MEMBLOCK_ALIGNMENT,
                                                     &staging_addr);
    if (staging == NULL) {
        return Complain(gpu, "the scratch block ran out of room to stage a texture upload");
    }
    memset(staging, 0, pitch * height);
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(staging + (size_t)y * pitch, (const uint8_t *)pixels + (size_t)y * row_bytes,
               row_bytes);
    }
    // The stride between depth slices, a byte count like the one before it. Left at zero the
    // library fills it in, which is what this copy wants; a row count there would be wrong and
    // would look right.
    out->addr = staging_addr;
    out->rowLength = pitch;
    out->imageHeight = 0;
    return true;
}

bool GpuUploadTexture(GpuContext *gpu, const DkImage *image, DkImageFormat format, uint32_t width,
                      uint32_t height, const void *pixels) {
    if (gpu == NULL || !gpu->ready) {
        return false;
    }
    if (GpuFormatBytes(format) != TARGET_BYTES_PER_PIXEL) {
        return Complain(gpu, "a texture was uploaded in a format this file cannot size");
    }
    DkCopyBuf source;
    if (!GpuStagePixels(gpu, width, height, pixels, &source)) {
        return false;
    }

    dkCmdBufClear(gpu->cmdbuf);
    dkCmdBufBarrier(gpu->cmdbuf, DkBarrier_Full, 0);

    DkImageView destination;
    dkImageViewDefaults(&destination, image);
    const DkImageRect rect = { 0, 0, 0, width, height, 1 };
    dkCmdBufCopyBufferToImage(gpu->cmdbuf, &source, &destination, &rect, 0);

    SubmitAndDrain(gpu, "texture upload");
    return gpu->ready;
}

bool GpuDrawTriangles(GpuContext *gpu, const DkShader *vertex_shader,
                      const DkShader *fragment_shader, const GpuVertex *vertices,
                      uint32_t vertex_count) {
    if (vertex_count % 3u != 0u ||
        !GpuBindTriangleState(gpu, vertex_shader, fragment_shader, vertices, vertex_count)) {
        return false;
    }
    dkCmdBufDraw(gpu->cmdbuf, DkPrimitive_Triangles, vertex_count, 1, 0, 0);
    return true;
}

bool GpuBindTriangleState(GpuContext *gpu, const DkShader *vertex_shader,
                          const DkShader *fragment_shader, const GpuVertex *vertices,
                          uint32_t vertex_count) {
    if (gpu == NULL || !gpu->ready || vertices == NULL || vertex_count == 0u) {
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
    return true;
}

bool GpuRawMethods(GpuContext *gpu, const uint32_t *words, uint32_t count) {
    if (gpu == NULL || !gpu->ready || words == NULL || count == 0u) {
        return false;
    }
    if (raw_lists_used == RAW_LISTS) {
        return Complain(gpu, "a list asked for more raw command blocks than gpu.c keeps");
    }
    DkGpuAddr address = 0;
    void *const destination = GpuAllocData(gpu, count * 4u, 4u, &address);
    if (destination == NULL) {
        return Complain(gpu, "the scratch block ran out of room for raw command words");
    }
    memcpy(destination, words, count * 4u);

    RawCtrlList *const list = &raw_lists[raw_lists_used++];
    list->header = 3u | ((uint64_t)1u << 32);
    list->entry_iova = address;
    list->entry_words = count;
    list->entry_flags = 0u;
    list->end = 0u;
    dkCmdBufCallList(gpu->cmdbuf, (DkCmdList)(uintptr_t)list);
    return true;
}
