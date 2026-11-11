// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Storage images: what a compute program's store writes, and what its load reads, as a game's
// programs make the levels of a texture, fill a volume or keep masks in an array.
//
// A store converts by the format of the image its descriptor names (the shader declares none):
// ten formats, each a value that lands on one encoding under any legal rounding, the floats ones
// every narrower float holds exactly (Vulkan's "Texel Output Format Conversion"; the unsigned
// normalised clamp at both ends, the signed one to -127 for -1 and below). A store to level 1 of
// three, to layer 1 of an array or to slice 1 of a volume writes that texel and no other: every
// level, layer and slice is filled first through the copy engine with bytes of their own, read
// back the same way after the dispatch, and judged byte for byte. A formatted load of an RGBA8
// texel gives each byte over 255 rounded to the nearest float; and a load and a store of one
// texel in an array at level 1 and in a volume keep its bits. These are the console's answers
// ScarletNX's storage tests hold its runtime to; every case is exact.

static const char *const SUITE = "storage";

#define WIDTH 8u
#define HEIGHT 4u
#define DEPTH 3u
#define LEVELS 3u
/// Where every store goes, in its level, layer or slice.
#define AT_X 1
#define AT_Y 1
#define AT_Z 1
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT
/// The most bytes an image here holds: the RGBA32 one, or three layers of a three level R32 one.
#define MAX_BYTES (WIDTH * HEIGHT * 16u)

typedef struct {
    GpuContext gpu;
    DkShader stores[3]; ///< float, unsigned, signed
    DkShader layer;
    DkShader slice;
    DkShader load;
    DkShader copy;
    DkShader copy_int;
} StorageContext;

static StorageContext context;

/// An image the suite stores into, and what its bytes should be, level by level.
typedef struct {
    DkImage image;
    DkImageType type;
    DkImageFormat format;
    uint32_t bytes;  ///< per texel
    uint32_t layers; ///< layers of an array, slices of a volume, one otherwise
    uint32_t levels;
    uint8_t want[LEVELS][MAX_BYTES];
} Storage;

static Storage images[2];

static uint32_t LevelSide(uint32_t side, uint32_t level) {
    const uint32_t s = side >> level;
    return s == 0u ? 1u : s;
}

/// Texels across, down and deep (layers or slices) at @p level.
static void LevelSize(const Storage *s, uint32_t level, uint32_t *w, uint32_t *h, uint32_t *d) {
    *w = LevelSide(WIDTH, level);
    *h = LevelSide(HEIGHT, level);
    *d = s->type == DkImageType_3D ? LevelSide(s->layers, level) : s->layers;
}

static uint32_t LevelBytes(const Storage *s, uint32_t level) {
    uint32_t w, h, d;
    LevelSize(s, level, &w, &h, &d);
    return w * h * d * s->bytes;
}

static bool Fail(const char *name, const char *why) {
    ReportCase(SUITE, name, CHECK_NONE, false,
               context.gpu.failure[0] != '\0' ? context.gpu.failure : why);
    return false;
}

/// Makes @p s, fills every level with bytes of its own (@p seed apart from another image's), and
/// records the uploads into the open list.
static bool Make(Storage *s, DkImageType type, DkImageFormat format, uint32_t bytes,
                 uint32_t layers, uint32_t levels, uint32_t seed) {
    s->type = type;
    s->format = format;
    s->bytes = bytes;
    s->layers = layers;
    s->levels = levels;
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, context.gpu.device);
    maker.type = type;
    maker.flags = DkImageFlags_UsageLoadStore;
    maker.format = format;
    maker.dimensions[0] = WIDTH;
    maker.dimensions[1] = HEIGHT;
    maker.dimensions[2] = type == DkImageType_2D ? 0u : layers;
    maker.mipLevels = levels;
    DkImageLayout layout;
    if (!GpuCreateFromMaker(&context.gpu, &s->image, &layout, &maker, NULL)) {
        return false;
    }
    for (uint32_t level = 0; level < levels; ++level) {
        const uint32_t size = LevelBytes(s, level);
        DkGpuAddr address = 0;
        uint8_t *const staged = (uint8_t *)GpuAllocData(&context.gpu, size, 256u, &address);
        if (staged == NULL) {
            return false;
        }
        for (uint32_t i = 0; i < size; ++i) {
            s->want[level][i] = (uint8_t)(i * 37u + 11u + level * 101u + seed * 59u + (i >> 8));
        }
        memcpy(staged, s->want[level], size);
        uint32_t w, h, d;
        LevelSize(s, level, &w, &h, &d);
        DkImageView view;
        dkImageViewDefaults(&view, &s->image);
        view.mipLevelOffset = (uint8_t)level;
        view.mipLevelCount = 1;
        // The copy's image height is the bytes from one layer or slice to the next.
        const DkCopyBuf source = { address, w * bytes, w * h * bytes };
        const DkImageRect rect = { 0, 0, 0, w, h, d };
        dkCmdBufCopyBufferToImage(context.gpu.cmdbuf, &source, &view, &rect, 0);
    }
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    return true;
}

