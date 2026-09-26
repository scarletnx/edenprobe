// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../formatref.h"
#include "../gpu.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Every colour format the graphics library can render to, one case each: the target is cleared
// to one value, a quad is drawn over part of it with another, and every pixel is read back and
// compared with what source/formatref.c says the format stores.
//
// Right after clear, because it is the same question asked of every format instead of one: the
// clear suite proves RGBA8 and the copy back, and this one proves that each other format reaches
// memory with its channels where its name puts them, converted by its own rule. Nothing between
// the shader and memory computes anything, and the values are ones every legal conversion lands
// on the same code for, so the integer and normalised cases are exact; the float ones are held
// to nought units in the last place, since every value drawn is exactly representable.
//
// Each format gets its own image, in memory no other format has used, so a case cannot be
// reading a view of the one before it; that question is the coherence suite's.

static const char *const SUITE = "format";

#define SIZE FORMAT_TARGET_SIZE
#define PIXEL_COUNT (SIZE * SIZE)

#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

// Room for every target. The largest is sixteen bytes a pixel, a sixteen kilobyte image, and
// each is lined up to the library's alignment for a render target, which is well under the rest.
#define IMAGE_MEMORY (4u * 1024u * 1024u)

/// The graphics library's format for each entry of FORMAT_TABLE, in the same order, with the
/// name spelled again so the pairing is checked rather than trusted.
static const struct {
    DkImageFormat format;
    const char *name;
} LIBRARY_FORMATS[] = {
    { DkImageFormat_R8_Unorm, "R8_Unorm" },
    { DkImageFormat_R8_Snorm, "R8_Snorm" },
    { DkImageFormat_R8_Uint, "R8_Uint" },
    { DkImageFormat_R8_Sint, "R8_Sint" },
    { DkImageFormat_R16_Float, "R16_Float" },
    { DkImageFormat_R16_Unorm, "R16_Unorm" },
    { DkImageFormat_R16_Snorm, "R16_Snorm" },
    { DkImageFormat_R16_Uint, "R16_Uint" },
    { DkImageFormat_R16_Sint, "R16_Sint" },
    { DkImageFormat_R32_Float, "R32_Float" },
    { DkImageFormat_R32_Uint, "R32_Uint" },
    { DkImageFormat_R32_Sint, "R32_Sint" },
    { DkImageFormat_RG8_Unorm, "RG8_Unorm" },
    { DkImageFormat_RG8_Snorm, "RG8_Snorm" },
    { DkImageFormat_RG8_Uint, "RG8_Uint" },
    { DkImageFormat_RG8_Sint, "RG8_Sint" },
    { DkImageFormat_RG16_Float, "RG16_Float" },
    { DkImageFormat_RG16_Unorm, "RG16_Unorm" },
    { DkImageFormat_RG16_Snorm, "RG16_Snorm" },
    { DkImageFormat_RG16_Uint, "RG16_Uint" },
    { DkImageFormat_RG16_Sint, "RG16_Sint" },
    { DkImageFormat_RG32_Float, "RG32_Float" },
    { DkImageFormat_RG32_Uint, "RG32_Uint" },
    { DkImageFormat_RG32_Sint, "RG32_Sint" },
    { DkImageFormat_RGBA8_Unorm, "RGBA8_Unorm" },
    { DkImageFormat_RGBA8_Snorm, "RGBA8_Snorm" },
    { DkImageFormat_RGBA8_Uint, "RGBA8_Uint" },
    { DkImageFormat_RGBA8_Sint, "RGBA8_Sint" },
    { DkImageFormat_RGBA16_Float, "RGBA16_Float" },
    { DkImageFormat_RGBA16_Unorm, "RGBA16_Unorm" },
    { DkImageFormat_RGBA16_Snorm, "RGBA16_Snorm" },
    { DkImageFormat_RGBA16_Uint, "RGBA16_Uint" },
    { DkImageFormat_RGBA16_Sint, "RGBA16_Sint" },
    { DkImageFormat_RGBA32_Float, "RGBA32_Float" },
    { DkImageFormat_RGBA32_Uint, "RGBA32_Uint" },
    { DkImageFormat_RGBA32_Sint, "RGBA32_Sint" },
    { DkImageFormat_RGBX8_Unorm_sRGB, "RGBX8_Unorm_sRGB" },
    { DkImageFormat_RGBA8_Unorm_sRGB, "RGBA8_Unorm_sRGB" },
    { DkImageFormat_RGB10A2_Unorm, "RGB10A2_Unorm" },
    { DkImageFormat_RGB10A2_Uint, "RGB10A2_Uint" },
    { DkImageFormat_RG11B10_Float, "RG11B10_Float" },
    { DkImageFormat_RGBX8_Unorm, "RGBX8_Unorm" },
    { DkImageFormat_RGBX8_Snorm, "RGBX8_Snorm" },
    { DkImageFormat_RGBX8_Uint, "RGBX8_Uint" },
    { DkImageFormat_RGBX8_Sint, "RGBX8_Sint" },
    { DkImageFormat_RGBX16_Float, "RGBX16_Float" },
    { DkImageFormat_RGBX16_Unorm, "RGBX16_Unorm" },
    { DkImageFormat_RGBX16_Snorm, "RGBX16_Snorm" },
    { DkImageFormat_RGBX16_Uint, "RGBX16_Uint" },
    { DkImageFormat_RGBX16_Sint, "RGBX16_Sint" },
    { DkImageFormat_RGBX32_Float, "RGBX32_Float" },
    { DkImageFormat_RGBX32_Uint, "RGBX32_Uint" },
    { DkImageFormat_RGBX32_Sint, "RGBX32_Sint" },
    { DkImageFormat_BGR565_Unorm, "BGR565_Unorm" },
    { DkImageFormat_BGR5_Unorm, "BGR5_Unorm" },
    { DkImageFormat_BGR5A1_Unorm, "BGR5A1_Unorm" },
    { DkImageFormat_BGRX8_Unorm, "BGRX8_Unorm" },
    { DkImageFormat_BGRA8_Unorm, "BGRA8_Unorm" },
    { DkImageFormat_BGRX8_Unorm_sRGB, "BGRX8_Unorm_sRGB" },
    { DkImageFormat_BGRA8_Unorm_sRGB, "BGRA8_Unorm_sRGB" },
};

