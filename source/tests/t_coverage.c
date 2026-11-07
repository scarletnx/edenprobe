// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../coverageref.h"
#include "../gpu.h"
#include "../rawref.h"
#include "../report.h"
#include "../shaders.h"
#include "tests.h"

// Multisample coverage, read through coverage to color (NV_fragment_coverage_to_color): with it
// on, the fragment's coverage, as an integer with bit i for sample i, takes the place of the red
// component the fragment shader wrote to an integer target, after the sample mask, alpha to
// coverage and the depth and stencil tests. With more raster samples than colour samples
// (NV_framebuffer_mixed_samples) a one sample R32_Uint target receives every raster sample's bit,
// so the mask a pixel holds says which of its samples a draw covered, with no multisample image to
// read back. The suite asks that of the standard positions, of positions the list sets, of the
// 2 by 2 grid four samples' positions can vary over, of the sample mask and of alpha to coverage;
// then coverage modulation, the colour scaled by the share of raster samples covered; a four
// sample depth image under the one sample target; and last coverage to color on a four sample
// target after a depth test, read back sample by sample.
//
// Every draw that cuts pixels is a rectangle from the left edge to a pixel's centre or a quarter
// into it. The positions are whole sixteenths and the edge falls between them (coverageref.c,
// whose reference cases hold every split here a sixteenth or more from its edge), so which
// samples a rectangle covers is exact, never a matter of the fill rule.

static const char *const SUITE = "coverage";

#define SIZE 16u
/// What the integer target is cleared to, and what the fragment shader writes to it: a pixel
/// holding CLEARED drew nothing, one holding WRITTEN drew without its coverage taking the value's
/// place.
#define CLEARED 0x0BADu
#define WRITTEN 0xDEAD0000u
/// The clear of the colour targets in the alpha to coverage and modulation cases.
#define COLOUR_CLEAR 0x11u

typedef struct {
    float colour[4];
    float colour2[4];
    uint32_t sentinel[4];
} Params;

typedef struct {
    GpuContext gpu;
    DkShader vertex;
    DkShader mask;
    DkShader alpha;
    DkShader modulate;
    DkShader fetch;
    DkImage colour;  ///< RGBA8, the alpha cases' first target and the modulation cases' first
    DkImage colour2; ///< RGBA8, the modulation cases' second target
    DkImageLayout layouts[6];
    DkImage ms_mask;  ///< four sample R32_Uint
    DkImage ms_depth; ///< four sample ZF32
    DkImage fetched;  ///< 16 by 16 R32_Uint: sample s of pixel (x, y) at (4 x + s, y), x < 4
    DkGpuAddr image_set;
    DkGpuAddr sampler_set;
} CoverageContext;

/// A rectangle over rows [y0, y1) from the left edge to @p edge sixteenths of a pixel.
static void Rect(GpuVertex out[6], uint32_t y0, uint32_t y1, uint32_t edge, float z) {
    const float right = -1.0f + (float)edge / (8.0f * (float)SIZE);
    const float top = 1.0f - 2.0f * (float)y0 / (float)SIZE;
    const float bottom = 1.0f - 2.0f * (float)y1 / (float)SIZE;
    const float corners[6][2] = { { -1.0f, bottom }, { right, bottom }, { right, top },
                                  { -1.0f, bottom }, { right, top },    { -1.0f, top } };
    for (uint32_t i = 0; i < 6u; ++i) {
        out[i] = (GpuVertex){ corners[i][0], corners[i][1], z, 0, 0, 1, 1, 1, 1 };
    }
}

/// Copies @p params into scratch and binds them as the fragment stage's uniform buffer 0.
static bool BindParams(CoverageContext *c, const Params *params) {
    DkGpuAddr addr = 0;
    void *const block = GpuAllocData(&c->gpu, DK_UNIFORM_BUF_ALIGNMENT, DK_UNIFORM_BUF_ALIGNMENT,
                                     &addr);
    if (block == NULL) {
        return false;
    }
    memset(block, 0, DK_UNIFORM_BUF_ALIGNMENT);
    memcpy(block, params, sizeof(*params));
    dkCmdBufBindUniformBuffer(c->gpu.cmdbuf, DkStage_Fragment, 0, addr, DK_UNIFORM_BUF_ALIGNMENT);
    return true;
}

static Params Plain(float alpha) {
    Params p;
    memset(&p, 0, sizeof(p));
    for (uint32_t i = 0; i < 3u; ++i) {
        p.colour[i] = COVERAGE_COLOUR_A[i] / 255.0f;
        p.colour2[i] = COVERAGE_COLOUR_B[i] / 255.0f;
    }
    p.colour[3] = alpha;
    p.colour2[3] = COVERAGE_COLOUR_B[3] / 255.0f;
    p.sentinel[0] = WRITTEN;
    return p;
}

/// The multisample state of a one sample target with @p raster raster samples at @p words, and
/// coverage to color into target @p output when @p to_color.
static void BindRaster(CoverageContext *c, DkMsMode colour, DkMsMode raster,
                       const uint32_t words[4], bool to_color, uint32_t output) {
    DkMultisampleState ms;
    dkMultisampleStateDefaults(&ms);
    ms.mode = colour;
    ms.rasterizerMode = raster;
    memcpy(ms.sampleLocations, words, sizeof(ms.sampleLocations));
    ms.coverageToColorEnable = to_color;
    ms.coverageToColorOutput = output;
    dkCmdBufBindMultisampleState(c->gpu.cmdbuf, &ms);
}