/// Records a descriptor for @p s at @p level, for load and store, into @p out.
static void Describe(const Storage *s, uint32_t level, DkImageDescriptor *out) {
    DkImageView view;
    dkImageViewDefaults(&view, &s->image);
    view.mipLevelOffset = (uint8_t)level;
    view.mipLevelCount = 1;
    dkImageDescriptorInitialize(out, &view, true, false);
}

/// Records a dispatch of @p shader over the images @p first and @p second (or none) at
/// @p level, with @p value and the place every store goes as its uniforms, and @p out, when
/// given, as storage buffer 0.
static bool Dispatch(const DkShader *shader, const Storage *first, const Storage *second,
                     uint32_t level, const uint32_t value[4], DkGpuAddr out) {
    DkGpuAddr set = 0, params = 0;
    DkImageDescriptor *const descriptors = (DkImageDescriptor *)GpuAllocData(
        &context.gpu, 2u * (uint32_t)sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT,
        &set);
    uint32_t *const words =
        (uint32_t *)GpuAllocData(&context.gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &params);
    if (descriptors == NULL || words == NULL) {
        return false;
    }
    Describe(first, level, &descriptors[0]);
    if (second != NULL) {
        Describe(second, level, &descriptors[1]);
    }
    memset(words, 0, UNIFORM_SIZE);
    memcpy(words, value, 16);
    words[4] = AT_X;
    words[5] = AT_Y;
    words[6] = AT_Z;
    dkCmdBufBindImageDescriptorSet(context.gpu.cmdbuf, set, 2);
    const DkShader *const shaders[] = { shader };
    dkCmdBufBindShaders(context.gpu.cmdbuf, DkStageFlag_Compute, shaders, 1);
    dkCmdBufBindUniformBuffer(context.gpu.cmdbuf, DkStage_Compute, 0, params, UNIFORM_SIZE);
    dkCmdBufBindImage(context.gpu.cmdbuf, DkStage_Compute, 0, dkMakeImageHandle(0));
    if (second != NULL) {
        dkCmdBufBindImage(context.gpu.cmdbuf, DkStage_Compute, 1, dkMakeImageHandle(1));
    }
    if (out != 0) {
        dkCmdBufBindStorageBuffer(context.gpu.cmdbuf, DkStage_Compute, 0, out, 16u);
    }
    dkCmdBufDispatchCompute(context.gpu.cmdbuf, 1, 1, 1);
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    return true;
}

/// Records copies of every level of @p s into scratch, and hands back where each landed.
static bool Download(const Storage *s, uint8_t *got[LEVELS]) {
    for (uint32_t level = 0; level < s->levels; ++level) {
        DkGpuAddr address = 0;
        got[level] = (uint8_t *)GpuAllocData(&context.gpu, LevelBytes(s, level), 256u, &address);
        if (got[level] == NULL) {
            return false;
        }
        memset(got[level], 0xA5, LevelBytes(s, level));
        uint32_t w, h, d;
        LevelSize(s, level, &w, &h, &d);
        DkImageView view;
        dkImageViewDefaults(&view, &s->image);
        view.mipLevelOffset = (uint8_t)level;
        view.mipLevelCount = 1;
        const DkCopyBuf destination = { address, w * s->bytes, w * h * s->bytes };
        const DkImageRect rect = { 0, 0, 0, w, h, d };
        dkCmdBufCopyImageToBuffer(context.gpu.cmdbuf, &view, &rect, &destination, 0);
    }
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full, 0);
    return true;
}

