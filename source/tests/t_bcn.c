// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../bcnref.h"
#include "../check.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// BC4 arrays whose levels or layers do not end on a block boundary: a six-layer array with
// levels down to 1x1, the shape of every mipmapped cube map; two layers six texels tall; and a
// 4x16 texture whose second level is two texels wide and two blocks tall. Every block is one
// value (both endpoints equal, every index nought), unique to its layer, level and position,
// written by the processor where source/bcnref.c says the library laid it out, and every texel
// of every level and layer is fetched onto its own pixel. BC4 decodes to exact bytes, so the
// tier is exact. A host with BCn textures samples them as they are; one without has to decode
// them on upload, level by level with all the layers of a level in one copy.
//
// Then one block of every BCn format, built a field at a time from the Khronos Data Format
// Specification's S3TC, RGTC and BPTC chapters, fetched texel by texel onto a target of 32-bit
// floats: BC1's four and three colour modes, BC2's four bit alphas, BC3's eight and six alpha
// modes, BC4 and BC5 unsigned and signed, BC6H's mode 11 unsigned and signed and a reserved mode,
// and BC7's modes 6 and 5 and a block with no mode. The interpolants are NVIDIA's, which differ
// from the specification's thirds and fifths: BC1's 2/3 point of 31 and 0 is 170 and 85, its
// midpoint 127; BC3's six-value alpha at index 2 is 48, where a fifth of 255 is 51; BC4's index 2
// between 255 and 0 is 56355 of 65535, where six sevenths is 56172. ScarletNX's BCn decoder test
// holds its decoder to these. Each listed texel's channels, times 255 for the eight bit formats,
// 65535 or 32767 for BC4 and BC5, rounded, must be the decoded integer; BC6H's halves are exact
// floats. A host that samples the blocks natively gives its own GPU's interpolants.

static const char *const SUITE = "bcn";

#define TARGET_WIDTH 16u
#define TARGET_HEIGHT 24u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

typedef struct {
    const char *name;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t levels;
    uint32_t first_value;
} ArrayCase;

// The value of block (bx, by) of @p level in @p layer: consecutive from first_value, level by
// level, layer by layer inside a level, rows then columns.
static uint8_t BlockValue(const ArrayCase *c, const BcnArray *array, uint32_t layer,
                          uint32_t level, uint32_t bx, uint32_t by) {
    uint32_t value = c->first_value;
    for (uint32_t l = 0; l < level; ++l) {
        value += BcnLevelBlocksX(array, l) * BcnLevelBlocksY(array, l) * c->layers;
    }
    const uint32_t per_layer = BcnLevelBlocksX(array, level) * BcnLevelBlocksY(array, level);
    return (uint8_t)(value + layer * per_layer + by * BcnLevelBlocksX(array, level) + bx);
}

