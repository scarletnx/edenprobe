// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../astcimages.h"
#include "../astcref.h"
#include "../check.h"
#include "../coherref.h"
#include "../formatref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Whole ASTC textures, as a game's are. The astc suite proves each feature of the format on one
// block; here a picture encoded by ARM's astcenc (source/astcimages.h: gradients, hard edges,
// noise and alpha) is sampled texel by texel at four footprints, and in two of them as sRGB. An
// emulator whose host has no ASTC decodes it itself, and one that then compresses it again to
// save memory (BC7, say) changes texels: each case says how many, how far, and the picture's
// PSNR over each region, so the cost of such a path is a number and not a look.
//
// What each texel may be: the linear formats decode by default to FP16, which an eight bit
// target rounds (AstcUnorm8Fp16), and a texture unit may also decode to UNORM16 or to eight bits
// (AstcUnorm8Answers), so any of those three bytes is right and nothing else. The sRGB formats
// decode to an eight bit sRGB code, which the texture unit turns into linear: red, green and
// blue may land on either code beside that linear value times 255, alpha is the code itself.
//
// Then the cost: two 2048x2048 textures of the same blocks, made and fetched once at every
// sixteenth texel (what an upload costs, decoding included), and the 4x4 one fetched 256 times,
// each texel once (what sampling it costs).

static const char *const SUITE = "astcimage";

#define TARGET ASTC_IMAGE_SIZE
#define BIG 2048u
#define BIG_STEP (BIG / TARGET)
#define SAMPLING_DRAWS (BIG_STEP * BIG_STEP)
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT
#define CHANNELS (TARGET * TARGET * 4u)

/// fetch_step.frag's uniform block.
typedef struct {
    int32_t size[4];
    int32_t offset[4];
} FetchStep;

/// What each channel of the picture may be read back as: three bytes, the first the one a
/// wrong texel is measured from.
static uint8_t g_answers[3][CHANNELS];
static const AstcImage *g_answered;

static DkImageFormat FormatFor(const AstcImage *image) {
    static const struct {
        uint32_t size;
        DkImageFormat linear;
        DkImageFormat srgb;
    } FORMATS[] = {
        { 4, DkImageFormat_RGBA_ASTC_4x4, DkImageFormat_RGBA_ASTC_4x4_sRGB },
        { 6, DkImageFormat_RGBA_ASTC_6x6, DkImageFormat_RGBA_ASTC_6x6_sRGB },
        { 8, DkImageFormat_RGBA_ASTC_8x8, DkImageFormat_RGBA_ASTC_8x8_sRGB },
        { 12, DkImageFormat_RGBA_ASTC_12x12, DkImageFormat_RGBA_ASTC_12x12_sRGB },
    };
    for (uint32_t i = 0; i < sizeof(FORMATS) / sizeof(FORMATS[0]); ++i) {
        if (FORMATS[i].size == image->block_width && image->block_width == image->block_height) {
            return image->srgb ? FORMATS[i].srgb : FORMATS[i].linear;
        }
    }
    return DkImageFormat_None;
}

static void Answer(const AstcImage *image) {
    if (g_answered == image) {
        return;
    }
    static uint16_t wide[ASTC_MAX_TEXELS][4];
    static uint8_t codes[ASTC_MAX_TEXELS][4];
    const uint32_t bw = image->block_width;
    const uint32_t bh = image->block_height;
    for (uint32_t by = 0; by < TARGET; by += bh) {
        for (uint32_t bx = 0; bx < TARGET; bx += bw) {
            const uint8_t *const block = AstcImageBlock(image, bx, by);
            if (image->srgb) {
                AstcDecodeBlockSrgb(block, bw, bh, codes);
            } else {
                AstcDecodeBlock(block, bw, bh, wide);
            }
            for (uint32_t t = 0; t < bh && by + t < TARGET; ++t) {
                for (uint32_t s = 0; s < bw && bx + s < TARGET; ++s) {
                    for (uint32_t c = 0; c < 4u; ++c) {
                        const uint32_t i = ((by + t) * TARGET + bx + s) * 4u + c;
                        const uint32_t k = t * bw + s;
                        if (!image->srgb) {
                            g_answers[0][i] = AstcUnorm8Fp16(wide[k][c]);
                            AstcUnorm8Answers(wide[k][c], &g_answers[1][i], &g_answers[2][i]);
                        } else if (c == 3u) {
                            g_answers[0][i] = g_answers[1][i] = g_answers[2][i] = codes[k][c];
                        } else {
                            const double linear = FormatSrgbDecode(codes[k][c] / 255.0) * 255.0;
                            g_answers[0][i] = (uint8_t)floor(linear + 0.5);
                            g_answers[1][i] = (uint8_t)floor(linear);
                            g_answers[2][i] = (uint8_t)ceil(linear);
                        }
                    }
                }
            }
        }
    }
    g_answered = image;
}