/// Reads the main R32_Uint target back as words.
static const uint32_t *ReadMasks(CoverageContext *c, bool recorded, const char *name) {
    GpuSubmit(&c->gpu);
    const uint8_t *const bytes = recorded && c->gpu.ready ? GpuReadBack(&c->gpu) : NULL;
    if (bytes == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure
                                             : "the list or the readback did not happen");
    }
    return (const uint32_t *)bytes;
}

/// Judges @p got, SIZE by SIZE words, against @p want for the pixels @p judged says are this
/// case's (NULL for all of them).
static void JudgeMasks(const char *name, const uint32_t *got, const uint32_t *want,
                       bool (*judged)(uint32_t x, uint32_t y)) {
    uint32_t wrong = 0;
    uint32_t count = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        if (judged != NULL && !judged(i % SIZE, i / SIZE)) {
            continue;
        }
        ++count;
        if (got[i] != want[i] && wrong++ == 0u) {
            first = i;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u,
               "%u of %u pixels wrong; (%u, %u) holds 0x%x, wanted 0x%x (0x%x: nothing drawn, "
               "0x%x: drawn without its coverage)",
               (unsigned)wrong, (unsigned)count, (unsigned)(first % SIZE), (unsigned)(first / SIZE),
               (unsigned)got[first], (unsigned)want[first], CLEARED, WRITTEN);
}

/// One coverage to color list: the main target cleared, then rectangles over rows [0, 8) to
/// @p edge_top and [8, 16) to @p edge_bottom with @p raster samples at @p words under
/// @p sample_mask; the expected masks worked out from the same words.
static void ColorCase(CoverageContext *c, const char *name, DkMsMode raster, uint32_t samples,
                      const uint32_t words[4], uint32_t sample_mask, uint32_t edge_top,
                      uint32_t edge_bottom, bool (*judged)(uint32_t x, uint32_t y)) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    GpuBeginPass(&c->gpu);
    dkCmdBufClearColorUint(c->gpu.cmdbuf, 0, DkColorMask_R, CLEARED, 0, 0, 0);
    BindRaster(c, DkMsMode_1x, raster, words, true, 0);
    dkCmdBufSetSampleMask(c->gpu.cmdbuf, sample_mask);
    const Params p = Plain(1.0f);
    GpuVertex quads[12];
    Rect(quads, 0, SIZE / 2u, edge_top, 0.5f);
    Rect(quads + 6, SIZE / 2u, SIZE, edge_bottom, 0.5f);
    const bool ok = BindParams(c, &p) && GpuDrawTriangles(&c->gpu, &c->vertex, &c->mask, quads, 12);
    const uint32_t *const got = ReadMasks(c, ok, name);
    if (got == NULL) {
        return;
    }
    uint32_t want[SIZE * SIZE];
    for (uint32_t y = 0; y < SIZE; ++y) {
        const uint32_t edge = y < SIZE / 2u ? edge_top : edge_bottom;
        for (uint32_t x = 0; x < SIZE; ++x) {
            const uint32_t mask = CoverageLeftOf(words, samples, x, y, edge) & sample_mask;
            want[y * SIZE + x] = mask != 0u ? mask : CLEARED;
        }
    }
    JudgeMasks(name, got, want, judged);
}

static bool CutColumns(uint32_t x, uint32_t y) {
    (void)y;
    return x == 8u || x == 9u;
}

static bool WholeColumns(uint32_t x, uint32_t y) {
    (void)y;
    return x != 8u && x != 9u;
}

/// Binds the RGBA8 target and the R32_Uint one as targets 0 and 1, and clears them.
static void BindAlphaTargets(CoverageContext *c) {
    DkImageView colour;
    DkImageView mask;
    dkImageViewDefaults(&colour, &c->colour);
    dkImageViewDefaults(&mask, &c->gpu.image);
    const DkImageView *const targets[2] = { &colour, &mask };
    dkCmdBufBindRenderTargets(c->gpu.cmdbuf, targets, 2, NULL);
    const float clear = COLOUR_CLEAR / 255.0f;
    dkCmdBufClearColorFloat(c->gpu.cmdbuf, 0, DkColorMask_RGBA, clear, clear, clear, clear);
    dkCmdBufClearColorUint(c->gpu.cmdbuf, 1, DkColorMask_R, CLEARED, 0, 0, 0);
}

/// One alpha to coverage list over the whole target at @p alpha with the standard four samples,
/// @p dither and @p sample_mask; with @p to_one the raw MULTISAMPLE_CONTROL word turns alpha to
/// one on too, which the library has no call for. Leaves the masks in @p masks and target 0's
/// alpha bytes in @p alphas; false when the list did not run.
static bool AlphaList(CoverageContext *c, float alpha, bool dither, uint32_t sample_mask,
                      bool to_one, uint32_t masks[SIZE * SIZE], uint8_t alphas[SIZE * SIZE]) {
    GpuBeginPass(&c->gpu);
    BindAlphaTargets(c);
    DkMultisampleState ms;
    dkMultisampleStateDefaults(&ms);
    ms.mode = DkMsMode_1x;
    ms.rasterizerMode = DkMsMode_4x;
    memcpy(ms.sampleLocations, COVERAGE_STANDARD_4X, sizeof(ms.sampleLocations));
    ms.alphaToCoverageEnable = 1;
    ms.alphaToCoverageDither = dither;
    ms.coverageToColorEnable = 1;
    ms.coverageToColorOutput = 1;
    dkCmdBufBindMultisampleState(c->gpu.cmdbuf, &ms);
    dkCmdBufSetSampleMask(c->gpu.cmdbuf, sample_mask);
    bool ok = true;
    if (to_one) {
        const uint32_t word = RawInlineHeader(RAW_SUBCHANNEL_3D, RAW_3D_MULTISAMPLE_CONTROL,
                                              RAW_MULTISAMPLE_ALPHA_TO_COVERAGE |
                                                  RAW_MULTISAMPLE_ALPHA_TO_ONE);
        ok = GpuRawMethods(&c->gpu, &word, 1);
    }
    const Params p = Plain(alpha);
    GpuVertex quad[6];
    Rect(quad, 0, SIZE, SIZE * 16u, 0.5f);
    ok = ok && BindParams(c, &p) && GpuDrawTriangles(&c->gpu, &c->vertex, &c->alpha, quad, 6);
    GpuSubmit(&c->gpu);
    if (!ok || !c->gpu.ready) {
        return false;
    }
    const uint8_t *const colour = GpuReadBackImage(&c->gpu, &c->colour, SIZE, SIZE);
    if (colour == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        alphas[i] = colour[4u * i + 3u];
    }
    const uint8_t *const words = GpuReadBack(&c->gpu);
    if (words == NULL) {
        return false;
    }
    memcpy(masks, words, SIZE * SIZE * 4u);
    return true;
}

