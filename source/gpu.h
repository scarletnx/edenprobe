// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
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
    /// What the colour target holds: any format GpuFormatBytes knows the size of. The readback
    /// is one tight copy at that size.
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
    /// Bytes of image memory the processor can also write, for the cases that change a texture
    /// behind the graphics unit's back. Uncached to the processor, so no flush is ever owed.
    uint32_t cpu_image_size;
    /// Bytes of memory the processor reaches through its data cache, for pitch linear textures
    /// whose writes have to be flushed before the graphics unit can see them.
    uint32_t cpu_cached_size;
    /// The widest pixel the suite will read back from an image of its own at the main target's
    /// size, when that is wider than the main target's. Zero means only the main target's.
    uint32_t readback_bytes_per_pixel;
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
    uint32_t read_size;      ///< how many bytes it holds
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
    DkMemBlock cpu_image_memory; ///< textures the processor also writes, bump allocated
    uint32_t cpu_image_used;
    DkMemBlock cpu_cached_memory; ///< the same, through the processor's data cache
    uint32_t cpu_cached_used;

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

/// The bytes one pixel of @p format occupies, for the colour formats a target can have and the
/// four byte depth format. Nought for anything else, which GpuOpenSpec refuses.
uint32_t GpuFormatBytes(DkImageFormat format);

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

/// GpuBeginPass without its barrier: the same state, bound to the main target, and nothing that
/// drops a cache. For the cases whose subject is the barrier, which they then issue themselves.
void GpuBeginList(GpuContext *gpu);

/// Points the open list at @p image, @p width by @p height, with the viewport and the scissor
/// covering all of it; NULL goes back to the main target. Issues no barrier: moving between
/// targets is ordered only by what the caller records between them.
void GpuSetTarget(GpuContext *gpu, const DkImage *image, uint32_t width, uint32_t height);

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

/// The same for any four byte image of @p width by @p height, no larger than the main target.
const uint8_t *GpuReadBackImage(GpuContext *gpu, const DkImage *image, uint32_t width,
                                uint32_t height);

/// The same for an image of any size GpuFormatBytes knows, @p bytes_per_pixel wide, packed in
/// row order. Refused with a sentence when it would not fit the readback block.
const uint8_t *GpuReadBackImageBytes(GpuContext *gpu, const DkImage *image, uint32_t width,
                                     uint32_t height, uint32_t bytes_per_pixel);

/// Copies a compiled shader module into code memory and initialises @p out from it.
///
/// @p dksh is the whole module as the compiler emitted it, header included. Returns false and
/// leaves a sentence in `failure` if there is no room or the module is not one.
bool GpuLoadShader(GpuContext *gpu, DkShader *out, const void *dksh, uint32_t size);

/// Carves @p size bytes out of the processor-visible scratch block.
///
/// Returns the address to write through, and stores the graphics unit's view of the same bytes
/// in @p gpu_addr. NULL when the block is exhausted, which is a spec that asked for too little.
///
/// Nothing is ever given back, and that is the point: reusing an address would have the processor
/// rewrite bytes the graphics unit has already pulled through a cache this file never
/// invalidates, and a case reading the previous case's vertices would look like a coverage fault.
void *GpuAllocData(GpuContext *gpu, uint32_t size, uint32_t alignment, DkGpuAddr *gpu_addr);

/// Carves image memory for a texture and initialises @p out over it.
bool GpuCreateTexture(GpuContext *gpu, DkImage *out, DkImageLayout *layout, DkImageFormat format,
                      uint32_t width, uint32_t height);

/// Carves image memory for a 2D image with the layout @p flags ask for and initialises @p out.
///
/// @p pitch_or_tile is the layout maker's union: the row pitch in bytes for a pitch linear
/// image, the tile size for DkImageFlags_CustomTileSize, zero otherwise. With @p cpu_bytes the
/// image lives in the processor-visible image block and its first byte is stored there;
/// without, in the ordinary one.
bool GpuCreateImage(GpuContext *gpu, DkImage *out, DkImageLayout *layout, DkImageFormat format,
                    uint32_t width, uint32_t height, uint32_t flags, uint32_t pitch_or_tile,
                    uint8_t **cpu_bytes);

/// A pitch linear image in the processor-cached block, rows @p width * 4 bytes apart rounded
/// to the linear stride alignment, with its first byte stored in @p cpu_bytes. A write through
/// that pointer sits in the processor's data cache until GpuFlushCachedImage.
bool GpuCreateCachedLinearImage(GpuContext *gpu, DkImage *out, DkImageLayout *layout,
                                DkImageFormat format, uint32_t width, uint32_t height,
                                uint8_t **cpu_bytes, uint32_t *stride);

/// Pushes @p size bytes the processor wrote at @p bytes, inside the processor-cached block, out
/// of its data cache: dkMemBlockFlushCpuCache, which the library asks for before the graphics
/// unit reads a processor write to cached memory.
void GpuFlushCachedImage(GpuContext *gpu, const uint8_t *bytes, uint32_t size);

/// Two images over the same bytes, one per format, laid out with the same @p flags.
///
/// False with a sentence in `failure` when the two layouts differ in size or alignment, since
/// then the second is not a view of the first and whatever it reads proves nothing.
bool GpuCreateAliasedImages(GpuContext *gpu, DkImage *first, DkImageFormat first_format,
                            DkImage *second, DkImageFormat second_format, uint32_t width,
                            uint32_t height, uint32_t flags);

/// Writes @p pixels, tightly packed, into scratch with the pitch the copy engine wants, and
/// describes them in @p out for a copy the caller records. False with a sentence when full.
bool GpuStagePixels(GpuContext *gpu, uint32_t width, uint32_t height, const void *pixels,
                    DkCopyBuf *out);

/// Uploads @p pixels, tightly packed, into the whole of @p image.
///
/// False with a sentence in `failure` when there was no room to stage the copy. Not silent,
/// because an upload that did nothing looks exactly like a sampler reading the wrong texels.
bool GpuUploadTexture(GpuContext *gpu, const DkImage *image, DkImageFormat format, uint32_t width,
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
