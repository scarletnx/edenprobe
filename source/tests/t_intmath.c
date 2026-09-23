// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../intref.h"
#include "../picture.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Integer arithmetic in a fragment shader, into a target that stores 32 bit unsigned integers,
// compared bit for bit against the same arithmetic written out on the processor.
//
// The suite that judges a shader translator, and the strongest one here: nothing normalises or
// rounds between the shader and the readback, and the operands come from the fragment's own
// coordinate, so every pixel is a different input. The order of the cases is the order they can
// be believed in, with the coordinate and the uniform buffer pinned before any arithmetic is
// judged.

static const char *const SUITE = "intmath";

#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define PIXEL_COUNT (TARGET_WIDTH * TARGET_HEIGHT)

// What the target is cleared to before each draw. Not nought: a pixel the draw never reached
// would then be indistinguishable from an operation that legitimately produced nought, and two
// of the operations here do produce nought for some inputs.
#define SENTINEL 0xDEADBEEFu

// The value the uniform guard case expects to read back. Chosen to have bits set in all four
// bytes, so a uniform buffer delivered with its bytes in the wrong order would say so.
#define UNIFORM_PROBE 0x5A17C0DEu

// One page of uniform buffer, which is far more than the two words in it. The graphics unit
// wants the address of a uniform buffer aligned to DK_UNIFORM_BUF_ALIGNMENT, and rounding the
// size to the same thing keeps the binding and the allocation saying the same number.
#define UNIFORM_SIZE DK_UNIFORM_BUF_ALIGNMENT

/// The block the shader reads its mode out of. The layout is std140's, which for two unsigned
/// integers is simply the two of them; the padding is here so the structure is a round sixteen
/// bytes and the push below is a whole number of words.
typedef struct {
    uint32_t mode;
    uint32_t probe;
    uint32_t padding[2];
} IntParams;

static GpuVertex Corner(float x, float y) {
    GpuVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = 0.0f;
    vertex.u = 0.0f;
    vertex.v = 0.0f;
    // The fragment shader in this suite ignores the colour entirely; it is here because one
    // vertex layout serves the whole program.
    vertex.r = 1.0f;
    vertex.g = 1.0f;
    vertex.b = 1.0f;
    vertex.a = 1.0f;
    return vertex;
}

/// The quad that puts one fragment on every pixel of the target.
static void FullScreenQuad(GpuVertex out[6]) {
    out[0] = Corner(-1.0f, 1.0f);
    out[1] = Corner(1.0f, 1.0f);
    out[2] = Corner(1.0f, -1.0f);
    out[3] = Corner(-1.0f, 1.0f);
    out[4] = Corner(1.0f, -1.0f);
    out[5] = Corner(-1.0f, -1.0f);
}

/// The 32 bit value the readback holds for one pixel.
///
/// Copied rather than dereferenced through a pointer: the readback block is mapped uncached to
/// the processor, and a copy of four bytes is the one form that is right whatever the compiler
/// decides to do with the alignment.
static uint32_t PixelValue(const uint8_t *pixels, uint32_t x, uint32_t y) {
    uint32_t value;
    memcpy(&value, pixels + ((size_t)y * TARGET_WIDTH + x) * 4u, sizeof(value));
    return value;
}

/// Compares all 4096 pixels against the reference and writes the sentence that explains a
/// failure: how many were wrong, and where the first one was with both values.
static bool CheckEveryPixel(const uint8_t *pixels, IntMathMode mode, Picture *picture,
                            char *detail, size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint32_t first_got = 0;
    uint32_t first_want = 0;
    uint32_t untouched = 0;

    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            const uint32_t got = PixelValue(pixels, x, y);
            const uint32_t want = IntMathReference(mode, x, y, UNIFORM_PROBE);
            // The four bytes in the order memory holds them, which is the order the picture and
            // the raw file both use, so one rule covers all three.
            uint8_t wanted[4];
            memcpy(wanted, &want, sizeof(wanted));
            PictureSet(picture, x, y, wanted, got == want ? PICTURE_MATCHED : PICTURE_WRONG);
            if (got == want) {
                continue;
            }
            if (got == SENTINEL) {
                ++untouched;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                first_got = got;
                first_want = want;
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        detail[0] = '\0';
        return true;
    }
    snprintf(detail, detail_size,
             "%u of %u pixels wrong, first at (%u,%u) is 0x%08x, expected 0x%08x; %u still hold "
             "the sentinel 0x%08x%s",
             (unsigned)wrong, (unsigned)PIXEL_COUNT, (unsigned)first_x, (unsigned)first_y,
             (unsigned)first_got, (unsigned)first_want, (unsigned)untouched, (unsigned)SENTINEL,
             untouched == PIXEL_COUNT ? ", so the draw wrote nothing at all: under an emulator, a "
                                        "shader that did not translate, and the emulator's log "
                                        "names the instruction"
                                      : "");
    return false;
}