/// How a readback compares with the answers, and the error of what is wrong.
typedef struct {
    uint32_t wrong;
    uint32_t worst;
    uint32_t first_x;
    uint32_t first_y;
    uint8_t got[4];
    uint8_t want[4];
    double squares[ASTC_IMAGE_REGIONS];
} Judged;

/// Judges @p pixels, where pixel (x, y) shows texel (x, y) * step + offset of a texture that
/// repeats the picture, into @p out and a picture of what was expected.
static void Judge(const AstcImage *image, const uint8_t *pixels, uint32_t step, uint32_t ox,
                  uint32_t oy, Judged *out) {
    Answer(image);
    memset(out, 0, sizeof(*out));
    Picture *const picture = PictureBegin(TARGET, TARGET, PICTURE_COLOUR, pixels);
    for (uint32_t y = 0; y < TARGET; ++y) {
        for (uint32_t x = 0; x < TARGET; ++x) {
            const uint32_t tx = (x * step + ox) % TARGET;
            const uint32_t ty = (y * step + oy) % TARGET;
            const uint8_t *const pixel = GpuPixel(pixels, TARGET, x, y);
            const uint32_t base = (ty * TARGET + tx) * 4u;
            uint32_t distance = 0;
            for (uint32_t c = 0; c < 4u; ++c) {
                const uint8_t g = pixel[c];
                if (g == g_answers[0][base + c] || g == g_answers[1][base + c] ||
                    g == g_answers[2][base + c]) {
                    continue;
                }
                const int32_t d = (int32_t)g - (int32_t)g_answers[0][base + c];
                const uint32_t away = (uint32_t)(d < 0 ? -d : d);
                out->squares[AstcImageRegion(tx, ty)] += (double)d * (double)d;
                if (away > distance) {
                    distance = away;
                }
            }
            PictureSet(picture, x, y, &g_answers[0][base],
                       distance == 0u ? PICTURE_MATCHED : PICTURE_WRONG);
            if (distance == 0u) {
                continue;
            }
            if (out->wrong == 0u) {
                out->first_x = tx;
                out->first_y = ty;
                memcpy(out->got, pixel, 4);
                memcpy(out->want, &g_answers[0][base], 4);
            }
            if (distance > out->worst) {
                out->worst = distance;
            }
            ++out->wrong;
        }
    }
}

/// PSNR over @p channels channels whose squared errors add to @p squares: "exact" for none.
static void Psnr(char *out, size_t size, double squares, double channels) {
    if (squares <= 0.0) {
        snprintf(out, size, "exact");
        return;
    }
    snprintf(out, size, "%.1f dB", 10.0 * log10(255.0 * 255.0 * channels / squares));
}

static void Report(const char *name, const AstcImage *image, const char *what, const Judged *j) {
    char whole[16];
    char region[ASTC_IMAGE_REGIONS][16];
    double all = 0.0;
    for (uint32_t r = 0; r < ASTC_IMAGE_REGIONS; ++r) {
        all += j->squares[r];
        Psnr(region[r], sizeof(region[r]), j->squares[r], CHANNELS / ASTC_IMAGE_REGIONS);
    }
    Psnr(whole, sizeof(whole), all, CHANNELS);
    CHECK_TRUE(SUITE, name, CHECK_EXACT, j->wrong == 0u,
               "%s %s: %u of %u texels wrong, worst channel %u away; PSNR %s (%s %s, %s %s, %s %s, "
               "%s %s); texel %u,%u of the picture is %02x%02x%02x%02x, the specification's "
               "%02x%02x%02x%02x",
               image->name, what, (unsigned)j->wrong, (unsigned)(TARGET * TARGET),
               (unsigned)j->worst, whole, ASTC_IMAGE_REGION[0], region[0], ASTC_IMAGE_REGION[1],
               region[1], ASTC_IMAGE_REGION[2], region[2], ASTC_IMAGE_REGION[3], region[3],
               (unsigned)j->first_x, (unsigned)j->first_y, j->got[0], j->got[1], j->got[2],
               j->got[3], j->want[0], j->want[1], j->want[2], j->want[3]);
}