static void AlphaCase(CoverageContext *c, const char *name, float alpha, uint32_t sample_mask,
                      uint32_t want_mask) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    uint32_t masks[SIZE * SIZE];
    uint8_t alphas[SIZE * SIZE];
    if (!AlphaList(c, alpha, false, sample_mask, false, masks, alphas)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "the list did not run");
        return;
    }
    uint32_t want[SIZE * SIZE];
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        want[i] = want_mask != 0u ? want_mask : CLEARED;
    }
    JudgeMasks(name, masks, want, NULL);
}

/// The masks alpha to coverage gives at sevenths of eighths of alpha, with and without the dither,
/// as notes: the specification fixes only nought and one, so what lies between is the machine's.
static void AlphaNotes(CoverageContext *c) {
    for (uint32_t dither = 0; dither < 2u; ++dither) {
        for (uint32_t k = 1; k < 8u; ++k) {
            uint32_t masks[SIZE * SIZE];
            uint8_t alphas[SIZE * SIZE];
            if (!AlphaList(c, (float)k / 8.0f, dither != 0u, 0xF, false, masks, alphas)) {
                ReportNote("coverage alpha %u/8 dither %s: the list did not run", (unsigned)k,
                           dither ? "on" : "off");
                continue;
            }
            // Slot 0 counts the pixels that drew nothing, 1 to 15 the masks, 16 the pixels that
            // drew without their coverage taking the shader's word, 17 any other word.
            uint32_t counts[18];
            uint32_t other = 0;
            memset(counts, 0, sizeof(counts));
            for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
                const uint32_t m = masks[i];
                const uint32_t slot = m == CLEARED ? 0u : m == WRITTEN ? 16u : m <= 15u ? m : 17u;
                if (slot == 17u && counts[17] == 0u) {
                    other = m;
                }
                counts[slot]++;
            }
            char line[256];
            int used = snprintf(line, sizeof(line), "coverage alpha %u/8 dither %s:", (unsigned)k,
                                dither ? "on" : "off");
            for (uint32_t m = 0; m <= 17u && used > 0 && (size_t)used < sizeof(line); ++m) {
                if (counts[m] == 0u) {
                    continue;
                }
                if (m == 0u) {
                    used += snprintf(line + used, sizeof(line) - (size_t)used,
                                     " 0x%x (nothing drawn) x%u", CLEARED, (unsigned)counts[m]);
                } else if (m == 16u) {
                    used += snprintf(line + used, sizeof(line) - (size_t)used,
                                     " 0x%x (no coverage written) x%u", WRITTEN,
                                     (unsigned)counts[m]);
                } else if (m == 17u) {
                    used += snprintf(line + used, sizeof(line) - (size_t)used,
                                     " other words, the first 0x%x, x%u", (unsigned)other,
                                     (unsigned)counts[m]);
                } else {
                    used += snprintf(line + used, sizeof(line) - (size_t)used, " 0x%x x%u",
                                     (unsigned)m, (unsigned)counts[m]);
                }
            }
            ReportNote("%s", line);
        }
    }
}

/// Alpha to one: the same draw at alpha one half twice, without and with it. Its coverage must be
/// the one the alpha before it gave, pixel by pixel, and the alpha written must be one. When one
/// half covers no sample or every sample nothing tells the two orders apart, and the case says so.
static void AlphaToOneCase(CoverageContext *c) {
    const char *const name = "alpha to one keeps the coverage the shader's alpha gave";
    if (!ReportStart(SUITE, name)) {
        return;
    }
    uint32_t before[SIZE * SIZE];
    uint32_t after[SIZE * SIZE];
    uint8_t alphas_before[SIZE * SIZE];
    uint8_t alphas[SIZE * SIZE];
    if (!AlphaList(c, 0.5f, false, 0xF, false, before, alphas_before) ||
        !AlphaList(c, 0.5f, false, 0xF, true, after, alphas)) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "a list did not run");
        return;
    }
    uint32_t telling = 0;
    uint32_t moved = 0;
    uint32_t not_one = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        telling += before[i] != CLEARED && before[i] != 0xFu;
        if (after[i] != before[i] && moved++ == 0u) {
            first = i;
        }
        not_one += after[i] != CLEARED && alphas[i] != 0xFFu;
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, telling == SIZE * SIZE && moved == 0u && not_one == 0u,
               "%u pixels where one half covered some samples and not all (wanted all %u); %u "
               "masks moved with alpha to one, the first (%u, %u) from 0x%x to 0x%x; %u alphas "
               "written not 0xff",
               (unsigned)telling, (unsigned)(SIZE * SIZE), (unsigned)moved,
               (unsigned)(first % SIZE), (unsigned)(first / SIZE), (unsigned)before[first],
               (unsigned)after[first], (unsigned)not_one);
}

