// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <deko3d.h>
#include <switch.h>

#include "gpu.h"
#include "nvnlayer.h"

// NVN on deko3d. Each NVN object is storage the program owns, of the size nvn/nvn.h gives it;
// the layer keeps its own record at the start of it, as a driver keeps its fields there, and
// nothing else reads those bytes: a case asks every question through an NVN function, since
// under ScarletNX the same storage holds ScarletNX's record instead.
//
// deko3d stands for the driver in every choice NVN leaves to it, and where the two disagree the
// layer takes NVN's word: the device starts with NVN's lower left origin and near plane at -w, a
// texture can be a render target without saying so, and a pool can hold textures whatever its
// flags. A choice NVN documents and this layer has not made yet is refused with its value in
// NvnLayerError, never filled in with a guess.

// ---- the failure a case reports

static char layer_error[256];

const char *NvnLayerError(void) {
    return layer_error;
}

void NvnLayerClearError(void) {
    layer_error[0] = '\0';
}

/// Keeps the first failure since the last clear: whatever fails after it is usually its
/// consequence, and the first one is the one worth a line in the report.
static void Complain(const char *format, ...) {
    if (layer_error[0] != '\0') {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(layer_error, sizeof(layer_error), format, args);
    va_end(args);
}

// ---- what the layer keeps in each NVN object

#define MAGIC_DEVICE 0x4e4c4456u  // "VDLN"
#define MAGIC_QUEUE 0x4e4c5155u   // "UQLN"
#define MAGIC_POOL 0x4e4c504fu    // "OPLN"
#define MAGIC_TEXTURE 0x4e4c5458u // "XTLN"
#define MAGIC_CMDBUF 0x4e4c4342u  // "BCLN"
#define MAGIC_WINDOW 0x4e4c5749u  // "IWLN"

// NVN's windows take two to four textures (nvnDeviceGetInteger's MIN and MAX_TEXTURES_PER_WINDOW).
#define WINDOW_TEXTURES_MAX 4
// deko3d's limit on colour targets, which is NVN's too.
#define COLOR_TARGETS_MAX 8

typedef struct {
    uint32_t magic;
    DkDevice device;
} LayerDevice;

typedef struct {
    int flags;
} LayerDeviceBuilder;

typedef struct {
    NVNdevice *device;
} LayerQueueBuilder;

typedef struct {
    uint32_t magic;
    DkQueue queue;
} LayerQueue;

typedef struct {
    NVNdevice *device;
    void *storage;
    size_t size;
    int flags;
} LayerPoolBuilder;

typedef struct {
    uint32_t magic;
    DkMemBlock block;
} LayerPool;

typedef struct {
    NVNdevice *device;
    int flags;
    NVNtextureTarget target;
    NVNformat format;
    int width;
    int height;
    NVNmemoryPool *pool;
    ptrdiff_t offset;
} LayerTextureBuilder;

typedef struct {
    DkImage image; // first: deko3d wants it at its own alignment, which the object's start has
    uint32_t magic;
} LayerTexture;

typedef struct {
    uint32_t magic;
    DkCmdBuf cmdbuf;
} LayerCommandBuffer;

typedef struct {
    NVNdevice *device;
    NVNnativeWindow native;
    int count;
    NVNtexture *const *textures;
} LayerWindowBuilder;

typedef struct {
    uint32_t magic;
    DkSwapchain swapchain;
} LayerWindow;

_Static_assert(sizeof(LayerDevice) <= sizeof(NVNdevice), "the device record fits NVNdevice");
_Static_assert(sizeof(LayerDeviceBuilder) <= sizeof(NVNdeviceBuilder), "fits NVNdeviceBuilder");
_Static_assert(sizeof(LayerQueueBuilder) <= sizeof(NVNqueueBuilder), "fits NVNqueueBuilder");
_Static_assert(sizeof(LayerQueue) <= sizeof(NVNqueue), "fits NVNqueue");
_Static_assert(sizeof(LayerPoolBuilder) <= sizeof(NVNmemoryPoolBuilder), "fits the builder");
_Static_assert(sizeof(LayerPool) <= sizeof(NVNmemoryPool), "fits NVNmemoryPool");
_Static_assert(sizeof(LayerTextureBuilder) <= sizeof(NVNtextureBuilder), "fits the builder");
_Static_assert(sizeof(LayerTexture) <= sizeof(NVNtexture), "fits NVNtexture");
_Static_assert(sizeof(LayerCommandBuffer) <= sizeof(NVNcommandBuffer), "fits NVNcommandBuffer");
_Static_assert(sizeof(LayerWindowBuilder) <= sizeof(NVNwindowBuilder), "fits the builder");
_Static_assert(sizeof(LayerWindow) <= sizeof(NVNwindow), "fits NVNwindow");

/// The layer's record in @p object, or NULL with a complaint when the object is not on the eight
/// byte boundary NVN asks of its objects: nvn/nvn.h declares them as bytes, so the compiler does
/// not place them there by itself, and a case that forgot gets told rather than a misaligned load.
static void *Record(const void *object, const char *what) {
    if (object == NULL) {
        Complain("%s: no object", what);
        return NULL;
    }
    if (((uintptr_t)object & 7u) != 0u) {
        Complain("%s: the object at %p is not 8-byte aligned, as NVN objects must be", what,
                 object);
        return NULL;
    }
    return (void *)(uintptr_t)object;
}

static LayerDevice *DeviceOf(const NVNdevice *device, const char *what) {
    LayerDevice *const record = Record(device, what);
    if (record != NULL && record->magic != MAGIC_DEVICE) {
        Complain("%s: the device was not initialized", what);
        return NULL;
    }
    return record;
}

static LayerPool *PoolOf(const NVNmemoryPool *pool, const char *what) {
    LayerPool *const record = Record(pool, what);
    if (record != NULL && record->magic != MAGIC_POOL) {
        Complain("%s: the memory pool was not initialized", what);
        return NULL;
    }
    return record;
}

static LayerTexture *TextureOf(const NVNtexture *texture, const char *what) {
    LayerTexture *const record = Record(texture, what);
    if (record != NULL && record->magic != MAGIC_TEXTURE) {
        Complain("%s: the texture was not initialized", what);
        return NULL;
    }
    return record;
}

static LayerQueue *QueueOf(const NVNqueue *queue, const char *what) {
    LayerQueue *const record = Record(queue, what);
    if (record != NULL && record->magic != MAGIC_QUEUE) {
        Complain("%s: the queue was not initialized", what);
        return NULL;
    }
    return record;
}

static LayerCommandBuffer *CmdOf(const NVNcommandBuffer *cmd, const char *what) {
    LayerCommandBuffer *const record = Record(cmd, what);
    if (record != NULL && record->magic != MAGIC_CMDBUF) {
        Complain("%s: the command buffer was not initialized", what);
        return NULL;
    }
    return record;
}

static LayerWindow *WindowOf(const NVNwindow *window, const char *what) {
    LayerWindow *const record = Record(window, what);
    if (record != NULL && record->magic != MAGIC_WINDOW) {
        Complain("%s: the window was not initialized", what);
        return NULL;
    }
    return record;
}

// ---- deko3d's constructors, which abort on a failure: run under gpu.c's AbortGuarded, their
// arguments passed through here because a guarded call takes none. One thread calls the layer.

static struct {
    DkDeviceMaker device_maker;
    DkDevice device;
    DkQueueMaker queue_maker;
    DkQueue queue;
    DkMemBlockMaker block_maker;
    DkMemBlock block;
    DkSwapchainMaker swapchain_maker;
    DkSwapchain swapchain;
    DkQueue acquire_queue;
    DkSwapchain acquire_swapchain;
    int acquired;
} pending;

static void MakeDevice(void) {
    pending.device = dkDeviceCreate(&pending.device_maker);
}

static void MakeQueue(void) {
    pending.queue = dkQueueCreate(&pending.queue_maker);
}

static void MakeBlock(void) {
    pending.block = dkMemBlockCreate(&pending.block_maker);
}

static void MakeSwapchain(void) {
    pending.swapchain = dkSwapchainCreate(&pending.swapchain_maker);
}

static void AcquireImage(void) {
    pending.acquired = dkQueueAcquireImage(pending.acquire_queue, pending.acquire_swapchain);
}

/// Runs @p call, and on an abort says which call and with what result.
static bool Guarded(void (*call)(void), const char *what) {
    uint32_t result = 0;
    if (AbortGuarded(call, &result)) {
        return true;
    }
    Complain("%s ended in deko3d's abort, result 0x%x", what, (unsigned)result);
    return false;
}

// ---- the device

static void Nvnl_nvnDeviceBuilderSetDefaults(NVNdeviceBuilder *builder) {
    memset(builder, 0, sizeof(*builder));
}

static void Nvnl_nvnDeviceBuilderSetFlags(NVNdeviceBuilder *builder, int flags) {
    LayerDeviceBuilder *const record = Record(builder, "nvnDeviceBuilderSetFlags");
    if (record != NULL) {
        record->flags = flags;
    }
}

static NVNboolean Nvnl_nvnDeviceInitialize(NVNdevice *device, const NVNdeviceBuilder *builder) {
    LayerDevice *const record = Record(device, "nvnDeviceInitialize");
    const LayerDeviceBuilder *const from = Record(builder, "nvnDeviceInitialize");
    if (record == NULL || from == NULL) {
        return 0;
    }
    // The flags all ask for the debug layer or for finalizes deferred by age, which this layer
    // does not have; a case that sets one is told, rather than run without it.
    if (from->flags != 0) {
        Complain("nvnDeviceInitialize: device flags 0x%x, and the layer takes 0 only",
                 (unsigned)from->flags);
        return 0;
    }
    dkDeviceMakerDefaults(&pending.device_maker);
    // NVN's defaults (nvnDeviceSetWindowOriginMode, nvnDeviceSetDepthMode): the window's origin
    // in its lower left corner and the near plane at -w, OpenGL's conventions. deko3d sets them
    // once, at creation, so the two setters are not in this layer.
    pending.device_maker.flags = DkDeviceFlags_OriginLowerLeft | DkDeviceFlags_DepthMinusOneToOne;
    pending.device = NULL;
    if (!Guarded(MakeDevice, "nvnDeviceInitialize: dkDeviceCreate")) {
        return 0;
    }
    if (pending.device == NULL) {
        Complain("nvnDeviceInitialize: dkDeviceCreate gave no device");
        return 0;
    }
    memset(device, 0, sizeof(*device));
    record->magic = MAGIC_DEVICE;
    record->device = pending.device;
    return 1;
}

static void Nvnl_nvnDeviceFinalize(NVNdevice *device) {
    LayerDevice *const record = DeviceOf(device, "nvnDeviceFinalize");
    if (record == NULL) {
        return;
    }
    dkDeviceDestroy(record->device);
    record->magic = 0;
}

// nvnDeviceGetInteger, in assembly, in the shape the console's NVN drivers give it: a bounded
// switch on the item through a byte table scaled by four from an ADR, its cases setting w0. That
// is the shape ScarletNX reads a game's API version out of, from the code, before the game asks
// (its nvn_driver.c); a C switch compiles to another shape, and ScarletNX would then warn and
// answer from its own table. Items 0 and 1, the API's major and minor version, are the two
// constants here; every other item goes to NvnLayerDeviceInfo, which ScarletNX's reader sees as
// an item it cannot read from code, and answers from its own table.
//
// The table holds the item's case as a word offset from label 2: 0 for the major version, 2 for
// the minor, 4 for the rest. The item is zero-extended first, since the callee owns the upper
// half of x1 and the table load indexes with all of it.
#define LAYER_STRING(x) #x
#define LAYER_EXPAND(x) LAYER_STRING(x)
void NvnLayerDeviceGetInteger(const NVNdevice *device, NVNdeviceInfo item, int *value);
void NvnLayerDeviceInfo(const NVNdevice *device, int item, int *value);
__asm__(".section .text.NvnLayerDeviceGetInteger,\"ax\",%progbits\n"
        ".global NvnLayerDeviceGetInteger\n"
        ".hidden NvnLayerDeviceGetInteger\n"
        ".type NvnLayerDeviceGetInteger, %function\n"
        ".p2align 2\n"
        "NvnLayerDeviceGetInteger:\n"
        ".cfi_startproc\n"
        "    mov w1, w1\n"
        "    cmp w1, #0x68\n"
        "    b.hi 1f\n"
        "    adr x8, 3f\n"
        "    ldrb w9, [x8, x1]\n"
        "    adr x10, 2f\n"
        "    add x10, x10, x9, lsl #2\n"
        "    br x10\n"
        "2:  mov w0, #" LAYER_EXPAND(NVN_LAYER_API_MAJOR) "\n"
        "    b 4f\n"
        "    mov w0, #" LAYER_EXPAND(NVN_LAYER_API_MINOR) "\n"
        "    b 4f\n"
        "1:  b NvnLayerDeviceInfo\n"
        "4:  str w0, [x2]\n"
        "    ret\n"
        ".cfi_endproc\n"
        "3:  .byte 0, 2\n"
        "    .fill 0x67, 1, 4\n"
        ".size NvnLayerDeviceGetInteger, . - NvnLayerDeviceGetInteger\n"
        ".text\n");

/// The items nvnDeviceGetInteger does not answer with a constant in its switch: the ones whose
/// deko3d value the layer knows, from deko3d.h. Anything else is refused with its number, as a
/// value nobody looked up yet; it reads back as 0.
void NvnLayerDeviceInfo(const NVNdevice *device, int item, int *value) {
    (void)device;
    int answer;
    switch (item) {
    case NVN_DEVICE_INFO_UNIFORM_BUFFER_ALIGNMENT:
        answer = DK_UNIFORM_BUF_ALIGNMENT;
        break;
    case NVN_DEVICE_INFO_MAX_UNIFORM_BUFFER_SIZE:
        answer = DK_UNIFORM_BUF_MAX_SIZE;
        break;
    case NVN_DEVICE_INFO_COLOR_BUFFER_BINDINGS:
        answer = DK_MAX_RENDER_TARGETS;
        break;
    case NVN_DEVICE_INFO_VERTEX_BUFFER_BINDINGS:
        answer = DK_MAX_VERTEX_BUFFERS;
        break;
    case NVN_DEVICE_INFO_TEXTURE_DESCRIPTOR_SIZE:
    case NVN_DEVICE_INFO_SAMPLER_DESCRIPTOR_SIZE:
        answer = 32;
        break;
    case NVN_DEVICE_INFO_COMMAND_BUFFER_COMMAND_ALIGNMENT:
        answer = DK_CMDMEM_ALIGNMENT;
        break;
    case NVN_DEVICE_INFO_LINEAR_TEXTURE_STRIDE_ALIGNMENT:
        answer = DK_IMAGE_LINEAR_STRIDE_ALIGNMENT;
        break;
    case NVN_DEVICE_INFO_MEMORY_POOL_PAGE_SIZE:
        answer = DK_MEMBLOCK_ALIGNMENT;
        break;
    case NVN_DEVICE_INFO_MIN_TEXTURES_PER_WINDOW:
        answer = 2;
        break;
    case NVN_DEVICE_INFO_MAX_TEXTURES_PER_WINDOW:
        answer = WINDOW_TEXTURES_MAX;
        break;
    default:
        Complain("nvnDeviceGetInteger: item 0x%x has no answer in the layer yet: it reads 0",
                 (unsigned)item);
        answer = 0;
        break;
    }
    *value = answer;
}

// ---- the queue

static void Nvnl_nvnQueueBuilderSetDefaults(NVNqueueBuilder *builder) {
    // The device stays, as NVN's own builder keeps it.
    LayerQueueBuilder *const record = Record(builder, "nvnQueueBuilderSetDefaults");
    if (record == NULL) {
        return;
    }
    NVNdevice *const device = record->device;
    memset(builder, 0, sizeof(*builder));
    record->device = device;
}

static void Nvnl_nvnQueueBuilderSetDevice(NVNqueueBuilder *builder, NVNdevice *device) {
    LayerQueueBuilder *const record = Record(builder, "nvnQueueBuilderSetDevice");
    if (record != NULL) {
        record->device = device;
    }
}

static NVNboolean Nvnl_nvnQueueInitialize(NVNqueue *queue, const NVNqueueBuilder *builder) {
    LayerQueue *const record = Record(queue, "nvnQueueInitialize");
    const LayerQueueBuilder *const from = Record(builder, "nvnQueueInitialize");
    if (record == NULL || from == NULL) {
        return 0;
    }
    const LayerDevice *const device = DeviceOf(from->device, "nvnQueueInitialize");
    if (device == NULL) {
        return 0;
    }
    // deko3d's defaults: a graphics and compute queue, as every NVN queue is.
    dkQueueMakerDefaults(&pending.queue_maker, device->device);
    pending.queue = NULL;
    if (!Guarded(MakeQueue, "nvnQueueInitialize: dkQueueCreate")) {
        return 0;
    }
    memset(queue, 0, sizeof(*queue));
    record->magic = MAGIC_QUEUE;
    record->queue = pending.queue;
    return 1;
}

static void Nvnl_nvnQueueFinalize(NVNqueue *queue) {
    LayerQueue *const record = QueueOf(queue, "nvnQueueFinalize");
    if (record == NULL) {
        return;
    }
    dkQueueDestroy(record->queue);
    record->magic = 0;
}

static void Nvnl_nvnQueueSubmitCommands(NVNqueue *queue, int count,
                                        const NVNcommandHandle *handles) {
    LayerQueue *const record = QueueOf(queue, "nvnQueueSubmitCommands");
    if (record == NULL) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        dkQueueSubmitCommands(record->queue, (DkCmdList)handles[i]);
    }
}