/// Makes a @p size by @p size texture of @p image's blocks, repeated, written by the processor in
/// the block linear layout a driver gives a texture that tall. False with a sentence in @p why.
static bool MakeTexture(GpuContext *gpu, const AstcImage *image, uint32_t size, DkImage *out,
                        char *why, size_t why_size) {
    const DkImageFormat format = FormatFor(image);
    const uint32_t across = (size + image->block_width - 1u) / image->block_width;
    const uint32_t rows = (size + image->block_height - 1u) / image->block_height;
    // The smallest block height that holds the rows, at most sixteen gobs.
    uint32_t tile = 0;
    while (tile < 4u && (8u << tile) < rows) {
        ++tile;
    }
    const uint32_t gobs = 1u << tile;
    DkImageLayout layout;
    uint8_t *memory = NULL;
    if (format == DkImageFormat_None ||
        !GpuCreateImage(gpu, out, &layout, format, size, size, DkImageFlags_CustomTileSize, tile,
                        &memory)) {
        snprintf(why, why_size, "%s", gpu->failure[0] != '\0' ? gpu->failure : "no image for it");
        return false;
    }
    const uint32_t row_bytes = across * 16u;
    const uint64_t expected = (uint64_t)((row_bytes + 63u) / 64u) *
                              ((rows + 8u * gobs - 1u) / (8u * gobs)) * gobs * 512u;
    if (dkImageLayoutGetSize(&layout) != expected) {
        snprintf(why, why_size, "the library laid it out in %llu bytes, the block linear rule says %llu",
                 (unsigned long long)dkImageLayoutGetSize(&layout), (unsigned long long)expected);
        return false;
    }
    const uint32_t picture_across = (TARGET + image->block_width - 1u) / image->block_width;
    const uint32_t picture_rows = (TARGET + image->block_height - 1u) / image->block_height;
    for (uint32_t by = 0; by < rows; ++by) {
        for (uint32_t bx = 0; bx < across; ++bx) {
            const uint8_t *const block =
                image->blocks + 16u * ((by % picture_rows) * picture_across + bx % picture_across);
            // A block is one 16 byte run of a gob, so its bytes stay together.
            memcpy(memory + CohBlockLinearOffset(bx * 16u, by, row_bytes, gobs), block, 16);
        }
    }
    return true;
}

/// Draws @p draws quads over the target, the n-th fetching every @p step-th texel of @p image
/// from offset (n % step, n / step), and reads the target back: the last draw's texels.
static const uint8_t *Fetch(GpuContext *gpu, const DkShader *shaders, const DkImage *image,
                            uint32_t size, uint32_t step, uint32_t draws) {
    DkGpuAddr image_set = 0;
    DkGpuAddr sampler_set = 0;
    DkGpuAddr uniforms = 0;
    DkImageDescriptor *const images = (DkImageDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT, &image_set);
    DkSamplerDescriptor *const samplers = (DkSamplerDescriptor *)GpuAllocData(
        gpu, (uint32_t)sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &sampler_set);
    uint8_t *const steps =
        (uint8_t *)GpuAllocData(gpu, UNIFORM_SIZE * draws, DK_UNIFORM_BUF_ALIGNMENT, &uniforms);
    if (images == NULL || samplers == NULL || steps == NULL) {
        return NULL;
    }
    DkImageView view;
    dkImageViewDefaults(&view, image);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Nearest;
    sampler.magFilter = DkFilter_Nearest;
    sampler.mipFilter = DkMipFilter_None;
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    for (uint32_t n = 0; n < draws; ++n) {
        const FetchStep s = { { (int32_t)size, (int32_t)size, (int32_t)step, (int32_t)step },
                              { (int32_t)(n % step), (int32_t)(n / step), 0, 0 } };
        memset(steps + UNIFORM_SIZE * n, 0, UNIFORM_SIZE);
        memcpy(steps + UNIFORM_SIZE * n, &s, sizeof(s));
    }

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
    if (!GpuBindTriangleState(gpu, &shaders[0], &shaders[1], quad, 6)) {
        return NULL;
    }
    for (uint32_t n = 0; n < draws; ++n) {
        dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, uniforms + UNIFORM_SIZE * n,
                                  UNIFORM_SIZE);
        dkCmdBufDraw(gpu->cmdbuf, DkPrimitive_Triangles, 6, 1, 0, 0);
    }
    GpuSubmit(gpu);
    return gpu->ready ? GpuReadBack(gpu) : NULL;
}