/// The shaders the modes are spread over. One shader per family rather than one for all, so an
/// instruction an emulator cannot translate, which fails the whole shader it is in, takes down
/// its own family's cases and leaves the rest measuring what their names say.
static const struct {
    ShaderId shader;
    IntMathMode first;
} FAMILIES[] = {
    { SHADER_INTMATH_FRAG, INTMATH_FRAGCOORD_X },
    { SHADER_INTCONV_FRAG, INTMATH_INT_TO_FLOAT },
    { SHADER_INTSHIFT_FRAG, INTMATH_SHIFT_LEFT_WIDE },
    { SHADER_INTDIV_FRAG, INTMATH_SIGNED_DIVIDE },
    { SHADER_INTREM_FRAG, INTMATH_SIGNED_REMAINDER },
    { SHADER_INTBITS_FRAG, INTMATH_FIND_MSB_UINT },
};

#define FAMILY_COUNT (sizeof(FAMILIES) / sizeof(FAMILIES[0]))

static size_t FamilyOf(IntMathMode mode) {
    size_t family = 0;
    for (size_t i = 0; i < FAMILY_COUNT; ++i) {
        if (mode >= FAMILIES[i].first) {
            family = i;
        }
    }
    return family;
}

/// One operation: clear, tell the shader which one, draw over everything, read back, compare.
static void ModeCase(GpuContext *gpu, const DkShader *vertex_shader,
                     const DkShader *fragment_shader, DkGpuAddr uniform, IntMathMode mode) {
    const char *const name = IntMathName(mode);

    GpuClearUint(gpu, SENTINEL, SENTINEL, SENTINEL, SENTINEL);

    GpuVertex vertices[6];
    FullScreenQuad(vertices);

    GpuBeginPass(gpu);

    const IntParams params = { (uint32_t)mode, UNIFORM_PROBE, { 0u, 0u } };
    // Written through the command list rather than by the processor: the graphics unit caches
    // uniform data, and a push goes down the same path as the draw that reads it, so it cannot
    // be overtaken by it.
    dkCmdBufPushConstants(gpu->cmdbuf, uniform, UNIFORM_SIZE, 0, sizeof(params), &params);
    dkCmdBufBindUniformBuffer(gpu->cmdbuf, DkStage_Fragment, 0, uniform, UNIFORM_SIZE);

    const bool drawn = GpuDrawTriangles(gpu, vertex_shader, fragment_shader, vertices, 6);
    GpuSubmit(gpu);

    if (!drawn) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the draw was not recorded");
        return;
    }

    const uint8_t *const pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   gpu->failure[0] != '\0' ? gpu->failure
                                            : "the readback handed back nothing");
        return;
    }

    Picture *const picture = PictureBegin(TARGET_WIDTH, TARGET_HEIGHT, PICTURE_BITS, pixels);

    char detail[384];
    const bool matched = CheckEveryPixel(pixels, mode, picture, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, CHECK_EXACT, matched, "%s", detail);
}

void RunIntmath(void) {
    GpuSpec spec;
    GpuSpecDefaults(&spec, TARGET_WIDTH, TARGET_HEIGHT);
    // R32_Uint, which is the whole reason this suite can claim the exact tier. A normalised
    // format would put a conversion between the shader and memory and there would be nothing
    // left to compare bit for bit.
    spec.color_format = DkImageFormat_R32_Uint;
    spec.code_size = 64u * 1024u;
    spec.data_size = 64u * 1024u;

    GpuContext gpu;
    const bool opened = GpuOpenSpec(&gpu, &spec);
    CHECK_TRUE(SUITE, "graphics device opens with an integer target", CHECK_NONE, opened, "%s",
               gpu.failure);
    if (!opened) {
        return;
    }

    DkShader vertex_shader;
    DkShader fragment_shaders[FAMILY_COUNT];
    bool loaded = GpuLoadShader(&gpu, &vertex_shader, ShaderData(SHADER_COMMON_VERT),
                                ShaderSize(SHADER_COMMON_VERT));
    for (size_t i = 0; i < FAMILY_COUNT && loaded; ++i) {
        loaded = GpuLoadShader(&gpu, &fragment_shaders[i], ShaderData(FAMILIES[i].shader),
                               ShaderSize(FAMILIES[i].shader));
    }
    CHECK_TRUE(SUITE, "the integer shaders load", CHECK_NONE, loaded, "%s", gpu.failure);
    if (!loaded) {
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

    ReportNote("intmath target %ux%u R32_Uint, %u pixels per case, sentinel 0x%08x",
               TARGET_WIDTH, TARGET_HEIGHT, (unsigned)PIXEL_COUNT, (unsigned)SENTINEL);

    for (uint32_t mode = 0; mode < INTMATH_MODE_COUNT; ++mode) {
        ModeCase(&gpu, &vertex_shader, &fragment_shaders[FamilyOf((IntMathMode)mode)], uniform,
                 (IntMathMode)mode);
    }

    GpuClose(&gpu);
}