static void Nvnl_nvnQueueFlush(NVNqueue *queue) {
    LayerQueue *const record = QueueOf(queue, "nvnQueueFlush");
    if (record != NULL) {
        dkQueueFlush(record->queue);
    }
}

static void Nvnl_nvnQueueFinish(NVNqueue *queue) {
    LayerQueue *const record = QueueOf(queue, "nvnQueueFinish");
    if (record == NULL) {
        return;
    }
    dkQueueWaitIdle(record->queue);
    // NVN has nvnQueueGetError for this; the layer says it where a case reads it.
    if (dkQueueIsInErrorState(record->queue)) {
        Complain("nvnQueueFinish: the queue faulted on the graphics unit");
    }
}

static NVNqueueAcquireTextureResult Nvnl_nvnQueueAcquireTexture(NVNqueue *queue,
                                                                NVNwindow *window, int *index) {
    LayerQueue *const record = QueueOf(queue, "nvnQueueAcquireTexture");
    LayerWindow *const target = WindowOf(window, "nvnQueueAcquireTexture");
    if (record == NULL || target == NULL) {
        return NVN_QUEUE_ACQUIRE_TEXTURE_RESULT_NATIVE_ERROR;
    }
    pending.acquire_queue = record->queue;
    pending.acquire_swapchain = target->swapchain;
    pending.acquired = -1;
    if (!Guarded(AcquireImage, "nvnQueueAcquireTexture: dkQueueAcquireImage") ||
        pending.acquired < 0) {
        Complain("nvnQueueAcquireTexture: dkQueueAcquireImage gave slot %d", pending.acquired);
        return NVN_QUEUE_ACQUIRE_TEXTURE_RESULT_NATIVE_ERROR;
    }
    *index = pending.acquired;
    return NVN_QUEUE_ACQUIRE_TEXTURE_RESULT_SUCCESS;
}