static void ArrayCaseRun(GpuContext *gpu, const DkShader *vertex_shader,
                         const DkShader *fragment_shader, const ArrayCase *c) {
    if (!ReportStart(SUITE, c->name)) {
        return;
    }
    BcnArray array;
    BcnArrayLayout(&array, c->width, c->height, c->layers, c->levels, 8);
    // One cell per level and layer, level 0's size rounded up to a power of two so the shader
    // finds its cell with shifts.
    uint32_t shift_w = 0;
    uint32_t shift_h = 0;
    while ((1u << shift_w) < c->width) {
        ++shift_w;
    }
    while ((1u << shift_h) < c->height) {
        ++shift_h;
    }
    const uint32_t cell_w = 1u << shift_w;
    const uint32_t cell_h = 1u << shift_h;
    DkImage image;
    DkImageLayout layout;
    uint8_t *memory = NULL;
    DkGpuAddr image_set = 0;
    DkGpuAddr sampler_set = 0;
    DkGpuAddr grid_address = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    int32_t *const grid =
        (int32_t *)GpuAllocData(gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &grid_address);
    if (images == NULL || samplers == NULL || grid == NULL ||
        !GpuCreateArrayImage(gpu, &image, &layout, DkImageFormat_R_BC4_Unorm, c->width,
                             c->height, c->layers, c->levels, &memory)) {
        ReportCase(SUITE, c->name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the texture");
        return;
    }
    const uint64_t size = dkImageLayoutGetSize(&layout);
    if (size < (uint64_t)array.layer_size * c->layers) {
        ReportCase(SUITE, c->name, CHECK_NONE, false,
                   "the library laid the array out smaller than the layers need");
        return;
    }
    memset(memory, 0, (size_t)size);
    for (uint32_t level = 0; level < c->levels; ++level) {
        for (uint32_t layer = 0; layer < c->layers; ++layer) {
            for (uint32_t by = 0; by < BcnLevelBlocksY(&array, level); ++by) {
                for (uint32_t bx = 0; bx < BcnLevelBlocksX(&array, level); ++bx) {
                    BcnSolidBc4(memory + BcnBlockOffset(&array, layer, level, bx, by),
                                BlockValue(c, &array, layer, level, bx, by));
                }
            }
        }
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    view.type = DkImageType_2DArray;
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_Nearest;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    memset(grid, 0, UNIFORM_SIZE);
    grid[0] = (int32_t)shift_w;
    grid[1] = (int32_t)shift_h;
    grid[2] = (int32_t)c->levels;
    grid[3] = (int32_t)c->layers;

    static const GpuVertex quad[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(gpu);
    dkCmdBufBindImageDescriptorSet(gpu->cmdbuf, image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(gpu->cmdbuf, sampler_set, 1);
    dkCmdBufBindTexture(gpu->cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, grid_address, UNIFORM_SIZE);
    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, quad, 6);
    GpuSubmit(gpu);
    const uint8_t *const pixels = drawn && gpu->ready ? GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, c->name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t judged = 0;
    uint32_t first_level = 0;
    uint32_t first_layer = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t seen[4] = { 0, 0, 0, 0 };
    uint8_t first_want = 0;
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            const uint32_t level = x / cell_w;
            const uint32_t layer = y / cell_h;
            if (level >= c->levels || layer >= c->layers) {
                PictureSet(picture, x, y, pixel, PICTURE_UNJUDGED);
                continue;
            }
            const uint32_t level_w = (c->width >> level) != 0u ? c->width >> level : 1u;
            const uint32_t level_h = (c->height >> level) != 0u ? c->height >> level : 1u;
            uint32_t tx = x % cell_w;
            uint32_t ty = y % cell_h;
            tx = tx < level_w ? tx : level_w - 1u;
            ty = ty < level_h ? ty : level_h - 1u;
            const uint8_t value = BlockValue(c, &array, layer, level, tx / 4u, ty / 4u);
            const uint8_t want[4] = { value, 0, 0, 255 };
            const bool right = memcmp(pixel, want, 4) == 0;
            PictureSet(picture, x, y, want, right ? PICTURE_MATCHED : PICTURE_WRONG);
            ++judged;
            if (!right) {
                if (wrong == 0) {
                    first_level = level;
                    first_layer = layer;
                    first_x = tx;
                    first_y = ty;
                    first_want = value;
                    memcpy(seen, pixel, 4);
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, c->name, CHECK_EXACT, wrong == 0,
               "%u of %u pixels wrong; level %u layer %u texel (%u, %u) is %02x%02x%02x%02x, "
               "its block's %02x0000ff",
               (unsigned)wrong, (unsigned)judged, (unsigned)first_level, (unsigned)first_layer,
               (unsigned)first_x, (unsigned)first_y, seen[0], seen[1], seen[2], seen[3],
               first_want);
}

// ---- one block of each format, decoded

#define DECODE_SIDE 4u

/// A block written a field at a time from bit 0 up, the way BPTC lays its blocks out.
typedef struct {
    uint8_t bytes[16];
    uint32_t at;
} BlockBits;

static void Put(BlockBits *b, uint32_t value, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i, ++b->at) {
        if ((value >> i & 1u) != 0u) {
            b->bytes[b->at / 8u] |= (uint8_t)(1u << b->at % 8u);
        }
    }
}

/// Eight little endian bytes of @p value at @p out.
static void PutWord64(uint8_t *out, uint64_t value) {
    for (uint32_t i = 0; i < 8u; ++i) {
        out[i] = (uint8_t)(value >> (8u * i));
    }
}

/// What one texel must decode to: its place, and its four channels as integers on the scale the
/// case gives (or, on the half scale, as halves).
typedef struct {
    uint8_t x, y;
    int32_t channel[4];
} DecodedTexel;

/// How a case reads the floats the sampler hands back.
typedef enum {
    SCALE_BYTE,  ///< times 255, rounded
    SCALE_SHORT, ///< times 65535
    SCALE_SNORM, ///< times 32767
    SCALE_HALF,  ///< the channel is a half: its bits as the float it is
} DecodeScale;

static float HalfValue(uint32_t half) {
    const uint32_t exponent = half >> 10 & 0x1Fu, mantissa = half & 0x3FFu;
    float value = exponent == 0u ? (float)mantissa / 16777216.0f
                                 : (1.0f + (float)mantissa / 1024.0f) *
                                       (float)(1u << exponent) / 32768.0f;
    return (half & 0x8000u) != 0u ? -value : value;
}

/// The block @p block of @p format as a 4x4 texture, fetched onto the 4x4 target of floats; every
/// listed texel judged.
static void DecodeCase(GpuContext *gpu, const DkShader *vertex_shader, const DkShader *fetch,
                       const char *name, DkImageFormat format, const uint8_t *block,
                       uint32_t block_bytes, DecodeScale scale, const DecodedTexel *texels,
                       uint32_t count) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    DkImage image;
    DkImageLayout layout;
    uint8_t *memory = NULL;
    DkGpuAddr image_set = 0, sampler_set = 0, size_address = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    int32_t *const size = (int32_t *)GpuAllocData(gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT,
                                                  &size_address);
    if (images == NULL || samplers == NULL || size == NULL ||
        !GpuCreateImage(gpu, &image, &layout, format, DECODE_SIDE, DECODE_SIDE, 0, 0, &memory)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the texture");
        return;
    }
    // One block is the whole texture, at its start whatever the layout.
    memcpy(memory, block, block_bytes);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_None;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    memset(size, 0, UNIFORM_SIZE);
    size[0] = (int32_t)DECODE_SIDE;
    size[1] = (int32_t)DECODE_SIDE;
    static const GpuVertex quad[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    };
    GpuClear(gpu, 0.0f, 0.0f, 0.0f, 0.0f);
    GpuBeginPass(gpu);
    dkCmdBufBindImageDescriptorSet(gpu->cmdbuf, image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(gpu->cmdbuf, sampler_set, 1);
    dkCmdBufBindTexture(gpu->cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, size_address, UNIFORM_SIZE);
    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fetch, quad, 6);
    GpuSubmit(gpu);
    const float *const pixels = drawn && gpu->ready ? (const float *)GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }
    static const float SCALES[] = { 255.0f, 65535.0f, 32767.0f, 1.0f };
    uint32_t wrong = 0;
    char first[200] = "";
    for (uint32_t i = 0; i < count; ++i) {
        const float *const got = pixels + ((uint32_t)texels[i].y * DECODE_SIDE + texels[i].x) * 4u;
        bool right = true;
        for (uint32_t c = 0; c < 4u; ++c) {
            if (scale == SCALE_HALF) {
                right = right && got[c] == HalfValue((uint32_t)texels[i].channel[c]);
            } else {
                const float scaled = got[c] * SCALES[scale];
                const int32_t near = (int32_t)(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
                right = right && near == texels[i].channel[c];
            }
        }
        if (!right && wrong++ == 0u) {
            snprintf(first, sizeof(first),
                     "(%u, %u) is %.9g %.9g %.9g %.9g, expected %d %d %d %d%s", texels[i].x,
                     texels[i].y, (double)got[0], (double)got[1], (double)got[2], (double)got[3],
                     (int)texels[i].channel[0], (int)texels[i].channel[1],
                     (int)texels[i].channel[2], (int)texels[i].channel[3],
                     scale == SCALE_HALF ? " as halves"
                     : scale == SCALE_BYTE ? " of 255"
                     : scale == SCALE_SHORT ? " of 65535"
                                            : " of 32767");
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0, "%u of %u texels wrong; the first, %s",
               (unsigned)wrong, (unsigned)count, first);
}

/// A BC1 colour block: the two RGB565 endpoints and the sixteen two bit indices.
static void Bc1(uint8_t *out, uint32_t c0, uint32_t c1, uint32_t indices) {
    PutWord64(out, (uint64_t)indices << 32 | (uint64_t)c1 << 16 | c0);
}

static void RunBcnDecode(void) {
    static const char *const OPEN = "graphics device opens with a target of floats";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    GpuSpec spec;
    GpuSpecDefaults(&spec, DECODE_SIDE, DECODE_SIDE);
    spec.color_format = DkImageFormat_RGBA32_Float;
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = 16u * 128u * 1024u;
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, opened, "%s", gpu.failure);
    if (!opened) {
        return;
    }
    static const char *const LOAD = "the fetching shader loads";
    if (!ReportStart(SUITE, LOAD)) {
        GpuClose(&gpu);
        return;
    }
    DkShader vertex_shader, fetch;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fetch, ShaderData(SHADER_FETCH_FRAG),
                                      ShaderSize(SHADER_FETCH_FRAG));
    CHECK_TRUE(SUITE, LOAD, CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    uint8_t block[16];

    // BC1: red 0xf800 above blue 0x001f is four colours; the first four texels take indices 0
    // to 3 (0xe4). Swapped, the first endpoint the smaller, three colours and transparent black.
    memset(block, 0, sizeof(block));
    Bc1(block, 0xF800u, 0x001Fu, 0xE4u);
    DecodeCase(&gpu, &vertex_shader, &fetch,
               "a BC1 block of four colours decodes 255 0 0, 0 0 255, 170 0 85 and 85 0 170",
               DkImageFormat_RGBA_BC1, block, 8, SCALE_BYTE,
               (const DecodedTexel[]){ { 0, 0, { 255, 0, 0, 255 } },
                                       { 1, 0, { 0, 0, 255, 255 } },
                                       { 2, 0, { 170, 0, 85, 255 } },
                                       { 3, 0, { 85, 0, 170, 255 } } },
               4);
    Bc1(block, 0x001Fu, 0xF800u, 0xE4u);
    DecodeCase(&gpu, &vertex_shader, &fetch,
               "a BC1 block of three colours decodes its midpoint as 127 and index 3 transparent",
               DkImageFormat_RGBA_BC1, block, 8, SCALE_BYTE,
               (const DecodedTexel[]){ { 2, 0, { 127, 0, 127, 255 } },
                                       { 3, 0, { 0, 0, 0, 0 } } },
               2);
    // BC2: alpha nibbles 15, 8, 0 for the first three texels, then the colour block above taken
    // as four colours, as BC2 always takes it.
    memset(block, 0, sizeof(block));
    PutWord64(block, 0x08Full);
    Bc1(block + 8, 0x001Fu, 0xF800u, 0xE4u);
    DecodeCase(&gpu, &vertex_shader, &fetch,
               "a BC2 block decodes alphas 15 and 8 as 255 and 136 and its colours as four",
               DkImageFormat_RGBA_BC2, block, 16, SCALE_BYTE,
               (const DecodedTexel[]){ { 0, 0, { 0, 0, 255, 255 } },
                                       { 1, 0, { 255, 0, 0, 136 } },
                                       { 3, 0, { 170, 0, 85, 0 } } },
               3);
    // BC3: alpha 255 above 0 is eight values, the first two texels at indices 2 and 7; 0 below
    // 255 is six and the two ends, the first three texels at indices 2, 6 and 7. Colour all red.
    memset(block, 0, sizeof(block));
    PutWord64(block, 7ull << 19 | 2ull << 16 | 0x00FFu);
    Bc1(block + 8, 0xF800u, 0x0000u, 0);
    DecodeCase(&gpu, &vertex_shader, &fetch, "a BC3 block of eight alphas decodes 219 and 36",
               DkImageFormat_RGBA_BC3, block, 16, SCALE_BYTE,
               (const DecodedTexel[]){ { 0, 0, { 255, 0, 0, 219 } },
                                       { 1, 0, { 255, 0, 0, 36 } } },
               2);
    PutWord64(block, 7ull << 22 | 6ull << 19 | 2ull << 16 | 0xFF00u);
    DecodeCase(&gpu, &vertex_shader, &fetch, "a BC3 block of six alphas decodes 48, 0 and 255",
               DkImageFormat_RGBA_BC3, block, 16, SCALE_BYTE,
               (const DecodedTexel[]){ { 0, 0, { 255, 0, 0, 48 } },
                                       { 1, 0, { 255, 0, 0, 0 } },
                                       { 2, 0, { 255, 0, 0, 255 } } },
               3);
    // BC4: 255 above 0, the first texel at index 2, the second at 0. Signed: 127 above -128,
    // which reads as -127, at indices 2 and 1.
    memset(block, 0, sizeof(block));
    PutWord64(block, 2ull << 16 | 0x00FFu);
    DecodeCase(&gpu, &vertex_shader, &fetch,
               "a BC4 block decodes index 2 between 255 and 0 as 56355 of 65535",
               DkImageFormat_R_BC4_Unorm, block, 8, SCALE_SHORT,
               (const DecodedTexel[]){ { 0, 0, { 56355, 0, 0, 65535 } },
                                       { 1, 0, { 65535, 0, 0, 65535 } } },
               2);
    PutWord64(block, 1ull << 19 | 2ull << 16 | 0x807Fu);
    DecodeCase(&gpu, &vertex_shader, &fetch,
               "a signed BC4 block decodes index 2 as 23623 of 32767 and -128 as -1",
               DkImageFormat_R_BC4_Snorm, block, 8, SCALE_SNORM,
               (const DecodedTexel[]){ { 0, 0, { 23623, 0, 0, 32767 } },
                                       { 1, 0, { -32767, 0, 0, 32767 } } },
               2);
    // BC5: red 255 everywhere (index 0 of 255 above 0), green 0 (index 0 of 0 below 255).
    memset(block, 0, sizeof(block));
    PutWord64(block, 0x00FFu);
    PutWord64(block + 8, 0xFF00u);
    DecodeCase(&gpu, &vertex_shader, &fetch,
               "a BC5 block decodes red and green each from its own half",
               DkImageFormat_RG_BC5_Unorm, block, 16, SCALE_SHORT,
               (const DecodedTexel[]){ { 2, 2, { 65535, 0, 0, 65535 } } }, 1);
    // BC6H mode 11 (mode bits 00011): two 10 bit endpoints with no deltas, 0 and 1023 (the
    // largest, which unquantizes to 0xffff); the anchor texel at index 0, the next at 15 and 8.
    // Index 15 is the second endpoint, 0xffff times 31 over 64: 0x7bff, the largest finite half;
    // index 8 weighs it 34 of 64: 0x41df.
    {
        BlockBits b;
        memset(&b, 0, sizeof(b));
        Put(&b, 3, 5);
        Put(&b, 0, 30);
        for (uint32_t c = 0; c < 3u; ++c) {
            Put(&b, 1023, 10);
        }
        Put(&b, 0, 3);
        Put(&b, 15, 4);
        Put(&b, 8, 4);
        DecodeCase(&gpu, &vertex_shader, &fetch,
                   "a BC6H mode 11 block decodes 0, 65504 and 2.935546875 as halves",
                   DkImageFormat_RGBA_BC6H_UF16_Float, b.bytes, 16, SCALE_HALF,
                   (const DecodedTexel[]){ { 0, 0, { 0, 0, 0, 0x3C00 } },
                                           { 1, 0, { 0x7BFF, 0x7BFF, 0x7BFF, 0x3C00 } },
                                           { 2, 0, { 0x41DF, 0x41DF, 0x41DF, 0x3C00 } } },
                   3);
        // Signed: -512 and 511, each past the largest magnitude, finish as -65504 and 65504.
        memset(&b, 0, sizeof(b));
        Put(&b, 3, 5);
        for (uint32_t c = 0; c < 3u; ++c) {
            Put(&b, 0x200, 10);
        }
        for (uint32_t c = 0; c < 3u; ++c) {
            Put(&b, 0x1FF, 10);
        }
        Put(&b, 0, 3);
        Put(&b, 15, 4);
        DecodeCase(&gpu, &vertex_shader, &fetch, "a signed BC6H block decodes -65504 and 65504",
                   DkImageFormat_RGBA_BC6H_SF16_Float, b.bytes, 16, SCALE_HALF,
                   (const DecodedTexel[]){ { 0, 0, { 0xFBFF, 0xFBFF, 0xFBFF, 0x3C00 } },
                                           { 1, 0, { 0x7BFF, 0x7BFF, 0x7BFF, 0x3C00 } } },
                   2);
    }
    // A reserved BC6H mode (mode bits 10011) decodes opaque black.
    memset(block, 0xFF, sizeof(block));
    block[0] = 19;
    DecodeCase(&gpu, &vertex_shader, &fetch, "a BC6H block of a reserved mode decodes black",
               DkImageFormat_RGBA_BC6H_UF16_Float, block, 16, SCALE_HALF,
               (const DecodedTexel[]){ { 0, 0, { 0, 0, 0, 0x3C00 } } }, 1);
    // BC7 mode 6: seven bit endpoints and a p-bit each, red 127 and 0, green 0 and 127, blue 0,
    // alpha 127, both p-bits 1: red 255 and 1, green 1 and 255, blue 1, alpha 255. Index 8 weighs
    // the second endpoint 34 of 64: red 120, green 136.
    {
        BlockBits b;
        memset(&b, 0, sizeof(b));
        Put(&b, 1u << 6, 7);
        Put(&b, 127, 7);
        Put(&b, 0, 7);
        Put(&b, 0, 7);
        Put(&b, 127, 7);
        Put(&b, 0, 14);
        Put(&b, 127, 7);
        Put(&b, 127, 7);
        Put(&b, 1, 1);
        Put(&b, 1, 1);
        Put(&b, 0, 3);
        Put(&b, 15, 4);
        Put(&b, 8, 4);
        DecodeCase(&gpu, &vertex_shader, &fetch,
                   "a BC7 mode 6 block decodes 255 1 1, 1 255 1 and 120 136 1",
                   DkImageFormat_RGBA_BC7_Unorm, b.bytes, 16, SCALE_BYTE,
                   (const DecodedTexel[]){ { 0, 0, { 255, 1, 1, 255 } },
                                           { 1, 0, { 1, 255, 1, 255 } },
                                           { 2, 0, { 120, 136, 1, 255 } } },
                   3);
        // Mode 5 with rotation 1: red's seven bit endpoints 0 and 127, green and blue 0, alpha's
        // eight bit ones both 200, every index 0; the rotation swaps alpha and red.
        memset(&b, 0, sizeof(b));
        Put(&b, 1u << 5, 6);
        Put(&b, 1, 2);
        Put(&b, 0, 7);
        Put(&b, 127, 7);
        Put(&b, 0, 28);
        Put(&b, 200, 8);
        Put(&b, 200, 8);
        DecodeCase(&gpu, &vertex_shader, &fetch,
                   "a BC7 mode 5 block with rotation 1 swaps alpha and red",
                   DkImageFormat_RGBA_BC7_Unorm, b.bytes, 16, SCALE_BYTE,
                   (const DecodedTexel[]){ { 0, 0, { 200, 0, 0, 0 } } }, 1);
    }
    // No mode bit in the first byte: transparent black, whatever the rest holds.
    memset(block, 0, sizeof(block));
    block[5] = 0xFF;
    DecodeCase(&gpu, &vertex_shader, &fetch, "a BC7 block with no mode decodes transparent black",
               DkImageFormat_RGBA_BC7_Unorm, block, 16, SCALE_BYTE,
               (const DecodedTexel[]){ { 1, 1, { 0, 0, 0, 0 } } }, 1);
    GpuClose(&gpu);
}

void RunBcn(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = 3u * 128u * 1024u;

    if (!ReportStart(SUITE, "graphics device opens with room for the arrays")) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the arrays", CHECK_NONE, opened, "%s",
               gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the array fetching shader loads")) {
        GpuClose(&gpu);
        return;
    }
    DkShader vertex_shader;
    DkShader fragment_shader;
    const bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &fragment_shader, ShaderData(SHADER_FETCH_ARRAY_FRAG),
                                      ShaderSize(SHADER_FETCH_ARRAY_FRAG));
    CHECK_TRUE(SUITE, "the array fetching shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    static const ArrayCase CASES[] = {
        { "every layer of a six-layer BC4 array keeps its own 2x2 and 1x1 levels", 4, 4, 6, 3,
          16 },
        { "both layers of a BC4 array six texels tall keep their own blocks", 8, 6, 2, 1, 64 },
        { "the 2x8 second level of a 4x16 BC4 texture reads both of its blocks", 4, 16, 1, 2,
          100 },
    };
    for (uint32_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); ++i) {
        ArrayCaseRun(&gpu, &vertex_shader, &fragment_shader, &CASES[i]);
    }
    GpuClose(&gpu);
    RunBcnDecode();
}
