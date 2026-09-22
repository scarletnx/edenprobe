// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <deko3d.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Everything a test needs to draw into an offscreen target and read the result back.
///
/// Offscreen on purpose: nothing here is presented to a screen. A test that renders to the
/// display would need someone to look at it, and would drag in the compositor, the swapchain
/// and vertical sync, none of which is the thing under test. A small image the test reads back
/// itself is both the cheaper path and the honest one.
typedef struct {
    DkDevice device;
    DkQueue queue;
    DkMemBlock cmd_memory;   ///< where the command list is built
    DkCmdBuf cmdbuf;
    DkMemBlock image_memory; ///< the render target
    DkMemBlock read_memory;  ///< CPU-visible destination for readback
    DkImage image;
    DkImageLayout layout;
    uint32_t width;
    uint32_t height;
    bool ready;
    char failure[256];       ///< why Ready() is false, for the report
} GpuContext;

/// Brings up a device, a queue and a @p width by @p height RGBA8 render target.
///
/// Returns false and fills `failure` rather than aborting: a machine where deko3d cannot start
/// should produce a report saying exactly that, not a silent crash with no line in the log.
bool GpuOpen(GpuContext *gpu, uint32_t width, uint32_t height);
void GpuClose(GpuContext *gpu);

/// Clears the whole target to one colour and waits for the queue to drain.
void GpuClear(GpuContext *gpu, float red, float green, float blue, float alpha);

/// Copies the target into CPU-visible memory and hands back a pointer to it.
///
/// The pixels are RGBA8 in row order, `width * height * 4` bytes. Valid until the next call.
const uint8_t *GpuReadBack(GpuContext *gpu);

/// The four bytes of the pixel at @p x, @p y in the buffer GpuReadBack returned.
static inline const uint8_t *GpuPixel(const uint8_t *pixels, uint32_t width, uint32_t x,
                                      uint32_t y) {
    return pixels + ((size_t)y * width + x) * 4;
}

#ifdef __cplusplus
}
#endif