static void Nvnl_nvnQueuePresentTexture(NVNqueue *queue, NVNwindow *window, int index) {
    LayerQueue *const record = QueueOf(queue, "nvnQueuePresentTexture");
    LayerWindow *const target = WindowOf(window, "nvnQueuePresentTexture");
    if (record != NULL && target != NULL) {
        dkQueuePresentImage(record->queue, target->swapchain, index);
    }
}

// ---- memory pools

static void Nvnl_nvnMemoryPoolBuilderSetDefaults(NVNmemoryPoolBuilder *builder) {
    memset(builder, 0, sizeof(*builder));
}

static void Nvnl_nvnMemoryPoolBuilderSetDevice(NVNmemoryPoolBuilder *builder, NVNdevice *device) {
    LayerPoolBuilder *const record = Record(builder, "nvnMemoryPoolBuilderSetDevice");
    if (record != NULL) {
        record->device = device;
    }
}

static void Nvnl_nvnMemoryPoolBuilderSetStorage(NVNmemoryPoolBuilder *builder, void *memory,
                                                size_t size) {
    LayerPoolBuilder *const record = Record(builder, "nvnMemoryPoolBuilderSetStorage");
    if (record != NULL) {
        record->storage = memory;
        record->size = size;
    }
}

static void Nvnl_nvnMemoryPoolBuilderSetFlags(NVNmemoryPoolBuilder *builder, int flags) {
    LayerPoolBuilder *const record = Record(builder, "nvnMemoryPoolBuilderSetFlags");
    if (record != NULL) {
        record->flags = flags;
    }
}

