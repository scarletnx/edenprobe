// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coherref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Block linear textures the processor writes texel by texel, at the addresses the block linear
// rule gives (source/coherref.c, CohBlockLinearOffset), and samples back one texel per pixel.
// What varies is what an emulator's unswizzle has to get right: the bytes per texel (1, 2, 4, 8
// and 16), the block height (1 to 32 gobs), and a width that ends inside a gob. The texels are
// unsigned integers, so a pixel is the texel's exact bits folded into one word
// (swizzle.frag.glsl), and each case compares every one.
//
// The last case is for time: a 1024 by 1024 texture, 4 MiB, the processor writes and the next
// draw samples, which is the whole upload an emulator makes on first use.

static const char *const SUITE = "swizzle";

#define TARGET_WIDTH 104u
#define TARGET_HEIGHT 100u
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT
#define MAX_TEXELS (1024u * 1024u)

typedef struct {
    DkImageFormat format;
    uint32_t bytes;    // per texel
    uint32_t channels; // 1, 2 or 4
    uint32_t channel_bytes;
} TexelFormat;

static const TexelFormat R8 = { DkImageFormat_R8_Uint, 1, 1, 1 };
static const TexelFormat RG8 = { DkImageFormat_RG8_Uint, 2, 2, 1 };
static const TexelFormat RGBA8 = { DkImageFormat_RGBA8_Uint, 4, 4, 1 };
static const TexelFormat RGBA16 = { DkImageFormat_RGBA16_Uint, 8, 4, 2 };
static const TexelFormat RGBA32 = { DkImageFormat_RGBA32_Uint, 16, 4, 4 };

static uint32_t Rotate(uint32_t v, uint32_t by) {
    return by == 0u ? v : (v << by) | (v >> (32u - by));
}

/// The four channels texelFetch hands back: missing colour channels are 0 and a missing alpha
/// is 1, as for every unsigned integer format.
static uint32_t Fold(const TexelFormat *f, const uint8_t *texel) {
    uint32_t c[4] = { 0, 0, 0, 1 };
    for (uint32_t i = 0; i < f->channels; ++i) {
        uint32_t v = 0;
        for (uint32_t b = 0; b < f->channel_bytes; ++b) {
            v |= (uint32_t)texel[i * f->channel_bytes + b] << (8u * b);
        }
        c[i] = v;
    }
    return c[0] ^ Rotate(c[1], 8) ^ Rotate(c[2], 16) ^ Rotate(c[3], 24);
}

static uint32_t tile_gobs[6] = { 1, 2, 4, 8, 16, 32 };

typedef struct {
    GpuContext gpu;
    DkShader vertex_shader;
    DkShader fragment_shader;
    uint32_t seed;
} SwizzleContext;

static uint32_t NextRandom(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void Case(SwizzleContext *s, const char *name, const TexelFormat *f, uint32_t width,
                 uint32_t height, uint32_t tile) {
    static uint8_t texels[MAX_TEXELS * 4u];
    GpuContext *const gpu = &s->gpu;
    DkImage image;
    DkImageLayout layout;
    uint8_t *memory = NULL;
    DkGpuAddr image_set = 0;
    DkGpuAddr sampler_set = 0;
    DkGpuAddr size_address = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    uint32_t *const size_words =
        (uint32_t *)GpuAllocData(gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &size_address);
    if (images == NULL || samplers == NULL || size_words == NULL ||
        (uint64_t)width * height * f->bytes > sizeof(texels) ||
        !GpuCreateImage(gpu, &image, &layout, f->format, width, height,
                        DkImageFlags_CustomTileSize, tile, &memory)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "no room for the texture");
        return;
    }
    const uint32_t gobs = tile_gobs[tile];
    const uint32_t row_bytes = width * f->bytes;
    const uint32_t gobs_across = (row_bytes + 63u) / 64u;
    const uint32_t block_rows = 8u * gobs;
    const uint64_t expected_size =
        (uint64_t)gobs_across * ((height + block_rows - 1u) / block_rows) * gobs * 512u;
    if (dkImageLayoutGetSize(&layout) != expected_size) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "the library laid it out in %llu bytes, the block linear rule says %llu",
                 (unsigned long long)dkImageLayoutGetSize(&layout),
                 (unsigned long long)expected_size);
        ReportCase(SUITE, name, CHECK_NONE, false, detail);
        return;
    }
    uint32_t state = s->seed ^ (width * 2654435761u) ^ (height << 7) ^ f->bytes ^ (gobs << 20);
    if (state == 0u) {
        state = 1u;
    }
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t *const texel = texels + ((size_t)y * width + x) * f->bytes;
            for (uint32_t b = 0; b < f->bytes; ++b) {
                texel[b] = (uint8_t)NextRandom(&state);
            }
            for (uint32_t b = 0; b < f->bytes; ++b) {
                memory[CohBlockLinearOffset(x * f->bytes + b, y, row_bytes, gobs)] = texel[b];
            }
        }
    }
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_None;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    memset(size_words, 0, UNIFORM_SIZE);
    size_words[0] = width;
    size_words[1] = height;

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
    const bool drawn =
        GpuDrawTriangles(gpu, &s->vertex_shader, &s->fragment_shader, quad, 6);
    GpuSubmit(gpu);
    const uint8_t *const pixels = drawn && gpu->ready ? GpuReadBack(gpu) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }
    const uint32_t judged_width = width < TARGET_WIDTH ? width : TARGET_WIDTH;
    const uint32_t judged_height = height < TARGET_HEIGHT ? height : TARGET_HEIGHT;
    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_COLOUR, pixels);
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint32_t got_word = 0;
    uint32_t want_word = 0;
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint8_t *const pixel = GpuPixel(pixels, TARGET_WIDTH, x, y);
            if (x >= judged_width || y >= judged_height) {
                PictureSet(picture, x, y, pixel, PICTURE_UNJUDGED);
                continue;
            }
            const uint32_t want =
                Fold(f, texels + ((size_t)y * width + x) * f->bytes);
            const uint8_t expect[4] = { (uint8_t)want, (uint8_t)(want >> 8),
                                        (uint8_t)(want >> 16), (uint8_t)(want >> 24) };
            const bool right = memcmp(pixel, expect, 4) == 0;
            PictureSet(picture, x, y, expect, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (!right) {
                if (wrong == 0) {
                    first_x = x;
                    first_y = y;
                    got_word = (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 |
                               (uint32_t)pixel[2] << 16 | (uint32_t)pixel[3] << 24;
                    want_word = want;
                }
                ++wrong;
            }
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u texels wrong; texel (%u,%u) folds to 0x%08x, written 0x%08x",
               (unsigned)wrong, (unsigned)(judged_width * judged_height), (unsigned)first_x,
               (unsigned)first_y, (unsigned)got_word, (unsigned)want_word);
}