/// Puts @p texel's bytes where the store goes in level @p level of what @p s should hold.
static void Expect(Storage *s, uint32_t level, const uint8_t *texel) {
    uint32_t w, h, d;
    LevelSize(s, level, &w, &h, &d);
    const uint32_t z = s->type == DkImageType_2D ? 0u : AT_Z;
    memcpy(s->want[level] + ((z * h + AT_Y) * w + AT_X) * s->bytes, texel, s->bytes);
}

/// Runs the list and holds every level of @p s, read back into @p got, to what it should hold.
static void Judge(const char *name, const Storage *s, uint8_t *got[LEVELS]) {
    GpuSubmit(&context.gpu);
    if (!context.gpu.ready) {
        Fail(name, "the list did not run");
        return;
    }
    uint32_t wrong = 0, first_level = 0, first = 0;
    for (uint32_t level = 0; level < s->levels; ++level) {
        const uint32_t size = LevelBytes(s, level);
        for (uint32_t i = 0; i < size; i += s->bytes) {
            if (memcmp(got[level] + i, s->want[level] + i, s->bytes) != 0) {
                if (wrong == 0) {
                    first_level = level;
                    first = i;
                }
                ++wrong;
            }
        }
    }
    char got_text[40] = "", want_text[40] = "";
    for (uint32_t b = 0; b < s->bytes; ++b) {
        snprintf(got_text + b * 2u, 3, "%02x", got[first_level][first + b]);
        snprintf(want_text + b * 2u, 3, "%02x", s->want[first_level][first + b]);
    }
    uint32_t w, h, d;
    LevelSize(s, first_level, &w, &h, &d);
    const uint32_t index = first / s->bytes;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u texels wrong; the first, (%u, %u, %u) of level %u, holds the bytes %s, expected "
               "%s",
               (unsigned)wrong, (unsigned)(index % w), (unsigned)(index / w % h),
               (unsigned)(index / (w * h)), (unsigned)first_level, got_text, want_text);
}

typedef enum { KIND_FLOAT, KIND_UINT, KIND_SINT } Kind;

/// One store of @p value (its four words, floats or integers by @p kind) into a one level 2D
/// image of @p format, which must hold @p texel there afterwards and nothing else changed.
static void FormatCase(const char *name, DkImageFormat format, uint32_t bytes, Kind kind,
                       const uint32_t value[4], const uint8_t *texel) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Storage *const s = &images[0];
    GpuBeginList(&context.gpu);
    uint8_t *got[LEVELS];
    if (!Make(s, DkImageType_2D, format, bytes, 1, 1, 0) ||
        !Dispatch(&context.stores[kind], s, NULL, 0, value, 0) || !Download(s, got)) {
        Fail(name, "no room for the image");
        return;
    }
    Expect(s, 0, texel);
    Judge(name, s, got);
}

static uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/// A store of 0x89abcdef to the store's place in level @p level of an R32_Uint image of
/// @p type, every level, layer and slice judged.
static void PlaceCase(const char *name, DkImageType type, const DkShader *shader, uint32_t layers,
                      uint32_t levels, uint32_t level) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Storage *const s = &images[0];
    const uint32_t value[4] = { 0x89ABCDEFu, 0, 0, 0 };
    GpuBeginList(&context.gpu);
    uint8_t *got[LEVELS];
    if (!Make(s, type, DkImageFormat_R32_Uint, 4, layers, levels, 1) ||
        !Dispatch(shader, s, NULL, level, value, 0) || !Download(s, got)) {
        Fail(name, "no room for the image");
        return;
    }
    const uint8_t texel[4] = { 0xEF, 0xCD, 0xAB, 0x89 };
    Expect(s, level, texel);
    Judge(name, s, got);
}