/// deko3d's access bits for one of NVN's three-way choices (no access, uncached, cached), or
/// false when the flags name more than one of them.
static bool Access(int flags, int none, int uncached, int cached, uint32_t *access) {
    const int named = flags & (none | uncached | cached);
    if (named == none) {
        *access = DkMemAccess_None;
    } else if (named == uncached) {
        *access = DkMemAccess_Uncached;
    } else if (named == cached) {
        *access = DkMemAccess_Cached;
    } else {
        return false;
    }
    return true;
}

static NVNboolean Nvnl_nvnMemoryPoolInitialize(NVNmemoryPool *pool,
                                               const NVNmemoryPoolBuilder *builder) {
    LayerPool *const record = Record(pool, "nvnMemoryPoolInitialize");
    const LayerPoolBuilder *const from = Record(builder, "nvnMemoryPoolInitialize");
    if (record == NULL || from == NULL) {
        return 0;
    }
    const LayerDevice *const device = DeviceOf(from->device, "nvnMemoryPoolInitialize");
    if (device == NULL) {
        return 0;
    }
    const int known = NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED |
                      NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_NO_ACCESS |
                      NVN_MEMORY_POOL_FLAGS_GPU_UNCACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                      NVN_MEMORY_POOL_FLAGS_SHADER_CODE | NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE;
    uint32_t cpu = 0;
    uint32_t gpu = 0;
    if ((from->flags & ~known) != 0 ||
        !Access(from->flags, NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS,
                NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED, NVN_MEMORY_POOL_FLAGS_CPU_CACHED, &cpu) ||
        !Access(from->flags, NVN_MEMORY_POOL_FLAGS_GPU_NO_ACCESS,
                NVN_MEMORY_POOL_FLAGS_GPU_UNCACHED, NVN_MEMORY_POOL_FLAGS_GPU_CACHED, &gpu)) {
        Complain("nvnMemoryPoolInitialize: pool flags 0x%x, which the layer does not take: one "
                 "processor and one graphics access each, shader code and compressible",
                 (unsigned)from->flags);
        return 0;
    }
    if (from->storage == NULL || ((uintptr_t)from->storage & (DK_MEMBLOCK_ALIGNMENT - 1)) != 0 ||
        from->size == 0 || (from->size & (DK_MEMBLOCK_ALIGNMENT - 1)) != 0 ||
        from->size > UINT32_MAX) {
        Complain("nvnMemoryPoolInitialize: storage %p of 0x%zx bytes, where NVN and deko3d take "
                 "a non-empty multiple of 0x%x bytes at that alignment",
                 from->storage, from->size, (unsigned)DK_MEMBLOCK_ALIGNMENT);
        return 0;
    }
    uint32_t flags = cpu << DkMemBlockFlags_CpuAccessShift | gpu << DkMemBlockFlags_GpuAccessShift;
    if ((from->flags & NVN_MEMORY_POOL_FLAGS_SHADER_CODE) != 0) {
        flags |= DkMemBlockFlags_Code;
    } else if (gpu != DkMemAccess_None) {
        // Any pool the graphics unit reaches can hold a texture in NVN; in deko3d that takes the
        // image mappings, the compressed one among them, which is what COMPRESSIBLE asks for.
        flags |= DkMemBlockFlags_Image;
    }
    dkMemBlockMakerDefaults(&pending.block_maker, device->device, (uint32_t)from->size);
    pending.block_maker.flags = flags;
    pending.block_maker.storage = from->storage;
    pending.block = NULL;
    if (!Guarded(MakeBlock, "nvnMemoryPoolInitialize: dkMemBlockCreate")) {
        return 0;
    }
    memset(pool, 0, sizeof(*pool));
    record->magic = MAGIC_POOL;
    record->block = pending.block;
    return 1;
}

