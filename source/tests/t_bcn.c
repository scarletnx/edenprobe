// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

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
}