/// What one modulated channel of @p code becomes over band @p band (sample masks 0x1, 0x3, 0x7
/// and 0xf, so one to four of four samples), through the table when @p table is set.
static uint8_t Modulated(uint8_t code, uint32_t band, bool table) {
    // The table the table case loads: a quarter reads entry 3, which holds one; a half entry 7,
    // a quarter; three quarters entry 11, a half; all entry 15, three quarters.
    static const uint32_t table_quarters[4] = { 4, 1, 2, 3 };
    const uint32_t quarters = table ? table_quarters[band] : band + 1u;
    uint8_t out = 0;
    CoverageScale(code, quarters, 4, &out);
    return out;
}

/// One modulation list: targets 0 and 1 (RGBA8, cleared to 0x11), four bands of four rows under
/// the sample masks 0x1, 0x3, 0x7 and 0xf with four raster samples, modulation @p mode, target 1
/// writing only red and alpha, the table loaded when @p table. Judges target @p judged against the
/// channels @p modulated says are scaled (bit 0 red to 3 alpha).
static void ModulationCase(CoverageContext *c, const char *name, DkCoverageModulation mode,
                           bool table, uint32_t judged, uint32_t modulated) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    GpuBeginPass(&c->gpu);
    DkImageView view0;
    DkImageView view1;
    dkImageViewDefaults(&view0, &c->colour);
    dkImageViewDefaults(&view1, &c->colour2);
    const DkImageView *const targets[2] = { &view0, &view1 };
    dkCmdBufBindRenderTargets(c->gpu.cmdbuf, targets, 2, NULL);
    const float clear = COLOUR_CLEAR / 255.0f;
    dkCmdBufClearColorFloat(c->gpu.cmdbuf, 0, DkColorMask_RGBA, clear, clear, clear, clear);
    dkCmdBufClearColorFloat(c->gpu.cmdbuf, 1, DkColorMask_RGBA, clear, clear, clear, clear);
    DkColorWriteState writes;
    dkColorWriteStateDefaults(&writes);
    dkColorWriteStateSetMask(&writes, 1, DkColorMask_R | DkColorMask_A);
    dkCmdBufBindColorWriteState(c->gpu.cmdbuf, &writes);
    DkMultisampleState ms;
    dkMultisampleStateDefaults(&ms);
    ms.mode = DkMsMode_1x;
    ms.rasterizerMode = DkMsMode_4x;
    memcpy(ms.sampleLocations, COVERAGE_STANDARD_4X, sizeof(ms.sampleLocations));
    ms.coverageModulation = mode;
    dkCmdBufBindMultisampleState(c->gpu.cmdbuf, &ms);
    if (table) {
        float values[16];
        for (uint32_t i = 0; i < 16u; ++i) {
            values[i] = (float)(i + 1u) / 16.0f;
        }
        values[3] = 1.0f;
        values[7] = 0.25f;
        values[11] = 0.5f;
        values[15] = 0.75f;
        dkCmdBufSetCoverageModulationTable(c->gpu.cmdbuf, values);
    }
    const Params p = Plain(COVERAGE_COLOUR_A[3] / 255.0f);
    bool ok = BindParams(c, &p);
    static const uint32_t masks[4] = { 0x1, 0x3, 0x7, 0xF };
    for (uint32_t band = 0; band < 4u && ok; ++band) {
        dkCmdBufSetSampleMask(c->gpu.cmdbuf, masks[band]);
        GpuVertex quad[6];
        Rect(quad, band * 4u, band * 4u + 4u, SIZE * 16u, 0.5f);
        ok = GpuDrawTriangles(&c->gpu, &c->vertex, &c->modulate, quad, 6);
    }
    if (table) {
        dkCmdBufSetCoverageModulationTable(c->gpu.cmdbuf, NULL);
    }
    GpuSubmit(&c->gpu);
    const uint8_t *const pixels =
        ok && c->gpu.ready ? GpuReadBackImage(&c->gpu, judged == 0u ? &c->colour : &c->colour2,
                                              SIZE, SIZE)
                           : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "the list did not run");
        return;
    }
    const uint8_t *const colour = judged == 0u ? COVERAGE_COLOUR_A : COVERAGE_COLOUR_B;
    const uint32_t writes_mask = judged == 0u ? 0xFu : 0x9u;
    uint32_t wrong = 0;
    uint32_t first = 0;
    uint8_t want_first[4] = { 0, 0, 0, 0 };
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        const uint32_t band = (i / SIZE) / 4u;
        uint8_t want[4];
        for (uint32_t ch = 0; ch < 4u; ++ch) {
            want[ch] = (writes_mask >> ch & 1u) == 0u ? (uint8_t)COLOUR_CLEAR
                       : (modulated >> ch & 1u) != 0u ? Modulated(colour[ch], band, table)
                                                      : colour[ch];
        }
        if (memcmp(pixels + 4u * i, want, 4) != 0 && wrong++ == 0u) {
            first = i;
            memcpy(want_first, want, 4);
        }
    }
    const uint8_t *const seen = pixels + 4u * first;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u,
               "%u of %u pixels wrong; row %u (%u of 4 samples) holds %02x%02x%02x%02x, wanted "
               "%02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)(SIZE * SIZE), (unsigned)(first / SIZE),
               (unsigned)((first / SIZE) / 4u + 1u), seen[0], seen[1], seen[2], seen[3],
               want_first[0], want_first[1], want_first[2], want_first[3]);
}