static void Nvnl_nvnMemoryPoolFinalize(NVNmemoryPool *pool) {
    LayerPool *const record = PoolOf(pool, "nvnMemoryPoolFinalize");
    if (record == NULL) {
        return;
    }
    dkMemBlockDestroy(record->block);
    record->magic = 0;
}

static void *Nvnl_nvnMemoryPoolMap(const NVNmemoryPool *pool) {
    LayerPool *const record = PoolOf(pool, "nvnMemoryPoolMap");
    return record != NULL ? dkMemBlockGetCpuAddr(record->block) : NULL;
}

static NVNbufferAddress Nvnl_nvnMemoryPoolGetBufferAddress(const NVNmemoryPool *pool) {
    LayerPool *const record = PoolOf(pool, "nvnMemoryPoolGetBufferAddress");
    return record != NULL ? dkMemBlockGetGpuAddr(record->block) : 0;
}

static void Nvnl_nvnMemoryPoolFlushMappedRange(const NVNmemoryPool *pool, ptrdiff_t offset,
                                               size_t size) {
    LayerPool *const record = PoolOf(pool, "nvnMemoryPoolFlushMappedRange");
    if (record != NULL) {
        dkMemBlockFlushCpuCache(record->block, (uint32_t)offset, (uint32_t)size);
    }
}

// ---- textures

static void Nvnl_nvnTextureBuilderSetDefaults(NVNtextureBuilder *builder) {
    // NVN's defaults: a 2D texture of one texel, one level; the device stays, as NVN keeps it.
    LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderSetDefaults");
    if (record == NULL) {
        return;
    }
    NVNdevice *const device = record->device;
    memset(builder, 0, sizeof(*builder));
    record->device = device;
    record->target = NVN_TEXTURE_TARGET_2D;
    record->format = NVN_FORMAT_NONE;
    record->width = 1;
    record->height = 1;
}

static void Nvnl_nvnTextureBuilderSetDevice(NVNtextureBuilder *builder, NVNdevice *device) {
    LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderSetDevice");
    if (record != NULL) {
        record->device = device;
    }
}

static void Nvnl_nvnTextureBuilderSetFlags(NVNtextureBuilder *builder, int flags) {
    LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderSetFlags");
    if (record != NULL) {
        record->flags = flags;
    }
}

static void Nvnl_nvnTextureBuilderSetTarget(NVNtextureBuilder *builder, NVNtextureTarget target) {
    LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderSetTarget");
    if (record != NULL) {
        record->target = target;
    }
}

static void Nvnl_nvnTextureBuilderSetFormat(NVNtextureBuilder *builder, NVNformat format) {
    LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderSetFormat");
    if (record != NULL) {
        record->format = format;
    }
}

static void Nvnl_nvnTextureBuilderSetSize2D(NVNtextureBuilder *builder, int width, int height) {
    LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderSetSize2D");
    if (record != NULL) {
        record->width = width;
        record->height = height;
    }
}

static void Nvnl_nvnTextureBuilderSetStorage(NVNtextureBuilder *builder, NVNmemoryPool *pool,
                                             ptrdiff_t offset) {
    LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderSetStorage");
    if (record != NULL) {
        record->pool = pool;
        record->offset = offset;
    }
}

/// The deko3d format of an NVN one, or DkImageFormat_None for one the layer has not mapped.
static DkImageFormat FormatOf(NVNformat format) {
    switch (format) {
    case NVN_FORMAT_RGBA8:
        return DkImageFormat_RGBA8_Unorm;
    case NVN_FORMAT_BGRA8:
        return DkImageFormat_BGRA8_Unorm;
    default:
        return DkImageFormat_None;
    }
}

/// The layout @p builder describes, or false with a complaint naming what the layer does not
/// take yet: a target, a format or a flag, by its number.
static bool LayoutOf(const LayerTextureBuilder *builder, DkImageLayout *layout, const char *what) {
    const LayerDevice *const device = DeviceOf(builder->device, what);
    if (device == NULL) {
        return false;
    }
    if (builder->target != NVN_TEXTURE_TARGET_2D) {
        Complain("%s: texture target %d, and the layer takes 2D (%d) only", what,
                 (int)builder->target, (int)NVN_TEXTURE_TARGET_2D);
        return false;
    }
    const DkImageFormat format = FormatOf(builder->format);
    if (format == DkImageFormat_None) {
        Complain("%s: NVN format 0x%x has no deko3d format in the layer yet", what,
                 (unsigned)builder->format);
        return false;
    }
    const int known = NVN_TEXTURE_FLAGS_DISPLAY | NVN_TEXTURE_FLAGS_COMPRESSIBLE;
    if ((builder->flags & ~known) != 0) {
        Complain("%s: texture flags 0x%x, and the layer takes display and compressible only",
                 what, (unsigned)builder->flags);
        return false;
    }
    if (builder->width <= 0 || builder->height <= 0) {
        Complain("%s: a %dx%d texture", what, builder->width, builder->height);
        return false;
    }
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, device->device);
    maker.type = DkImageType_2D;
    maker.format = format;
    // Any NVN texture of a colour format can be drawn into, so every one here says so.
    maker.flags = DkImageFlags_UsageRender;
    if ((builder->flags & NVN_TEXTURE_FLAGS_DISPLAY) != 0) {
        maker.flags |= DkImageFlags_UsagePresent;
    }
    if ((builder->flags & NVN_TEXTURE_FLAGS_COMPRESSIBLE) != 0) {
        maker.flags |= DkImageFlags_HwCompression;
    }
    maker.dimensions[0] = (uint32_t)builder->width;
    maker.dimensions[1] = (uint32_t)builder->height;
    dkImageLayoutInitialize(layout, &maker);
    return true;
}

