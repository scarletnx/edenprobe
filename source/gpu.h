// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <deko3d.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What a suite asks the graphics unit to bring up for it.
///
/// One structure rather than a widening argument list, since the suites want very different
/// things. Nothing a suite did not ask for is allocated, so a suite that is wrong about what it
/// needs fails on a null handle rather than on a silent pile of unused memory.
typedef struct {
    uint32_t width;
    uint32_t height;
    /// What the colour target holds. Every format used here is four bytes per pixel, which is
    /// what keeps the readback a single tight copy.
    DkImageFormat color_format;
    /// DkImageFormat_None for no depth target.
    DkImageFormat depth_format;
    /// Bytes of shader code memory. Zero means the suite draws nothing.
    uint32_t code_size;
    /// Bytes of processor-visible scratch: vertices, uniform buffers, descriptor sets, and the
    /// staging for anything uploaded to an image.
    uint32_t data_size;
    /// Bytes of image memory beyond the render targets, for textures.
    uint32_t image_size;
} GpuSpec;

/// Everything a test needs to draw into an offscreen target and read the result back.
///
/// Offscreen on purpose: presenting would drag in the compositor, the swapchain and vertical
/// sync, none of which is the thing under test, and would need someone to look at the result.
typedef struct {
    DkDevice device;
    DkQueue queue;
    DkMemBlock cmd_memory;   ///< where the command list is built
    DkCmdBuf cmdbuf;
    DkMemBlock image_memory; ///< the colour render target
    DkMemBlock read_memory;  ///< CPU-visible destination for readback
    DkImage image;
    DkImageLayout layout;

    DkMemBlock depth_memory; ///< the depth target, when the spec asked for one
    DkImage depth_image;
    DkImageLayout depth_layout;

    DkMemBlock code_memory;  ///< shader code, when the spec asked for any
    uint32_t code_used;
    DkMemBlock data_memory;  ///< processor-visible scratch, bump allocated
    uint32_t data_used;
    DkMemBlock extra_image_memory; ///< textures, bump allocated
    uint32_t extra_image_used;

    GpuSpec spec;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    bool ready;
    char failure[256];       ///< why Ready() is false, for the report
} GpuContext;

/// One vertex, the same shape for every case in the suite.
///
/// Wider than any single case needs, so that one layout, one vertex shader and one draw path
/// serve everything and a case cannot go wrong in its own private plumbing. The colour rides on
/// the vertex so the rasterisation cases do not depend on uniform buffer delivery.
typedef struct {
    float x, y, z;    ///< clip space, with w fixed at one by the vertex shader
    float u, v;       ///< texture coordinates, ignored by the cases that do not sample
    float r, g, b, a; ///< flat, so the fragment receives it unaltered
} GpuVertex;

/// Fills @p spec with a colour-only target and nothing else.
void GpuSpecDefaults(GpuSpec *spec, uint32_t width, uint32_t height);

/// Brings up everything @p spec asks for.
///
/// Returns false and fills `failure` rather than aborting: a machine where deko3d cannot start
/// should produce a report saying exactly that, not a silent crash with no line in the log.
bool GpuOpenSpec(GpuContext *gpu, const GpuSpec *spec);

/// Brings up a device, a queue and a @p width by @p height RGBA8 render target, and nothing
/// else. The shape the clear suite has always asked for.
bool GpuOpen(GpuContext *gpu, uint32_t width, uint32_t height);

void GpuClose(GpuContext *gpu);

/// Starts a command list with the state every case here shares, and nothing inherited.
///
/// Binds the render targets, sets the viewport and the scissor to the whole target, turns off
/// blending, the depth test and the stencil test, and opens every colour write mask. A case that
/// wants one of those changed says so after this call and before GpuSubmit.
void GpuBeginPass(GpuContext *gpu);

/// Finishes the list, submits it and waits for the queue to drain.
void GpuSubmit(GpuContext *gpu);

/// Clears the whole colour target to one colour and waits for the queue to drain.
void GpuClear(GpuContext *gpu, float red, float green, float blue, float alpha);

/// Clears only the rectangle at @p x, @p y of size @p width by @p height, and drains the queue.
///
/// Everything outside keeps whatever it held, which is what lets a case be position sensitive: a
/// target cleared all over holds the same four bytes everywhere, so a readback that transposed
/// or shuffled it would still look right. A rectangle that does not fit is not clamped, it
/// leaves the context unusable with `failure` saying so.
void GpuClearRect(GpuContext *gpu, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  float red, float green, float blue, float alpha);

/// The same, for a target whose format holds unsigned integers rather than normalised values.
void GpuClearUint(GpuContext *gpu, uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha);

/// Clears the depth target. Does nothing when the spec asked for no depth.
void GpuClearDepth(GpuContext *gpu, float depth);

/// Copies the colour target into CPU-visible memory and hands back a pointer to it.
///
/// The pixels are in row order, four bytes each, `width * height * 4` bytes. Valid until the
/// next call to either readback.
const uint8_t *GpuReadBack(GpuContext *gpu);

/// The same for the depth target, four bytes per pixel. NULL when there is no depth target.
const uint8_t *GpuReadBackDepth(GpuContext *gpu);

/// Copies a compiled shader module into code memory and initialises @p out from it.
///
/// @p dksh is the whole module as the compiler emitted it, header included. Returns false and
/// leaves a sentence in `failure` if there is no room or the module is not one.
bool GpuLoadShader(GpuContext *gpu, DkShader *out, const void *dksh, uint32_t size);

/// Carves @p size bytes out of the processor-visible scratch block.
///
/// Returns the address to write through, and stores the graphics unit's view of the same bytes
/// in @p gpu_addr. NULL when the block is exhausted, which is a spec that asked for too little.
void *GpuAllocData(GpuContext *gpu, uint32_t size, uint32_t alignment, DkGpuAddr *gpu_addr);

/// How much of the scratch block is spoken for, for GpuDataRewind to hand back later.
uint32_t GpuDataMark(const GpuContext *gpu);

/// Gives back everything allocated since @p mark. Safe only once the queue has drained, which
/// every case here has done before it reads its pixels.
void GpuDataRewind(GpuContext *gpu, uint32_t mark);

/// Carves image memory for a texture and initialises @p out over it.
bool GpuCreateTexture(GpuContext *gpu, DkImage *out, DkImageLayout *layout, DkImageFormat format,
                      uint32_t width, uint32_t height);

/// Uploads @p pixels, tightly packed, into the whole of @p image.
void GpuUploadTexture(GpuContext *gpu, const DkImage *image, DkImageFormat format, uint32_t width,
                      uint32_t height, const void *pixels);

/// Records a triangle list into the open pass. Does not submit.
///
/// The vertices are copied into scratch first, so the caller's array can be an ordinary local.
bool GpuDrawTriangles(GpuContext *gpu, const DkShader *vertex_shader,
                      const DkShader *fragment_shader, const GpuVertex *vertices,
                      uint32_t vertex_count);

/// The four bytes of the pixel at @p x, @p y in the buffer a readback returned.
static inline const uint8_t *GpuPixel(const uint8_t *pixels, uint32_t width, uint32_t x,
                                      uint32_t y) {
    return pixels + ((size_t)y * width + x) * 4;
}

#ifdef __cplusplus
}
#endif