/// A pixel cut at its centre, modulated: rows of target 0 drawn to the centre of pixel 8 under
/// the standard four samples, which cover two of pixel 8's four.
static void ModulationEdgeCase(CoverageContext *c) {
    const char *const name = "a pixel cut at its centre is modulated by the two samples it covers";
    if (!ReportStart(SUITE, name)) {
        return;
    }
    GpuBeginPass(&c->gpu);
    DkImageView view0;
    dkImageViewDefaults(&view0, &c->colour);
    dkCmdBufBindRenderTarget(c->gpu.cmdbuf, &view0, NULL);
    const float clear = COLOUR_CLEAR / 255.0f;
    dkCmdBufClearColorFloat(c->gpu.cmdbuf, 0, DkColorMask_RGBA, clear, clear, clear, clear);
    DkMultisampleState ms;
    dkMultisampleStateDefaults(&ms);
    ms.mode = DkMsMode_1x;
    ms.rasterizerMode = DkMsMode_4x;
    memcpy(ms.sampleLocations, COVERAGE_STANDARD_4X, sizeof(ms.sampleLocations));
    ms.coverageModulation = DkCoverageModulation_Rgba;
    dkCmdBufBindMultisampleState(c->gpu.cmdbuf, &ms);
    const Params p = Plain(COVERAGE_COLOUR_A[3] / 255.0f);
    GpuVertex quad[6];
    Rect(quad, 0, SIZE, COVERAGE_EDGE_CENTRE_8, 0.5f);
    const bool ok =
        BindParams(c, &p) && GpuDrawTriangles(&c->gpu, &c->vertex, &c->modulate, quad, 6);
    GpuSubmit(&c->gpu);
    const uint8_t *const pixels =
        ok && c->gpu.ready ? GpuReadBackImage(&c->gpu, &c->colour, SIZE, SIZE) : NULL;
    if (pixels == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "the list did not run");
        return;
    }
    uint32_t wrong = 0;
    uint32_t first_i = 0;
    uint8_t want_first[4] = { 0, 0, 0, 0 };
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        const uint32_t x = i % SIZE;
        const uint32_t covered = CoverageCount(
            CoverageLeftOf(COVERAGE_STANDARD_4X, 4, x, i / SIZE, COVERAGE_EDGE_CENTRE_8));
        uint8_t want[4];
        for (uint32_t ch = 0; ch < 4u; ++ch) {
            if (covered == 0u) {
                want[ch] = (uint8_t)COLOUR_CLEAR;
            } else {
                CoverageScale(COVERAGE_COLOUR_A[ch], covered, 4, &want[ch]);
            }
        }
        if (memcmp(pixels + 4u * i, want, 4) != 0 && wrong++ == 0u) {
            first_i = i;
            memcpy(want_first, want, 4);
        }
    }
    const uint8_t *const seen = pixels + 4u * first_i;
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u,
               "%u of %u pixels wrong; (%u, %u) holds %02x%02x%02x%02x, wanted %02x%02x%02x%02x",
               (unsigned)wrong, (unsigned)(SIZE * SIZE), (unsigned)(first_i % SIZE),
               (unsigned)(first_i / SIZE), seen[0], seen[1], seen[2], seen[3], want_first[0],
               want_first[1], want_first[2], want_first[3]);
}

/// Copies the four sample R32_Uint image's left four columns out sample by sample into the 16 by
/// 16 one, through a texture fetch of each sample, and reads it back: a readback is never wider
/// than the main target, and the draws these cases judge cover every column alike.
static const uint32_t *FetchSamples(CoverageContext *c) {
    DkImageView view;
    dkImageViewDefaults(&view, &c->fetched);
    GpuSetTarget(&c->gpu, &c->fetched, SIZE, SIZE);
    DkMultisampleState ms;
    dkMultisampleStateDefaults(&ms);
    dkCmdBufBindMultisampleState(c->gpu.cmdbuf, &ms);
    dkCmdBufSetSampleMask(c->gpu.cmdbuf, ~0u);
    dkCmdBufBindImageDescriptorSet(c->gpu.cmdbuf, c->image_set, 1);
    dkCmdBufBindSamplerDescriptorSet(c->gpu.cmdbuf, c->sampler_set, 1);
    dkCmdBufBindTexture(c->gpu.cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(0, 0));
    DkDepthStencilState off;
    dkDepthStencilStateDefaults(&off);
    off.depthTestEnable = 0;
    off.depthWriteEnable = 0;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &off);
    static const GpuVertex full[6] = {
        { -1, -1, 0, 0, 0, 1, 1, 1, 1 }, { 1, -1, 0, 0, 0, 1, 1, 1, 1 },
        { 1, 1, 0, 0, 0, 1, 1, 1, 1 },   { -1, -1, 0, 0, 0, 1, 1, 1, 1 },
        { 1, 1, 0, 0, 0, 1, 1, 1, 1 },   { -1, 1, 0, 0, 0, 1, 1, 1, 1 },
    };
    const bool ok = GpuDrawTriangles(&c->gpu, &c->vertex, &c->fetch, full, 6);
    GpuSubmit(&c->gpu);
    if (!ok || !c->gpu.ready) {
        return NULL;
    }
    return (const uint32_t *)GpuReadBackImageBytes(&c->gpu, &c->fetched, SIZE, SIZE, 4);
}