static size_t Nvnl_nvnTextureBuilderGetStorageSize(const NVNtextureBuilder *builder) {
    const LayerTextureBuilder *const record = Record(builder, "nvnTextureBuilderGetStorageSize");
    DkImageLayout layout;
    if (record == NULL || !LayoutOf(record, &layout, "nvnTextureBuilderGetStorageSize")) {
        return 0;
    }
    return (size_t)dkImageLayoutGetSize(&layout);
}

static size_t Nvnl_nvnTextureBuilderGetStorageAlignment(const NVNtextureBuilder *builder) {
    const LayerTextureBuilder *const record =
        Record(builder, "nvnTextureBuilderGetStorageAlignment");
    DkImageLayout layout;
    if (record == NULL || !LayoutOf(record, &layout, "nvnTextureBuilderGetStorageAlignment")) {
        return 0;
    }
    return (size_t)dkImageLayoutGetAlignment(&layout);
}

static NVNboolean Nvnl_nvnTextureInitialize(NVNtexture *texture,
                                            const NVNtextureBuilder *builder) {
    LayerTexture *const record = Record(texture, "nvnTextureInitialize");
    const LayerTextureBuilder *const from = Record(builder, "nvnTextureInitialize");
    if (record == NULL || from == NULL) {
        return 0;
    }
    const LayerPool *const pool = PoolOf(from->pool, "nvnTextureInitialize");
    DkImageLayout layout;
    if (pool == NULL || !LayoutOf(from, &layout, "nvnTextureInitialize")) {
        return 0;
    }
    if (from->offset < 0 || (uint64_t)from->offset + dkImageLayoutGetSize(&layout) >
                                dkMemBlockGetSize(pool->block)) {
        Complain("nvnTextureInitialize: 0x%llx bytes at offset 0x%llx, past the pool's 0x%x",
                 (unsigned long long)dkImageLayoutGetSize(&layout),
                 (unsigned long long)from->offset, (unsigned)dkMemBlockGetSize(pool->block));
        return 0;
    }
    memset(texture, 0, sizeof(*texture));
    dkImageInitialize(&record->image, &layout, pool->block, (uint32_t)from->offset);
    record->magic = MAGIC_TEXTURE;
    return 1;
}

static void Nvnl_nvnTextureFinalize(NVNtexture *texture) {
    LayerTexture *const record = TextureOf(texture, "nvnTextureFinalize");
    if (record != NULL) {
        record->magic = 0;
    }
}

// ---- command buffers

static NVNboolean Nvnl_nvnCommandBufferInitialize(NVNcommandBuffer *cmd, NVNdevice *device) {
    LayerCommandBuffer *const record = Record(cmd, "nvnCommandBufferInitialize");
    const LayerDevice *const owner = DeviceOf(device, "nvnCommandBufferInitialize");
    if (record == NULL || owner == NULL) {
        return 0;
    }
    DkCmdBufMaker maker;
    dkCmdBufMakerDefaults(&maker, owner->device);
    // No memory callback: a case gives a command buffer the memory it needs up front, and
    // deko3d ends the program when a list runs out, which the case's journal then names.
    const DkCmdBuf made = dkCmdBufCreate(&maker);
    if (made == NULL) {
        Complain("nvnCommandBufferInitialize: dkCmdBufCreate gave no command buffer");
        return 0;
    }
    memset(cmd, 0, sizeof(*cmd));
    record->magic = MAGIC_CMDBUF;
    record->cmdbuf = made;
    return 1;
}

static void Nvnl_nvnCommandBufferFinalize(NVNcommandBuffer *cmd) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferFinalize");
    if (record == NULL) {
        return;
    }
    dkCmdBufDestroy(record->cmdbuf);
    record->magic = 0;
}

static void Nvnl_nvnCommandBufferAddCommandMemory(NVNcommandBuffer *cmd,
                                                  const NVNmemoryPool *pool, ptrdiff_t offset,
                                                  size_t size) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferAddCommandMemory");
    const LayerPool *const from = PoolOf(pool, "nvnCommandBufferAddCommandMemory");
    if (record != NULL && from != NULL) {
        dkCmdBufAddMemory(record->cmdbuf, from->block, (uint32_t)offset, (uint32_t)size);
    }
}

static void Nvnl_nvnCommandBufferAddControlMemory(NVNcommandBuffer *cmd, void *memory,
                                                  size_t size) {
    // deko3d keeps a list's control words in its command memory, so the processor-side memory
    // NVN asks for has no use here; taken and left alone.
    (void)memory;
    (void)size;
    (void)CmdOf(cmd, "nvnCommandBufferAddControlMemory");
}

static void Nvnl_nvnCommandBufferBeginRecording(NVNcommandBuffer *cmd) {
    // A deko3d list is whatever was recorded since the last one finished, so a recording starts
    // by itself.
    (void)CmdOf(cmd, "nvnCommandBufferBeginRecording");
}

static NVNcommandHandle Nvnl_nvnCommandBufferEndRecording(NVNcommandBuffer *cmd) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferEndRecording");
    return record != NULL ? (NVNcommandHandle)dkCmdBufFinishList(record->cmdbuf) : 0;
}