#define LIBRARY_FORMAT_COUNT (sizeof(LIBRARY_FORMATS) / sizeof(LIBRARY_FORMATS[0]))

// The pixels the picture shows: the first four bytes of each, since a picture holds four and a
// pixel here holds one to sixteen. The raw file beside a failing case is these too; the whole
// pixel is in the case's line.
static uint8_t preview[PIXEL_COUNT * 4u];

static void Hex(char *out, size_t out_size, const uint8_t *bytes, uint32_t count) {
    size_t used = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < count && used + 3u <= out_size; ++i) {
        used += (size_t)snprintf(out + used, out_size - used, "%02x", bytes[i]);
    }
}

static bool Matches(const uint8_t *got, const uint8_t *want, const uint8_t *mask,
                    uint32_t bytes) {
    for (uint32_t i = 0; i < bytes; ++i) {
        if (((got[i] ^ want[i]) & mask[i]) != 0u) {
            return false;
        }
    }
    return true;
}

static GpuVertex Corner(float x, float y) {
    GpuVertex vertex;
    memset(&vertex, 0, sizeof(vertex));
    vertex.x = x;
    vertex.y = y;
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;
    return vertex;
}

static void FormatCase(GpuContext *gpu, const DkShader *vertex_shader,
                       const DkShader *const fragment_shaders[3], DkGpuAddr uniform,
                       uint32_t index) {
    const FormatInfo *const format = &FORMAT_TABLE[index];
    const DkImageFormat library_format = LIBRARY_FORMATS[index].format;
    char name[96];
    snprintf(name, sizeof(name), "%s keeps a clear and a quad drawn over it", format->name);
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const CheckTier tier =
        format->channel[0].kind == FORMAT_FLOAT || format->channel[0].kind == FORMAT_UFLOAT
            ? CHECK_ULP
            : CHECK_EXACT;

    const bool sized = GpuFormatBytes(library_format) == format->bytes;
    if (!sized) {
        CHECK_TRUE(SUITE, name, CHECK_NONE, sized,
                   "the reference says %u bytes a pixel and the graphics side %u",
                   (unsigned)format->bytes, (unsigned)GpuFormatBytes(library_format));
        return;
    }

    DkImage image;
    DkImageLayout layout;
    if (!GpuCreateImage(gpu, &image, &layout, library_format, SIZE, SIZE,
                        DkImageFlags_UsageRender, 0, NULL)) {
        ReportCase(SUITE, name, CHECK_NONE, false, gpu->failure);
        return;
    }

    const FormatValue clear = FormatClearValue(format);
    const FormatValue drawn = FormatDrawValue(format);

    GpuBeginPass(gpu);
    GpuSetTarget(gpu, &image, SIZE, SIZE);
    switch ((FormatOutput)format->output) {
    case FORMAT_OUTPUT_UINT:
        dkCmdBufClearColorUint(gpu->cmdbuf, 0, DkColorMask_RGBA, clear.word[0], clear.word[1],
                               clear.word[2], clear.word[3]);
        break;
    case FORMAT_OUTPUT_SINT:
        dkCmdBufClearColorSint(gpu->cmdbuf, 0, DkColorMask_RGBA, (int32_t)clear.word[0],
                               (int32_t)clear.word[1], (int32_t)clear.word[2],
                               (int32_t)clear.word[3]);
        break;
    case FORMAT_OUTPUT_FLOAT: {
        float values[4];
        memcpy(values, clear.word, sizeof(values));
        dkCmdBufClearColorFloat(gpu->cmdbuf, 0, DkColorMask_RGBA, values[0], values[1],
                                values[2], values[3]);
        break;
    }
    }

    // The value rides in the uniform buffer as four words, which is the only way a thirty two
    // bit integer reaches the shader whole: a vertex attribute here is a float.
    uint32_t words[4];
    memcpy(words, drawn.word, sizeof(words));
    dkCmdBufPushConstants(gpu->cmdbuf, uniform, UNIFORM_SIZE, 0, sizeof(words), words);
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, uniform, UNIFORM_SIZE);

    float clip[12];
    FormatQuadClip(clip);
    GpuVertex vertices[6];
    for (uint32_t i = 0; i < 6u; ++i) {
        vertices[i] = Corner(clip[i * 2u], clip[i * 2u + 1u]);
    }
    const bool recorded = GpuDrawTriangles(gpu, vertex_shader, fragment_shaders[format->output],
                                           vertices, 6);
    GpuSubmit(gpu);
    if (!recorded) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw was not recorded");
        return;
    }

    const uint8_t *const pixels = GpuReadBackImageBytes(gpu, &image, SIZE, SIZE, format->bytes);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the readback handed back nothing");
        return;
    }

    uint8_t want_clear[FORMAT_MAX_BYTES];
    uint8_t want_drawn[FORMAT_MAX_BYTES];
    uint8_t mask[FORMAT_MAX_BYTES];
    FormatPack(format, &clear, want_clear, mask);
    FormatPack(format, &drawn, want_drawn, mask);

    for (uint32_t i = 0; i < PIXEL_COUNT; ++i) {
        memset(preview + i * 4u, 0, 4u);
        memcpy(preview + i * 4u, pixels + (size_t)i * format->bytes,
               format->bytes < 4u ? format->bytes : 4u);
    }
    Picture *const picture = PictureBegin(SIZE, SIZE, PICTURE_BITS, preview);

    uint32_t wrong = 0;
    uint32_t kept_clear = 0;
    uint32_t drawn_outside = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    bool first_inside = false;
    const uint8_t *first_got = NULL;
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            const bool inside = FormatInsideQuad(x, y);
            const uint8_t *const want = inside ? want_drawn : want_clear;
            const uint8_t *const got = pixels + ((size_t)y * SIZE + x) * format->bytes;
            const bool right = Matches(got, want, mask, format->bytes);
            uint8_t shown[4] = { 0, 0, 0, 0 };
            memcpy(shown, want, format->bytes < 4u ? format->bytes : 4u);
            PictureSet(picture, x, y, shown, right ? PICTURE_MATCHED : PICTURE_WRONG);
            if (right) {
                continue;
            }
            if (inside && Matches(got, want_clear, mask, format->bytes)) {
                ++kept_clear;
            }
            if (!inside && Matches(got, want_drawn, mask, format->bytes)) {
                ++drawn_outside;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                first_inside = inside;
                first_got = got;
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        ReportCase(SUITE, name, tier, true, NULL);
        return;
    }
    char got_hex[FORMAT_MAX_BYTES * 2u + 1u];
    char want_hex[FORMAT_MAX_BYTES * 2u + 1u];
    char mask_hex[FORMAT_MAX_BYTES * 2u + 1u];
    Hex(got_hex, sizeof(got_hex), first_got, format->bytes);
    Hex(want_hex, sizeof(want_hex), first_inside ? want_drawn : want_clear, format->bytes);
    Hex(mask_hex, sizeof(mask_hex), mask, format->bytes);
    CHECK_TRUE(SUITE, name, tier, false,
               "%u of %u pixels wrong, first at (%u,%u) %s the quad holds %s, expected %s "
               "under the mask %s (bytes in memory order); %u inside still hold the clear "
               "value and %u outside hold the drawn one",
               (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)first_x, (unsigned)first_y,
               first_inside ? "inside" : "outside", got_hex, want_hex, mask_hex,
               (unsigned)kept_clear, (unsigned)drawn_outside);
}