/// Opens a list on the four sample targets, the integer one cleared to CLEARED and the depth to
/// one, with the four standard samples and no mixing.
static void BeginFourSamples(CoverageContext *c) {
    GpuBeginPass(&c->gpu);
    DkImageView mask;
    DkImageView depth;
    dkImageViewDefaults(&mask, &c->ms_mask);
    dkImageViewDefaults(&depth, &c->ms_depth);
    dkCmdBufBindRenderTarget(c->gpu.cmdbuf, &mask, &depth);
    dkCmdBufClearColorUint(c->gpu.cmdbuf, 0, DkColorMask_R, CLEARED, 0, 0, 0);
    DkDepthStencilState writing;
    dkDepthStencilStateDefaults(&writing);
    writing.depthTestEnable = 1;
    writing.depthWriteEnable = 1;
    writing.depthCompareOp = DkCompareOp_Always;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &writing);
    dkCmdBufClearDepthStencil(c->gpu.cmdbuf, true, 1.0f, 0, 0);
    BindRaster(c, DkMsMode_4x, DkMsMode_4x, COVERAGE_STANDARD_4X, false, 0);
}

/// Sample s of every pixel written s + 1, each by a draw under the sample mask 1 << s, then
/// fetched back: what the post-depth case's readback leans on.
static void FetchCase(CoverageContext *c) {
    const char *const name = "a four sample texture fetches the sample it names";
    if (!ReportStart(SUITE, name)) {
        return;
    }
    BeginFourSamples(c);
    DkDepthStencilState off;
    dkDepthStencilStateDefaults(&off);
    off.depthTestEnable = 0;
    off.depthWriteEnable = 0;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &off);
    bool ok = true;
    for (uint32_t s = 0; s < 4u && ok; ++s) {
        Params p = Plain(1.0f);
        p.sentinel[0] = s + 1u;
        dkCmdBufSetSampleMask(c->gpu.cmdbuf, 1u << s);
        GpuVertex quad[6];
        Rect(quad, 0, SIZE, SIZE * 16u, 0.5f);
        ok = BindParams(c, &p) && GpuDrawTriangles(&c->gpu, &c->vertex, &c->mask, quad, 6);
    }
    dkCmdBufBarrier(c->gpu.cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
    const uint32_t *const got = ok ? FetchSamples(c) : NULL;
    if (got == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "the lists did not run");
        return;
    }
    uint32_t wrong = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        if (got[i] != (i % 4u) + 1u && wrong++ == 0u) {
            first = i;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u,
               "%u of %u samples wrong; sample %u of (%u, %u) holds 0x%x, wanted %u",
               (unsigned)wrong, (unsigned)(SIZE * SIZE), (unsigned)(first % 4u),
               (unsigned)((first % SIZE) / 4u), (unsigned)(first / SIZE), (unsigned)got[first],
               (unsigned)(first % 4u) + 1u);
}

/// Coverage to color on the four sample target after a depth test: samples 0 and 2 nearer first
/// (depth 0.25 under the sample mask 0x5, no colour), then the whole target at 0.5 under LESS.
/// Samples 1 and 3 pass and each receives the coverage left after the test, 0xa; 0 and 2 keep
/// the clear.
static void PostDepthCase(CoverageContext *c) {
    const char *const name = "coverage to color writes each sample the coverage left after the "
                             "depth test";
    if (!ReportStart(SUITE, name)) {
        return;
    }
    BeginFourSamples(c);
    DkDepthStencilState first;
    dkDepthStencilStateDefaults(&first);
    first.depthTestEnable = 1;
    first.depthWriteEnable = 1;
    first.depthCompareOp = DkCompareOp_Less;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &first);
    DkColorWriteState none;
    dkColorWriteStateDefaults(&none);
    dkColorWriteStateSetMask(&none, 0, 0);
    dkCmdBufBindColorWriteState(c->gpu.cmdbuf, &none);
    dkCmdBufSetSampleMask(c->gpu.cmdbuf, 0x5);
    const Params p = Plain(1.0f);
    GpuVertex quad[6];
    Rect(quad, 0, SIZE, SIZE * 16u, 0.25f);
    bool ok = BindParams(c, &p) && GpuDrawTriangles(&c->gpu, &c->vertex, &c->mask, quad, 6);
    DkColorWriteState all;
    dkColorWriteStateDefaults(&all);
    dkCmdBufBindColorWriteState(c->gpu.cmdbuf, &all);
    DkDepthStencilState second = first;
    second.depthWriteEnable = 0;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &second);
    dkCmdBufSetSampleMask(c->gpu.cmdbuf, 0xF);
    BindRaster(c, DkMsMode_4x, DkMsMode_4x, COVERAGE_STANDARD_4X, true, 0);
    Rect(quad, 0, SIZE, SIZE * 16u, 0.5f);
    ok = ok && GpuDrawTriangles(&c->gpu, &c->vertex, &c->mask, quad, 6);
    dkCmdBufBarrier(c->gpu.cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
    const uint32_t *const got = ok ? FetchSamples(c) : NULL;
    if (got == NULL) {
        ReportCase(SUITE, name, CHECK_NONE, false,
                   c->gpu.failure[0] != '\0' ? c->gpu.failure : "the lists did not run");
        return;
    }
    uint32_t wrong = 0;
    uint32_t first_i = 0;
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        const uint32_t want = (i % 4u) % 2u != 0u ? 0xAu : CLEARED;
        if (got[i] != want && wrong++ == 0u) {
            first_i = i;
        }
    }
    CHECK_TRUE(SUITE, name, CHECK_EXACT, wrong == 0u,
               "%u of %u samples wrong; sample %u of (%u, %u) holds 0x%x, wanted 0x%x (0xf is the "
               "coverage before the depth test, 0x%x nothing drawn, 0x%x drawn without coverage)",
               (unsigned)wrong, (unsigned)(SIZE * SIZE), (unsigned)(first_i % 4u),
               (unsigned)((first_i % SIZE) / 4u), (unsigned)(first_i / SIZE),
               (unsigned)got[first_i], (first_i % 2u) != 0u ? 0xAu : CLEARED, CLEARED, WRITTEN);
}