static void Nvnl_nvnCommandBufferSetRenderTargets(NVNcommandBuffer *cmd, int count,
                                                  const NVNtexture *const *colors,
                                                  const NVNtextureView *const *views,
                                                  const NVNtexture *depth,
                                                  const NVNtextureView *depth_view) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferSetRenderTargets");
    if (record == NULL) {
        return;
    }
    if (count < 0 || count > COLOR_TARGETS_MAX || depth != NULL || depth_view != NULL) {
        Complain("nvnCommandBufferSetRenderTargets: %d colour targets and %s depth target, and "
                 "the layer takes 0 to %d colour targets and no depth yet",
                 count, depth != NULL ? "a" : "no", COLOR_TARGETS_MAX);
        return;
    }
    DkImageView targets[COLOR_TARGETS_MAX];
    DkImageView const *bound[COLOR_TARGETS_MAX];
    for (int i = 0; i < count; ++i) {
        if (views != NULL && views[i] != NULL) {
            Complain("nvnCommandBufferSetRenderTargets: a texture view on target %d, which the "
                     "layer does not take yet",
                     i);
            return;
        }
        const LayerTexture *const texture =
            TextureOf(colors[i], "nvnCommandBufferSetRenderTargets");
        if (texture == NULL) {
            return;
        }
        dkImageViewDefaults(&targets[i], &texture->image);
        bound[i] = &targets[i];
    }
    dkCmdBufBindRenderTargets(record->cmdbuf, bound, (uint32_t)count, NULL);
}

static void Nvnl_nvnCommandBufferSetViewport(NVNcommandBuffer *cmd, int x, int y, int width,
                                             int height) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferSetViewport");
    if (record == NULL) {
        return;
    }
    // NVN sets the depth range apart (nvnCommandBufferSetDepthRange), 0 to 1 by default; deko3d
    // takes it with the rectangle.
    const DkViewport viewport = { (float)x, (float)y, (float)width, (float)height, 0.0f, 1.0f };
    dkCmdBufSetViewports(record->cmdbuf, 0, &viewport, 1);
}

static void Nvnl_nvnCommandBufferSetScissor(NVNcommandBuffer *cmd, int x, int y, int width,
                                            int height) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferSetScissor");
    if (record == NULL) {
        return;
    }
    const DkScissor scissor = { (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height };
    dkCmdBufSetScissors(record->cmdbuf, 0, &scissor, 1);
}

static void Nvnl_nvnCommandBufferClearColor(NVNcommandBuffer *cmd, int index, const float *rgba,
                                            int mask) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferClearColor");
    if (record == NULL) {
        return;
    }
    // NVN's channel mask bits (R 1, G 2, B 4, A 8) are deko3d's.
    dkCmdBufClearColorFloat(record->cmdbuf, (uint32_t)index, (uint32_t)mask & DkColorMask_RGBA,
                            rgba[0], rgba[1], rgba[2], rgba[3]);
}

static void Nvnl_nvnCommandBufferBarrier(NVNcommandBuffer *cmd, int bits) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferBarrier");
    if (record == NULL) {
        return;
    }
    // Every order NVN can ask for becomes deko3d's full barrier, a wait for all the work before
    // it: what a case reads back is then the ordered answer whichever order it asked for. The
    // invalidations map one to one.
    const int order = NVN_BARRIER_ORDER_PRIMITIVES_BIT | NVN_BARRIER_ORDER_FRAGMENTS_BIT |
                      NVN_BARRIER_ORDER_FRAGMENTS_TILED_BIT | NVN_BARRIER_ORDER_INDIRECT_DATA_BIT;
    uint32_t invalidate = 0;
    if ((bits & NVN_BARRIER_INVALIDATE_TEXTURE_BIT) != 0) {
        invalidate |= DkInvalidateFlags_Image;
    }
    if ((bits & NVN_BARRIER_INVALIDATE_SHADER_BIT) != 0) {
        invalidate |= DkInvalidateFlags_Shader;
    }
    if ((bits & NVN_BARRIER_INVALIDATE_TEXTURE_DESCRIPTOR_BIT) != 0) {
        invalidate |= DkInvalidateFlags_Descriptors;
    }
    if ((bits & NVN_BARRIER_INVALIDATE_ZCULL_BIT) != 0) {
        invalidate |= DkInvalidateFlags_Zcull;
    }
    dkCmdBufBarrier(record->cmdbuf, (bits & order) != 0 ? DkBarrier_Full : DkBarrier_None,
                    invalidate);
}

static void Nvnl_nvnCommandBufferCopyTextureToBuffer(NVNcommandBuffer *cmd,
                                                     const NVNtexture *texture,
                                                     const NVNtextureView *view,
                                                     const NVNcopyRegion *region,
                                                     NVNbufferAddress buffer, int flags) {
    LayerCommandBuffer *const record = CmdOf(cmd, "nvnCommandBufferCopyTextureToBuffer");
    const LayerTexture *const source = TextureOf(texture, "nvnCommandBufferCopyTextureToBuffer");
    if (record == NULL || source == NULL) {
        return;
    }
    if (view != NULL || flags != NVN_COPY_FLAGS_NONE) {
        Complain("nvnCommandBufferCopyTextureToBuffer: %s view and copy flags 0x%x, and the "
                 "layer takes no view and no flags yet",
                 view != NULL ? "a" : "no", (unsigned)flags);
        return;
    }
    DkImageView from;
    dkImageViewDefaults(&from, &source->image);
    const DkImageRect rect = { (uint32_t)region->xoffset, (uint32_t)region->yoffset,
                               (uint32_t)region->zoffset, (uint32_t)region->width,
                               (uint32_t)region->height, (uint32_t)region->depth };
    // Rows tightly packed, NVN's default until nvnCommandBufferSetCopyRowStride, which the layer
    // does not have yet.
    const DkCopyBuf to = { buffer, 0, 0 };
    dkCmdBufCopyImageToBuffer(record->cmdbuf, &from, &rect, &to, 0);
}

// ---- windows

static void Nvnl_nvnWindowBuilderSetDefaults(NVNwindowBuilder *builder) {
    memset(builder, 0, sizeof(*builder));
}