void RunFormat(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, SIZE, SIZE);
    spec.code_size = 16u * 1024u;
    spec.data_size = 64u * 1024u;
    spec.image_size = IMAGE_MEMORY;
    spec.readback_bytes_per_pixel = FORMAT_MAX_BYTES;

    if (!ReportStart(SUITE, "graphics device opens with room for every format")) {
        return;
    }
    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with room for every format", CHECK_NONE, opened,
               "%s", gpu.failure);
    if (!opened) {
        return;
    }

    // The pairing between the two tables is by position, so it is checked by name before any
    // case leans on it: an entry added to one and not the other shifts every case after it.
    if (!ReportStart(SUITE, "every reference format is paired with the library's own")) {
        GpuClose(&gpu);
        return;
    }
    uint32_t mismatched = 0;
    const char *first_mismatch = "";
    for (uint32_t i = 0; i < FORMAT_COUNT; ++i) {
        if (i >= LIBRARY_FORMAT_COUNT || strcmp(LIBRARY_FORMATS[i].name, FORMAT_TABLE[i].name)) {
            if (mismatched == 0) {
                first_mismatch = FORMAT_TABLE[i].name;
            }
            ++mismatched;
        }
    }
    const bool paired = mismatched == 0 && FORMAT_COUNT == LIBRARY_FORMAT_COUNT;
    CHECK_TRUE(SUITE, "every reference format is paired with the library's own", CHECK_NONE,
               paired, "%u entries disagree, first \"%s\"; %u in the reference and %u here",
               (unsigned)mismatched, first_mismatch, (unsigned)FORMAT_COUNT,
               (unsigned)LIBRARY_FORMAT_COUNT);
    if (!paired) {
        GpuClose(&gpu);
        return;
    }

    if (!ReportStart(SUITE, "the three output shaders load")) {
        GpuClose(&gpu);
        return;
    }
    DkShader vertex_shader;
    DkShader fragment[3];
    const bool loaded =
        GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&gpu, &fragment[FORMAT_OUTPUT_FLOAT], ShaderData(SHADER_FORMAT_FLOAT_FRAG),
                      ShaderSize(SHADER_FORMAT_FLOAT_FRAG)) &&
        GpuLoadShader(&gpu, &fragment[FORMAT_OUTPUT_UINT], ShaderData(SHADER_FORMAT_UINT_FRAG),
                      ShaderSize(SHADER_FORMAT_UINT_FRAG)) &&
        GpuLoadShader(&gpu, &fragment[FORMAT_OUTPUT_SINT], ShaderData(SHADER_FORMAT_SINT_FRAG),
                      ShaderSize(SHADER_FORMAT_SINT_FRAG));
    CHECK_TRUE(SUITE, "the three output shaders load", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
        GpuClose(&gpu);
        return;
    }
    const DkShader *const fragment_shaders[3] = { &fragment[0], &fragment[1], &fragment[2] };

    if (!ReportStart(SUITE, "the uniform buffer has somewhere to live")) {
        GpuClose(&gpu);
        return;
    }
    DkGpuAddr uniform = 0;
    const bool reserved =
        GpuAllocData(&gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &uniform) != NULL;
    CHECK_TRUE(SUITE, "the uniform buffer has somewhere to live", CHECK_NONE, reserved,
               "the scratch block had no room for %u aligned bytes", (unsigned)UNIFORM_SIZE);
    if (!reserved) {
        GpuClose(&gpu);
        return;
    }

    ReportNote("format target %ux%u per format, quad over x %u to %u and y %u to %u, %u formats",
               SIZE, SIZE, FORMAT_QUAD_X0, FORMAT_QUAD_X1, FORMAT_QUAD_Y0, FORMAT_QUAD_Y1,
               (unsigned)FORMAT_COUNT);
    for (uint32_t i = 0; i < FORMAT_REFUSED_COUNT; ++i) {
        ReportNote("format not drawn: %s; %s", FORMAT_REFUSED[i].name, FORMAT_REFUSED[i].reason);
    }

    for (uint32_t i = 0; i < FORMAT_COUNT; ++i) {
        FormatCase(&gpu, &vertex_shader, fragment_shaders, uniform, i);
        // One bad format must not take the rest with it: a queue fault closes the context, and
        // the cases after it would report that same fault rather than their own answer.
        if (!gpu.ready) {
            const char *const cause = gpu.failure[0] != '\0' ? gpu.failure : "unknown";
            char why[320];
            snprintf(why, sizeof(why), "%s", cause);
            GpuClose(&gpu);
            if (!GpuOpenSpec(&gpu, &spec) ||
                !GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                               ShaderSize(SHADER_COMMON_VERT)) ||
                !GpuLoadShader(&gpu, &fragment[0], ShaderData(SHADER_FORMAT_FLOAT_FRAG),
                               ShaderSize(SHADER_FORMAT_FLOAT_FRAG)) ||
                !GpuLoadShader(&gpu, &fragment[1], ShaderData(SHADER_FORMAT_UINT_FRAG),
                               ShaderSize(SHADER_FORMAT_UINT_FRAG)) ||
                !GpuLoadShader(&gpu, &fragment[2], ShaderData(SHADER_FORMAT_SINT_FRAG),
                               ShaderSize(SHADER_FORMAT_SINT_FRAG)) ||
                GpuAllocData(&gpu, UNIFORM_SIZE, DK_UNIFORM_BUF_ALIGNMENT, &uniform) == NULL) {
                // A line for every format left, so the count of cases never depends on how far
                // a broken machine got.
                GpuClose(&gpu);
                for (uint32_t rest = i + 1u; rest < FORMAT_COUNT; ++rest) {
                    char name[96];
                    snprintf(name, sizeof(name), "%s keeps a clear and a quad drawn over it",
                             FORMAT_TABLE[rest].name);
                    if (ReportStart(SUITE, name)) {
                        CHECK_TRUE(SUITE, name, CHECK_NONE, false,
                                   "not drawn: the device did not come back after \"%s\"", why);
                    }
                }
                return;
            }
            ReportNote("format: the device was reopened after \"%s\"", why);
        }
    }

    GpuClose(&gpu);
}