/// Mixed samples with depth (NV_framebuffer_mixed_samples): the one sample R32_Uint target with
/// the four sample depth image, four raster samples. Samples 0 and 2 of the depth go nearer first
/// (0.25 under the sample mask 0x5, no colour), then coverage to color over the whole target at
/// 0.5 under LESS: every pixel's one colour sample receives the raster coverage the depth test
/// left, 0xa.
static void MixedDepthCase(CoverageContext *c) {
    const char *const name = "over a four sample depth image a one sample target receives the "
                             "coverage the depth test left";
    if (!ReportStart(SUITE, name)) {
        return;
    }
    GpuBeginPass(&c->gpu);
    DkImageView mask;
    DkImageView depth;
    dkImageViewDefaults(&mask, &c->gpu.image);
    dkImageViewDefaults(&depth, &c->ms_depth);
    dkCmdBufBindRenderTarget(c->gpu.cmdbuf, &mask, &depth);
    dkCmdBufClearColorUint(c->gpu.cmdbuf, 0, DkColorMask_R, CLEARED, 0, 0, 0);
    DkDepthStencilState state;
    dkDepthStencilStateDefaults(&state);
    state.depthTestEnable = 1;
    state.depthWriteEnable = 1;
    state.depthCompareOp = DkCompareOp_Always;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &state);
    dkCmdBufClearDepthStencil(c->gpu.cmdbuf, true, 1.0f, 0, 0);
    BindRaster(c, DkMsMode_1x, DkMsMode_4x, COVERAGE_STANDARD_4X, false, 0);
    state.depthCompareOp = DkCompareOp_Less;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &state);
    DkColorWriteState none;
    dkColorWriteStateDefaults(&none);
    dkColorWriteStateSetMask(&none, 0, 0);
    dkCmdBufBindColorWriteState(c->gpu.cmdbuf, &none);
    dkCmdBufSetSampleMask(c->gpu.cmdbuf, 0x5);
    const Params p = Plain(1.0f);
    GpuVertex quad[6];
    Rect(quad, 0, SIZE, SIZE * 16u, 0.25f);
    bool ok = BindParams(c, &p) && GpuDrawTriangles(&c->gpu, &c->vertex, &c->mask, quad, 6);
    DkColorWriteState all;
    dkColorWriteStateDefaults(&all);
    dkCmdBufBindColorWriteState(c->gpu.cmdbuf, &all);
    state.depthWriteEnable = 0;
    dkCmdBufBindDepthStencilState(c->gpu.cmdbuf, &state);
    dkCmdBufSetSampleMask(c->gpu.cmdbuf, 0xF);
    BindRaster(c, DkMsMode_1x, DkMsMode_4x, COVERAGE_STANDARD_4X, true, 0);
    Rect(quad, 0, SIZE, SIZE * 16u, 0.5f);
    ok = ok && GpuDrawTriangles(&c->gpu, &c->vertex, &c->mask, quad, 6);
    const uint32_t *const got = ReadMasks(c, ok, name);
    if (got == NULL) {
        return;
    }
    uint32_t want[SIZE * SIZE];
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) {
        want[i] = 0xAu;
    }
    JudgeMasks(name, got, want, NULL);
}

static bool Bring(CoverageContext *c) {
    bool ok = GpuCreateRenderImage(&c->gpu, &c->colour, &c->layouts[0], DkImageFormat_RGBA8_Unorm,
                                   SIZE, SIZE, DkMsMode_1x) &&
              GpuCreateRenderImage(&c->gpu, &c->colour2, &c->layouts[1],
                                   DkImageFormat_RGBA8_Unorm, SIZE, SIZE, DkMsMode_1x) &&
              GpuCreateRenderImage(&c->gpu, &c->ms_mask, &c->layouts[2], DkImageFormat_R32_Uint,
                                   SIZE, SIZE, DkMsMode_4x) &&
              GpuCreateRenderImage(&c->gpu, &c->ms_depth, &c->layouts[3], DkImageFormat_ZF32, SIZE,
                                   SIZE, DkMsMode_4x) &&
              GpuCreateRenderImage(&c->gpu, &c->fetched, &c->layouts[4], DkImageFormat_R32_Uint,
                                   SIZE, SIZE, DkMsMode_1x);
    DkImageDescriptor *const images = ok ? (DkImageDescriptor *)GpuAllocData(
                                               &c->gpu, (uint32_t)sizeof(DkImageDescriptor),
                                               DK_IMAGE_DESCRIPTOR_ALIGNMENT, &c->image_set)
                                         : NULL;
    DkSamplerDescriptor *const samplers =
        ok ? (DkSamplerDescriptor *)GpuAllocData(&c->gpu, (uint32_t)sizeof(DkSamplerDescriptor),
                                                 DK_SAMPLER_DESCRIPTOR_ALIGNMENT, &c->sampler_set)
           : NULL;
    if (images == NULL || samplers == NULL) {
        return false;
    }
    DkImageView view;
    dkImageViewDefaults(&view, &c->ms_mask);
    dkImageDescriptorInitialize(&images[0], &view, false, false);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    dkSamplerDescriptorInitialize(&samplers[0], &sampler);
    return true;
}