static void Nvnl_nvnWindowBuilderSetDevice(NVNwindowBuilder *builder, NVNdevice *device) {
    LayerWindowBuilder *const record = Record(builder, "nvnWindowBuilderSetDevice");
    if (record != NULL) {
        record->device = device;
    }
}

static void Nvnl_nvnWindowBuilderSetNativeWindow(NVNwindowBuilder *builder,
                                                 NVNnativeWindow native) {
    LayerWindowBuilder *const record = Record(builder, "nvnWindowBuilderSetNativeWindow");
    if (record != NULL) {
        record->native = native;
    }
}

static void Nvnl_nvnWindowBuilderSetTextures(NVNwindowBuilder *builder, int count,
                                             NVNtexture *const *textures) {
    LayerWindowBuilder *const record = Record(builder, "nvnWindowBuilderSetTextures");
    if (record != NULL) {
        record->count = count;
        record->textures = textures;
    }
}

static NVNboolean Nvnl_nvnWindowInitialize(NVNwindow *window, const NVNwindowBuilder *builder) {
    LayerWindow *const record = Record(window, "nvnWindowInitialize");
    const LayerWindowBuilder *const from = Record(builder, "nvnWindowInitialize");
    if (record == NULL || from == NULL) {
        return 0;
    }
    const LayerDevice *const device = DeviceOf(from->device, "nvnWindowInitialize");
    if (device == NULL) {
        return 0;
    }
    if (from->count < 2 || from->count > WINDOW_TEXTURES_MAX || from->textures == NULL ||
        from->native == NULL) {
        Complain("nvnWindowInitialize: %d textures and %s native window, where NVN takes 2 to "
                 "%d and one",
                 from->count, from->native != NULL ? "a" : "no", WINDOW_TEXTURES_MAX);
        return 0;
    }
    DkImage const *images[WINDOW_TEXTURES_MAX];
    for (int i = 0; i < from->count; ++i) {
        const LayerTexture *const texture = TextureOf(from->textures[i], "nvnWindowInitialize");
        if (texture == NULL) {
            return 0;
        }
        images[i] = &texture->image;
    }
    dkSwapchainMakerDefaults(&pending.swapchain_maker, device->device, from->native, images,
                             (uint32_t)from->count);
    pending.swapchain = NULL;
    if (!Guarded(MakeSwapchain, "nvnWindowInitialize: dkSwapchainCreate")) {
        return 0;
    }
    memset(window, 0, sizeof(*window));
    record->magic = MAGIC_WINDOW;
    record->swapchain = pending.swapchain;
    return 1;
}

static void Nvnl_nvnWindowFinalize(NVNwindow *window) {
    LayerWindow *const record = WindowOf(window, "nvnWindowFinalize");
    if (record == NULL) {
        return;
    }
    dkSwapchainDestroy(record->swapchain);
    record->magic = 0;
}

// ---- the lookup

static NvnGenericProc Lookup(const char *name);

static NvnGenericProc Nvnl_nvnDeviceGetProcAddress(const NVNdevice *device, const char *name) {
    // A game's driver answers with no device too, and ScarletNX's loader calls it so.
    (void)device;
    return Lookup(name);
}

// The names the lookup answers, built from the list in nvnlayer.h, so that every name has the
// implementation of the type the list gives it: a definition above that disagrees with the list
// does not compile against these declarations.
#define NVN_LAYER_DECLARE(result, name, parameters) static result Nvnl_##name parameters;
NVN_LAYER_PROCS(NVN_LAYER_DECLARE)
#undef NVN_LAYER_DECLARE

static const struct {
    const char *name;
    NvnGenericProc proc;
} PROCS[] = {
    { "nvnDeviceGetInteger", (NvnGenericProc)NvnLayerDeviceGetInteger },
#define NVN_LAYER_ENTRY(result, name, parameters) { #name, (NvnGenericProc)Nvnl_##name },
    NVN_LAYER_PROCS(NVN_LAYER_ENTRY)
#undef NVN_LAYER_ENTRY
};

/// The function named @p name, by a search of the table and nothing else: ScarletNX runs this
/// code itself, from inside the program's first nvnBootstrapLoader call, and expects of it what
/// a driver's lookup does, a name in and an address out.
static NvnGenericProc Lookup(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(PROCS) / sizeof(PROCS[0]); ++i) {
        if (strcmp(PROCS[i].name, name) == 0) {
            return PROCS[i].proc;
        }
    }
    return NULL;
}

NvnGenericProc nvnBootstrapLoader(const char *name) {
    return Lookup(name);
}

// ---- what a program does with the lookup

bool NvnLoadProcs(NvnProcs *procs, char *missing, size_t missing_size) {
    // Through a pointer the compiler cannot see through, as a game calls its import.
    NvnGenericProc (*volatile bootstrap)(const char *) = nvnBootstrapLoader;
    memset(procs, 0, sizeof(*procs));
    procs->nvnDeviceGetProcAddress =
        (NvnGenericProc(*)(const NVNdevice *, const char *))bootstrap("nvnDeviceGetProcAddress");
    if (procs->nvnDeviceGetProcAddress == NULL) {
        snprintf(missing, missing_size, "nvnDeviceGetProcAddress");
        return false;
    }
    procs->nvnDeviceGetInteger =
        (NvnDeviceGetIntegerProc)procs->nvnDeviceGetProcAddress(NULL, "nvnDeviceGetInteger");
    if (procs->nvnDeviceGetInteger == NULL) {
        snprintf(missing, missing_size, "nvnDeviceGetInteger");
        return false;
    }
#define NVN_LAYER_LOAD(result, name, parameters)                                                  \
    procs->name = (result(*) parameters)procs->nvnDeviceGetProcAddress(NULL, #name);              \
    if (procs->name == NULL) {                                                                    \
        snprintf(missing, missing_size, "%s", #name);                                             \
        return false;                                                                             \
    }
    NVN_LAYER_PROCS(NVN_LAYER_LOAD)
#undef NVN_LAYER_LOAD
    return true;
}