/// The formatted load of texel 00 80 ff 40 of an RGBA8 image: 0, 128/255, 1 and 64/255, each
/// rounded to the nearest float.
static void LoadCase(void) {
    static const char *const NAME =
        "a formatted load of RGBA8 00 80 ff 40 gives 0, 128/255, 1 and 64/255 to the bit";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Storage *const s = &images[0];
    DkGpuAddr out_address = 0;
    uint32_t *const out = (uint32_t *)GpuAllocData(&context.gpu, 16u, 256u, &out_address);
    GpuBeginList(&context.gpu);
    if (out == NULL || !Make(s, DkImageType_2D, DkImageFormat_RGBA8_Unorm, 4, 1, 1, 2)) {
        Fail(NAME, "no room for the image");
        return;
    }
    memset(out, 0xA5, 16);
    // The texel the load reads, written over the filled image by a second upload of one texel.
    DkGpuAddr texel_address = 0;
    uint8_t *const texel = (uint8_t *)GpuAllocData(&context.gpu, 4u, 256u, &texel_address);
    if (texel == NULL) {
        Fail(NAME, "no room for the texel");
        return;
    }
    static const uint8_t TEXEL[4] = { 0x00, 0x80, 0xFF, 0x40 };
    memcpy(texel, TEXEL, 4);
    DkImageView view;
    dkImageViewDefaults(&view, &s->image);
    const DkCopyBuf source = { texel_address, 4u, 4u };
    const DkImageRect rect = { AT_X, AT_Y, 0, 1, 1, 1 };
    dkCmdBufCopyBufferToImage(context.gpu.cmdbuf, &source, &view, &rect, 0);
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
    const uint32_t none[4] = { 0, 0, 0, 0 };
    if (!Dispatch(&context.load, s, NULL, 0, none, out_address)) {
        Fail(NAME, "no room for the dispatch");
        return;
    }
    GpuSubmit(&context.gpu);
    if (!context.gpu.ready) {
        Fail(NAME, "the list did not run");
        return;
    }
    const uint32_t want[4] = { 0x00000000u, 0x3F008081u, 0x3F800000u, 0x3E808081u };
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, memcmp(out, want, 16) == 0,
               "the load gave 0x%08x 0x%08x 0x%08x 0x%08x, expected 0x%08x 0x%08x 0x%08x 0x%08x",
               (unsigned)out[0], (unsigned)out[1], (unsigned)out[2], (unsigned)out[3],
               (unsigned)want[0], (unsigned)want[1], (unsigned)want[2], (unsigned)want[3]);
}

/// A load of the store's place from one image of @p type and @p format and a store of it at the
/// same place of another, at @p level: the second holds the first's bytes there, @p texel, and
/// neither image changes anywhere else.
static void CopyCase(const char *name, DkImageType type, DkImageFormat format,
                     const DkShader *shader, uint32_t levels, uint32_t level,
                     const uint8_t texel[4]) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    Storage *const from = &images[0];
    Storage *const to = &images[1];
    GpuBeginList(&context.gpu);
    if (!Make(from, type, format, 4, DEPTH, levels, 3) || !Make(to, type, format, 4, DEPTH, levels, 4)) {
        Fail(name, "no room for the images");
        return;
    }
    // The texel the load reads, put in the first image by one more upload.
    DkGpuAddr texel_address = 0;
    uint8_t *const staged = (uint8_t *)GpuAllocData(&context.gpu, 4u, 256u, &texel_address);
    if (staged == NULL) {
        Fail(name, "no room for the texel");
        return;
    }
    memcpy(staged, texel, 4);
    DkImageView view;
    dkImageViewDefaults(&view, &from->image);
    view.mipLevelOffset = (uint8_t)level;
    view.mipLevelCount = 1;
    const DkCopyBuf source = { texel_address, 4u, 4u };
    const DkImageRect rect = { AT_X, AT_Y, AT_Z, 1, 1, 1 };
    dkCmdBufCopyBufferToImage(context.gpu.cmdbuf, &source, &view, &rect, 0);
    dkCmdBufBarrier(context.gpu.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
    Expect(from, level, texel);
    Expect(to, level, texel);
    const uint32_t none[4] = { 0, 0, 0, 0 };
    uint8_t *got_from[LEVELS], *got_to[LEVELS];
    if (!Dispatch(shader, from, to, level, none, 0) || !Download(from, got_from) ||
        !Download(to, got_to)) {
        Fail(name, "no room for the dispatch");
        return;
    }
    GpuSubmit(&context.gpu);
    if (!context.gpu.ready) {
        Fail(name, "the list did not run");
        return;
    }
    uint32_t wrong = 0;
    for (uint32_t level_i = 0; level_i < levels; ++level_i) {
        const uint32_t size = LevelBytes(to, level_i);
        wrong += memcmp(got_from[level_i], from->want[level_i], size) != 0 ? 1u : 0u;
        wrong += memcmp(got_to[level_i], to->want[level_i], size) != 0 ? 1u : 0u;
    }
    uint32_t w, h, d;
    LevelSize(to, level, &w, &h, &d);
    const uint8_t *const landed = got_to[level] + ((AT_Z * h + AT_Y) * w + AT_X) * 4u;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0,
               "%u of %u levels differ from what they held; the stored texel holds "
               "%02x%02x%02x%02x, expected %02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)(2u * levels), landed[0], landed[1], landed[2],
               landed[3], texel[0], texel[1], texel[2], texel[3]);
}