void RunSwizzle(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    spec.code_size = 32u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.cpu_image_size = 12u * 1024u * 1024u;

    SwizzleContext s;
    s.seed = 0x5EEDF00Du;
    const bool opened = GpuOpenSpec(&s.gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the textures", CHECK_NONE, opened,
               "%s", s.gpu.failure);
    if (!opened) {
        return;
    }
    const bool loaded =
        GpuLoadShader(&s.gpu, &s.vertex_shader, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&s.gpu, &s.fragment_shader, ShaderData(SHADER_SWIZZLE_FRAG),
                      ShaderSize(SHADER_SWIZZLE_FRAG));
    CHECK_TRUE(SUITE, "the folding shader loads", CHECK_NONE, loaded, "%s", s.gpu.failure);
    if (!loaded) {
        GpuClose(&s.gpu);
        return;
    }
    Case(&s, "a block linear R8 texture the processor wrote samples as written", &R8, 100, 100,
         DkTileSize_SixteenGobs);
    Case(&s, "a block linear RG8 texture the processor wrote samples as written", &RG8, 100,
         100, DkTileSize_SixteenGobs);
    Case(&s, "a block linear RGBA8 texture the processor wrote samples as written", &RGBA8, 100,
         100, DkTileSize_SixteenGobs);
    Case(&s, "a block linear RGBA16 texture the processor wrote samples as written", &RGBA16,
         100, 100, DkTileSize_SixteenGobs);
    Case(&s, "a block linear RGBA32 texture the processor wrote samples as written", &RGBA32,
         100, 100, DkTileSize_SixteenGobs);
    Case(&s, "one gob tall blocks sample as written", &RGBA8, 100, 100, DkTileSize_OneGob);
    Case(&s, "two gob tall blocks sample as written", &RGBA8, 100, 100, DkTileSize_TwoGobs);
    Case(&s, "four gob tall blocks sample as written", &RGBA8, 100, 100, DkTileSize_FourGobs);
    Case(&s, "eight gob tall blocks sample as written", &RGBA8, 100, 100, DkTileSize_EightGobs);
    // Tall enough to keep 32 gob blocks: a block linear layout halves the block height while
    // half of it still covers the image, so a 100 row image never gets more than 16.
    Case(&s, "thirty two gob tall blocks sample as written", &RGBA8, 100, 260,
         DkTileSize_ThirtyTwoGobs);
    Case(&s, "a 37 by 23 texture that ends inside a gob samples as written", &RGBA8, 37, 23,
         DkTileSize_FourGobs);
    Case(&s, "a 4 MiB texture the processor wrote uploads and samples as written", &RGBA8, 1024,
         1024, DkTileSize_SixteenGobs);
    GpuClose(&s.gpu);
}