static void Fidelity(GpuContext *gpu, const DkShader *shaders, const AstcImage *image) {
    char name[128];
    snprintf(name, sizeof(name), "a picture in %ux%u%s blocks decodes as the specification says, every texel",
             (unsigned)image->block_width, (unsigned)image->block_height, image->srgb ? " sRGB" : "");
    if (!ReportStart(SUITE, name)) {
        return;
    }
    DkImage texture;
    char why[288];
    if (!MakeTexture(gpu, image, TARGET, &texture, why, sizeof(why))) {
        ReportCase(SUITE, name, CHECK_NONE, false, why);
        return;
    }
    const uint8_t *const pixels = Fetch(gpu, shaders, &texture, TARGET, 1u, 1u);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return;
    }
    Judged j;
    Judge(image, pixels, 1u, 0u, 0u, &j);
    Report(name, image, "every texel", &j);
}

/// Makes a 2048x2048 texture of @p image and fetches every sixteenth texel: the upload's cost.
static bool Upload(GpuContext *gpu, const DkShader *shaders, const AstcImage *image, DkImage *texture) {
    char name[128];
    snprintf(name, sizeof(name), "a %ux%u texture of %ux%u blocks decodes all over, every %uth texel",
             BIG, BIG, (unsigned)image->block_width, (unsigned)image->block_height, BIG_STEP);
    if (!ReportStart(SUITE, name)) {
        return false;
    }
    char why[288];
    if (!MakeTexture(gpu, image, BIG, texture, why, sizeof(why))) {
        ReportCase(SUITE, name, CHECK_NONE, false, why);
        return false;
    }
    const uint8_t *const pixels = Fetch(gpu, shaders, texture, BIG, BIG_STEP, 1u);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw or readback did not happen");
        return false;
    }
    Judged j;
    Judge(image, pixels, BIG_STEP, 0u, 0u, &j);
    Report(name, image, "every sixteenth texel", &j);
    return true;
}

/// Fetches every texel of the 2048x2048 4x4 texture once, 256 draws of every sixteenth: what
/// sampling it costs, once it is uploaded.
static void Sampling(GpuContext *gpu, const DkShader *shaders, const AstcImage *image,
                     const DkImage *texture, bool made) {
    char name[128];
    snprintf(name, sizeof(name), "the %ux%u texture of %ux%u blocks fetched %u times over, each texel once",
             BIG, BIG, (unsigned)image->block_width, (unsigned)image->block_height, SAMPLING_DRAWS);
    if (!ReportStart(SUITE, name)) {
        return;
    }
    if (!made) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no texture: the case that makes it did not");
        return;
    }
    const uint8_t *const pixels = Fetch(gpu, shaders, texture, BIG, BIG_STEP, SAMPLING_DRAWS);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draws or readback did not happen");
        return;
    }
    Judged j;
    Judge(image, pixels, BIG_STEP, BIG_STEP - 1u, BIG_STEP - 1u, &j);
    Report(name, image, "the last draw's texels", &j);
}

void RunAstcImage(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET, TARGET);
    spec.code_size = 32u * 1024u;
    spec.data_size = UNIFORM_SIZE * SAMPLING_DRAWS + 64u * 1024u;
    // The two large textures (4 MiB and 1 MiB) and the six pictures, each rounded to its layout.
    spec.cpu_image_size = 7u * 1024u * 1024u;
    g_answered = NULL;

    if (!ReportStart(SUITE, "graphics device opens with room for the textures")) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for the textures", CHECK_NONE, opened, "%s",
               gpu.failure);
    if (!opened) {
        return;
    }
    if (!ReportStart(SUITE, "the fetching shader loads")) {
        GpuClose(&gpu);
        return;
    }
    DkShader shaders[2];
    const bool loaded = GpuLoadShader(&gpu, &shaders[0], ShaderData(SHADER_COMMON_VERT),
                                      ShaderSize(SHADER_COMMON_VERT)) &&
                        GpuLoadShader(&gpu, &shaders[1], ShaderData(SHADER_FETCH_STEP_FRAG),
                                      ShaderSize(SHADER_FETCH_STEP_FRAG));
    CHECK_TRUE(SUITE, "the fetching shader loads", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    for (uint32_t i = 0; i < ASTC_IMAGES; ++i) {
        Fidelity(&gpu, shaders, &ASTC_IMAGE[i]);
    }
    // 4x4 is the first picture and 8x8 the third.
    DkImage big4;
    DkImage big8;
    const bool made = Upload(&gpu, shaders, &ASTC_IMAGE[0], &big4);
    Upload(&gpu, shaders, &ASTC_IMAGE[2], &big8);
    Sampling(&gpu, shaders, &ASTC_IMAGE[0], &big4, made);
    GpuClose(&gpu);
}