void RunCoverage(void) {
    if (!ReportStart(SUITE, "graphics device opens with its targets")) {
        return;
    }
    CoverageContext c;
    memset(&c, 0, sizeof(c));
    GpuSpec spec;
    GpuSpecDefaults(&spec, SIZE, SIZE);
    spec.color_format = DkImageFormat_R32_Uint;
    spec.code_size = 64u * 1024u;
    spec.data_size = 256u * 1024u;
    spec.image_size = 1024u * 1024u;
    const bool opened = GpuOpenSpec(&c.gpu, &spec) && Bring(&c);
    CHECK_TRUE(SUITE, "graphics device opens with its targets", CHECK_NONE, opened, "%s",
               c.gpu.failure[0] != '\0' ? c.gpu.failure : "no room for the descriptors");
    if (!opened) {
        GpuClose(&c.gpu);
        return;
    }
    if (!ReportStart(SUITE, "the shaders load")) {
        GpuClose(&c.gpu);
        return;
    }
    const bool loaded =
        GpuLoadShader(&c.gpu, &c.vertex, ShaderData(SHADER_COMMON_VERT),
                      ShaderSize(SHADER_COMMON_VERT)) &&
        GpuLoadShader(&c.gpu, &c.mask, ShaderData(SHADER_COVMASK_FRAG),
                      ShaderSize(SHADER_COVMASK_FRAG)) &&
        GpuLoadShader(&c.gpu, &c.alpha, ShaderData(SHADER_COVALPHA_FRAG),
                      ShaderSize(SHADER_COVALPHA_FRAG)) &&
        GpuLoadShader(&c.gpu, &c.modulate, ShaderData(SHADER_MODULATE_FRAG),
                      ShaderSize(SHADER_MODULATE_FRAG)) &&
        GpuLoadShader(&c.gpu, &c.fetch, ShaderData(SHADER_MSFETCH_FRAG),
                      ShaderSize(SHADER_MSFETCH_FRAG));
    CHECK_TRUE(SUITE, "the shaders load", CHECK_NONE, loaded, "%s", c.gpu.failure);
    if (!loaded) {
        GpuClose(&c.gpu);
        return;
    }

    ColorCase(&c, "four raster samples write 0xf where a pixel is covered", DkMsMode_4x, 4,
              COVERAGE_STANDARD_4X, 0xF, COVERAGE_EDGE_CENTRE_8, COVERAGE_EDGE_CENTRE_8,
              WholeColumns);
    ColorCase(&c, "the standard four samples split a pixel at its centre as 0x5", DkMsMode_4x, 4,
              COVERAGE_STANDARD_4X, 0xF, COVERAGE_EDGE_CENTRE_8, COVERAGE_EDGE_CENTRE_8,
              CutColumns);
    ColorCase(&c, "two raster samples write 0x3, and 0x2 where a pixel's centre cuts them",
              DkMsMode_2x, 2, COVERAGE_STANDARD_2X, 0x3, COVERAGE_EDGE_CENTRE_8,
              COVERAGE_EDGE_CENTRE_8, NULL);
    ColorCase(&c, "eight raster samples write 0xff, and 0x3a where a pixel's centre cuts them",
              DkMsMode_8x, 8, COVERAGE_STANDARD_8X, 0xFF, COVERAGE_EDGE_CENTRE_8,
              COVERAGE_EDGE_CENTRE_8, NULL);
    ColorCase(&c, "the sample mask is in the mask written", DkMsMode_4x, 4, COVERAGE_STANDARD_4X,
              0x6, COVERAGE_EDGE_CENTRE_8, COVERAGE_EDGE_CENTRE_8, NULL);
    uint32_t pairs[4];
    uint32_t grid[4];
    CoverageSuitePatterns(pairs, grid);
    ColorCase(&c, "positions the list sets split a pixel as 0x3 where the standard give 0x5",
              DkMsMode_4x, 4, pairs, 0xF, COVERAGE_EDGE_CENTRE_8, COVERAGE_EDGE_CENTRE_8, NULL);
    ColorCase(&c, "every sample at the centre leaves a pixel cut a quarter in unwritten",
              DkMsMode_4x, 4, COVERAGE_CENTRE, 0xF, COVERAGE_EDGE_QUARTER_8,
              COVERAGE_EDGE_QUARTER_8, NULL);
    ColorCase(&c, "odd columns of the 2 by 2 grid take their own positions", DkMsMode_4x, 4, grid,
              0xF, COVERAGE_EDGE_CENTRE_8, COVERAGE_EDGE_CENTRE_9, NULL);

    AlphaCase(&c, "alpha to coverage at nought covers no sample", 0.0f, 0xF, 0);
    AlphaCase(&c, "alpha to coverage at one covers every sample", 1.0f, 0xF, 0xF);
    AlphaCase(&c, "alpha to coverage at one keeps to the sample mask", 1.0f, 0x6, 0x6);
    AlphaToOneCase(&c);
    AlphaNotes(&c);

    ModulationCase(&c, "rgba modulation scales every channel by the share of samples covered",
                   DkCoverageModulation_Rgba, false, 0, 0xF);
    ModulationCase(&c, "rgb modulation leaves alpha whole", DkCoverageModulation_Rgb, false, 0,
                   0x7);
    ModulationCase(&c, "alpha modulation leaves the colour whole", DkCoverageModulation_Alpha,
                   false, 0, 0x8);
    ModulationCase(&c, "a second target is modulated and keeps its own write mask",
                   DkCoverageModulation_Rgba, false, 1, 0xF);
    ModulationCase(&c, "the modulation table takes the place of the share",
                   DkCoverageModulation_Rgba, true, 0, 0xF);
    ModulationEdgeCase(&c);

    MixedDepthCase(&c);
    FetchCase(&c);
    PostDepthCase(&c);
    GpuClose(&c.gpu);
}
