// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "nvn/nvn.h"

#ifdef __cplusplus
extern "C" {
#endif

/// NVN, the console's graphics API, for this program: the functions a game gets by name from
/// nvnBootstrapLoader and the nvnDeviceGetProcAddress it hands back, built on deko3d.
///
/// A homebrew program carries no NVN: each game brings its own driver. This layer is one, so that
/// a case written against NVN runs on every machine. On the console and in an emulator that runs
/// the graphics unit, its functions record deko3d commands, and what the graphics unit does with
/// them is the answer. ScarletNX runs a game's NVN calls itself: it finds nvnBootstrapLoader among
/// the program's exported functions, puts its own implementation in place of every function the
/// program resolves through it, and none of the code below runs past the name lookup. The same
/// case then exercises ScarletNX's NVN against the console's graphics unit.
///
/// What that proves, and what it does not, is in docs/families/Nvn.md: the reference is the
/// graphics unit running what this layer recorded, never the official driver.

/// The type every NVN lookup answers with, cast to the function's own type by its caller.
typedef void (*NvnGenericProc)(void);

/// The NVN API version this layer answers through nvnDeviceGetInteger: 55.8, one ScarletNX has
/// measured a game's driver for, and not the 55.15 ScarletNX answers when it cannot read a
/// program's version from its code, so the nvn suite's version case tells the two apart. The
/// assembly switch in nvnlayer.c carries it.
#define NVN_LAYER_API_MAJOR 55
#define NVN_LAYER_API_MINOR 8

/// Every function the layer answers, with its NVN prototype: X(result, name, parameters). One list
/// for the layer, which declares and names each of its implementations from it, and for
/// NvnLoadProcs, which gets each through the lookup, so the two cannot disagree on a type.
/// nvnDeviceGetInteger is not in it: it is written in assembly (nvnlayer.c says why) and NvnProcs
/// carries it by hand.
#define NVN_LAYER_PROCS(X)                                                                        \
    X(NvnGenericProc, nvnDeviceGetProcAddress, (const NVNdevice *device, const char *name))       \
    X(void, nvnDeviceBuilderSetDefaults, (NVNdeviceBuilder * builder))                            \
    X(void, nvnDeviceBuilderSetFlags, (NVNdeviceBuilder * builder, int flags))                    \
    X(NVNboolean, nvnDeviceInitialize, (NVNdevice * device, const NVNdeviceBuilder *builder))    \
    X(void, nvnDeviceFinalize, (NVNdevice * device))                                              \
    X(void, nvnQueueBuilderSetDefaults, (NVNqueueBuilder * builder))                              \
    X(void, nvnQueueBuilderSetDevice, (NVNqueueBuilder * builder, NVNdevice *device))             \
    X(NVNboolean, nvnQueueInitialize, (NVNqueue * queue, const NVNqueueBuilder *builder))         \
    X(void, nvnQueueFinalize, (NVNqueue * queue))                                                 \
    X(void, nvnQueueSubmitCommands,                                                               \
      (NVNqueue * queue, int count, const NVNcommandHandle *handles))                            \
    X(void, nvnQueueFlush, (NVNqueue * queue))                                                    \
    X(void, nvnQueueFinish, (NVNqueue * queue))                                                   \
    X(NVNqueueAcquireTextureResult, nvnQueueAcquireTexture,                                       \
      (NVNqueue * queue, NVNwindow *window, int *index))                                          \
    X(void, nvnQueuePresentTexture, (NVNqueue * queue, NVNwindow *window, int index))             \
    X(void, nvnMemoryPoolBuilderSetDefaults, (NVNmemoryPoolBuilder * builder))                    \
    X(void, nvnMemoryPoolBuilderSetDevice, (NVNmemoryPoolBuilder * builder, NVNdevice *device))   \
    X(void, nvnMemoryPoolBuilderSetStorage,                                                       \
      (NVNmemoryPoolBuilder * builder, void *memory, size_t size))                                \
    X(void, nvnMemoryPoolBuilderSetFlags, (NVNmemoryPoolBuilder * builder, int flags))            \
    X(NVNboolean, nvnMemoryPoolInitialize,                                                        \
      (NVNmemoryPool * pool, const NVNmemoryPoolBuilder *builder))                                \
    X(void, nvnMemoryPoolFinalize, (NVNmemoryPool * pool))                                        \
    X(void *, nvnMemoryPoolMap, (const NVNmemoryPool *pool))                                      \
    X(NVNbufferAddress, nvnMemoryPoolGetBufferAddress, (const NVNmemoryPool *pool))               \
    X(void, nvnMemoryPoolFlushMappedRange,                                                        \
      (const NVNmemoryPool *pool, ptrdiff_t offset, size_t size))                                 \
    X(void, nvnTextureBuilderSetDefaults, (NVNtextureBuilder * builder))                          \
    X(void, nvnTextureBuilderSetDevice, (NVNtextureBuilder * builder, NVNdevice *device))         \
    X(void, nvnTextureBuilderSetFlags, (NVNtextureBuilder * builder, int flags))                  \
    X(void, nvnTextureBuilderSetTarget, (NVNtextureBuilder * builder, NVNtextureTarget target))   \
    X(void, nvnTextureBuilderSetFormat, (NVNtextureBuilder * builder, NVNformat format))          \
    X(void, nvnTextureBuilderSetSize2D, (NVNtextureBuilder * builder, int width, int height))     \
    X(void, nvnTextureBuilderSetStorage,                                                          \
      (NVNtextureBuilder * builder, NVNmemoryPool *pool, ptrdiff_t offset))                       \
    X(size_t, nvnTextureBuilderGetStorageSize, (const NVNtextureBuilder *builder))                \
    X(size_t, nvnTextureBuilderGetStorageAlignment, (const NVNtextureBuilder *builder))           \
    X(NVNboolean, nvnTextureInitialize, (NVNtexture * texture, const NVNtextureBuilder *builder)) \
    X(void, nvnTextureFinalize, (NVNtexture * texture))                                           \
    X(NVNboolean, nvnCommandBufferInitialize, (NVNcommandBuffer * cmd, NVNdevice *device))        \
    X(void, nvnCommandBufferFinalize, (NVNcommandBuffer * cmd))                                   \
    X(void, nvnCommandBufferAddCommandMemory,                                                     \
      (NVNcommandBuffer * cmd, const NVNmemoryPool *pool, ptrdiff_t offset, size_t size))         \
    X(void, nvnCommandBufferAddControlMemory,                                                     \
      (NVNcommandBuffer * cmd, void *memory, size_t size))                                        \
    X(void, nvnCommandBufferBeginRecording, (NVNcommandBuffer * cmd))                             \
    X(NVNcommandHandle, nvnCommandBufferEndRecording, (NVNcommandBuffer * cmd))                   \
    X(void, nvnCommandBufferSetRenderTargets,                                                     \
      (NVNcommandBuffer * cmd, int count, const NVNtexture *const *colors,                       \
       const NVNtextureView *const *views, const NVNtexture *depth,                               \
       const NVNtextureView *depth_view))                                                         \
    X(void, nvnCommandBufferSetViewport,                                                          \
      (NVNcommandBuffer * cmd, int x, int y, int width, int height))                              \
    X(void, nvnCommandBufferSetScissor,                                                           \
      (NVNcommandBuffer * cmd, int x, int y, int width, int height))                              \
    X(void, nvnCommandBufferClearColor,                                                           \
      (NVNcommandBuffer * cmd, int index, const float *rgba, int mask))                           \
    X(void, nvnCommandBufferBarrier, (NVNcommandBuffer * cmd, int bits))                          \
    X(void, nvnCommandBufferCopyTextureToBuffer,                                                  \
      (NVNcommandBuffer * cmd, const NVNtexture *texture, const NVNtextureView *view,             \
       const NVNcopyRegion *region, NVNbufferAddress buffer, int flags))                          \
    X(void, nvnWindowBuilderSetDefaults, (NVNwindowBuilder * builder))                            \
    X(void, nvnWindowBuilderSetDevice, (NVNwindowBuilder * builder, NVNdevice *device))           \
    X(void, nvnWindowBuilderSetNativeWindow,                                                      \
      (NVNwindowBuilder * builder, NVNnativeWindow native))                                       \
    X(void, nvnWindowBuilderSetTextures,                                                          \
      (NVNwindowBuilder * builder, int count, NVNtexture *const *textures))                       \
    X(NVNboolean, nvnWindowInitialize, (NVNwindow * window, const NVNwindowBuilder *builder))     \
    X(void, nvnWindowFinalize, (NVNwindow * window))

/// nvnDeviceGetInteger's prototype, the one function NVN_LAYER_PROCS leaves out.
typedef void (*NvnDeviceGetIntegerProc)(const NVNdevice *device, NVNdeviceInfo item, int *value);

/// Every function of the list, as a program holds them once it has looked them up.
typedef struct {
    NvnDeviceGetIntegerProc nvnDeviceGetInteger;
#define NVN_LAYER_FIELD(result, name, parameters) result(*name) parameters;
    NVN_LAYER_PROCS(NVN_LAYER_FIELD)
#undef NVN_LAYER_FIELD
} NvnProcs;

/// The program's NVN entry point, as a game's driver exports it: the function named @p name, or
/// NULL for a name the layer does not have. Exported (the Makefile's --export-dynamic-symbol),
/// because an exported nvnBootstrapLoader is what ScarletNX looks for.
NvnGenericProc nvnBootstrapLoader(const char *name);

/// Fills @p procs the way a game does: nvnDeviceGetProcAddress from nvnBootstrapLoader, then
/// every other function from it, with no device. The bootstrap is called through a pointer, as
/// a game's code calls it through its import, so ScarletNX's implementation takes the call even
/// where a direct branch would be compiled as one. False with the first name that came back
/// NULL in @p missing.
bool NvnLoadProcs(NvnProcs *procs, char *missing, size_t missing_size);

/// The last failure the layer met under deko3d, or "" when none: a sentence for the report. The
/// layer keeps NVN's signatures, which have no room for a reason; this is where it goes. Always ""
/// under ScarletNX, whose implementation replaces the layer's functions.
const char *NvnLayerError(void);

/// Clears NvnLayerError, before a step whose failure should name only its own reason.
void NvnLayerClearError(void);

#ifdef __cplusplus
}
#endif