void RunStorage(void) {
    static const char *const OPEN = "graphics device opens with a compute queue and room for the "
                                    "images";
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    memset(&context, 0, sizeof(context));
    GpuSpec spec;
    GpuSpecDefaults(&spec, 16, 16);
    spec.compute = true;
    spec.code_size = 64u * 1024u;
    spec.data_size = 512u * 1024u;
    spec.image_size = 8u * 1024u * 1024u;
    const bool opened = GpuOpenSpec(&context.gpu, &spec);
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, opened, "%s", context.gpu.failure);
    if (!opened) {
        return;
    }
    static const char *const LOAD = "the storage shaders load";
    if (!ReportStart(SUITE, LOAD)) {
        GpuClose(&context.gpu);
        return;
    }
    static const struct {
        DkShader *shader;
        ShaderId id;
    } SHADERS[] = {
        { &context.stores[KIND_FLOAT], SHADER_STORAGE_FLOAT_COMP },
        { &context.stores[KIND_UINT], SHADER_STORAGE_UINT_COMP },
        { &context.stores[KIND_SINT], SHADER_STORAGE_SINT_COMP },
        { &context.layer, SHADER_STORAGE_LAYER_COMP },
        { &context.slice, SHADER_STORAGE_SLICE_COMP },
        { &context.load, SHADER_STORAGE_LOAD_COMP },
        { &context.copy, SHADER_STORAGE_COPY_COMP },
        { &context.copy_int, SHADER_STORAGE_COPY_INT_COMP },
    };
    bool loaded = true;
    for (uint32_t i = 0; loaded && i < sizeof(SHADERS) / sizeof(SHADERS[0]); ++i) {
        loaded = GpuLoadShader(&context.gpu, SHADERS[i].shader, ShaderData(SHADERS[i].id),
                               ShaderSize(SHADERS[i].id));
    }
    CHECK_TRUE(SUITE, LOAD, CHECK_NONE, loaded, "%s", context.gpu.failure);
    if (!loaded) {
        GpuClose(&context.gpu);
        return;
    }

    // Normalised: clamped at both ends; 0.2 is 51 of 255 and a third 341 of 1023 under either
    // rounding; two thirds of the two alpha bits is 2.
    FormatCase("a store of -0.25 1 0.2 2 into RGBA8 writes 00 ff 33 ff", DkImageFormat_RGBA8_Unorm,
               4, KIND_FLOAT, (const uint32_t[4]){ Bits(-0.25f), Bits(1.0f), Bits(0.2f), Bits(2.0f) },
               (const uint8_t[4]){ 0x00, 0xFF, 0x33, 0xFF });
    FormatCase("a store of -1 1 -2 0 into RGBA8 snorm writes 81 7f 81 00",
               DkImageFormat_RGBA8_Snorm, 4, KIND_FLOAT,
               (const uint32_t[4]){ Bits(-1.0f), Bits(1.0f), Bits(-2.0f), Bits(0.0f) },
               (const uint8_t[4]){ 0x81, 0x7F, 0x81, 0x00 });
    FormatCase("a store of 1 1/3 0 2/3 into RGB10A2 writes 0x800557ff", DkImageFormat_RGB10A2_Unorm,
               4, KIND_FLOAT,
               (const uint32_t[4]){ Bits(1.0f), Bits(1.0f / 3.0f), Bits(0.0f), Bits(2.0f / 3.0f) },
               (const uint8_t[4]){ 0xFF, 0x57, 0x05, 0x80 });
    // Floats: each value a narrower float holds exactly.
    FormatCase("a store of 1 0.5 2 into RG11B10F writes 0x801c03c0", DkImageFormat_RG11B10_Float, 4,
               KIND_FLOAT, (const uint32_t[4]){ Bits(1.0f), Bits(0.5f), Bits(2.0f), Bits(1.0f) },
               (const uint8_t[4]){ 0xC0, 0x03, 0x1C, 0x80 });
    FormatCase("a store of -0.25 0.5 -0.75 1 into RGBA16F writes its halves",
               DkImageFormat_RGBA16_Float, 8, KIND_FLOAT,
               (const uint32_t[4]){ Bits(-0.25f), Bits(0.5f), Bits(-0.75f), Bits(1.0f) },
               (const uint8_t[8]){ 0x00, 0xB4, 0x00, 0x38, 0x00, 0xBA, 0x00, 0x3C });
    FormatCase("a store of -0.25 into R32F writes 0xbe800000", DkImageFormat_R32_Float, 4,
               KIND_FLOAT, (const uint32_t[4]){ Bits(-0.25f), Bits(0.5f), Bits(-0.75f), Bits(1.0f) },
               (const uint8_t[4]){ 0x00, 0x00, 0x80, 0xBE });
    FormatCase("a store of -0.25 0.5 -0.75 1 into RGBA32F writes its floats",
               DkImageFormat_RGBA32_Float, 16, KIND_FLOAT,
               (const uint32_t[4]){ Bits(-0.25f), Bits(0.5f), Bits(-0.75f), Bits(1.0f) },
               (const uint8_t[16]){ 0x00, 0x00, 0x80, 0xBE, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00,
                                    0x40, 0xBF, 0x00, 0x00, 0x80, 0x3F });
    // Integers, each in its format's range.
    FormatCase("a store of 0x89abcdef into R32 uint writes it", DkImageFormat_R32_Uint, 4, KIND_UINT,
               (const uint32_t[4]){ 0x89ABCDEFu, 1u, 2u, 3u },
               (const uint8_t[4]){ 0xEF, 0xCD, 0xAB, 0x89 });
    FormatCase("a store of 17 34 51 68 into RGBA8 uint writes 11 22 33 44",
               DkImageFormat_RGBA8_Uint, 4, KIND_UINT, (const uint32_t[4]){ 17u, 34u, 51u, 68u },
               (const uint8_t[4]){ 0x11, 0x22, 0x33, 0x44 });
    FormatCase("a store of -17 34 -51 68 into RGBA16 sint writes ffef 0022 ffcd 0044",
               DkImageFormat_RGBA16_Sint, 8, KIND_SINT,
               (const uint32_t[4]){ (uint32_t)-17, 34u, (uint32_t)-51, 68u },
               (const uint8_t[8]){ 0xEF, 0xFF, 0x22, 0x00, 0xCD, 0xFF, 0x44, 0x00 });

    PlaceCase("a store to level 1 of three writes that texel and no other", DkImageType_2D,
              &context.stores[KIND_UINT], 1, LEVELS, 1);
    PlaceCase("a store to layer 1 of a three layer array writes that texel and no other",
              DkImageType_2DArray, &context.layer, DEPTH, 1, 0);
    PlaceCase("a store to slice 1 of a volume writes that texel and no other", DkImageType_3D,
              &context.slice, DEPTH, 1, 0);

    LoadCase();
    CopyCase("a load and a store of layer 1 at level 1 of an R32F array keep the float's bits",
             DkImageType_2DArray, DkImageFormat_R32_Float, &context.copy, 2, 1,
             (const uint8_t[4]){ 0x00, 0x00, 0x40, 0xBF });
    CopyCase("a load and a store of slice 1 of an RGBA8 sint volume keep its four bytes",
             DkImageType_3D, DkImageFormat_RGBA8_Sint, &context.copy_int, 1, 0,
             (const uint8_t[4]){ 0xEF, 0x22, 0xCD, 0x44 });
    GpuClose(&context.gpu);
}
