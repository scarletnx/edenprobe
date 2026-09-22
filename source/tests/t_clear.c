// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "tests.h"

// The first cases that put work on the graphics unit. A 64 by 64 target is cleared to a colour,
// every pixel is read back, and every pixel is compared to the one answer the colour can have.
//
// Exact tier, and it qualifies: a normalised target, a constant clear value, and no shader
// between the two, so there is no float result to disagree about. The clear happens three times
// with colours that differ in all four channels, which is what makes a stale readback, or a
// channel quietly not written, impossible to mistake for a correct answer.

static const char *const SUITE = "clear";

// Small on purpose. 4096 pixels is enough to cover several of the 512-byte blocks a swizzled
// target is stored in, which is where a readback that mishandles the layout goes wrong, and
// small enough that checking every one of them costs nothing.
#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u

/// Compares every pixel against @p expected, and on a mismatch writes a sentence naming how many
/// were wrong and what the first wrong one held.
///
/// Every pixel, not a sample: a target where only the first block came out right is a real
/// failure mode of a swizzled copy, and it is exactly the one that checking a corner misses.
static bool EveryPixelIs(const uint8_t *pixels, uint32_t width, uint32_t height,
                         const uint8_t expected[4], char *detail, size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    uint8_t first[4] = { 0, 0, 0, 0 };

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *pixel = GpuPixel(pixels, width, x, y);
            // Threshold zero, which is what the Exact tier means: the same comparison the
            // selfcheck suite pins down, asked for no tolerance at all.
            if (CheckChannel(pixel[0], expected[0], 0) &&
                CheckChannel(pixel[1], expected[1], 0) &&
                CheckChannel(pixel[2], expected[2], 0) &&
                CheckChannel(pixel[3], expected[3], 0)) {
                continue;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                memcpy(first, pixel, sizeof(first));
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        detail[0] = '\0';
        return true;
    }

    snprintf(detail, detail_size,
             "%u of %u pixels wrong, first at (%u,%u) is %02x%02x%02x%02x, expected "
             "%02x%02x%02x%02x",
             (unsigned)wrong, (unsigned)(width * height), (unsigned)first_x, (unsigned)first_y,
             first[0], first[1], first[2], first[3], expected[0], expected[1], expected[2],
             expected[3]);
    return false;
}

/// Clears to one colour, reads the target back, and reports one case.
static void ClearAndCheck(GpuContext *gpu, const char *name, float red, float green, float blue,
                          float alpha, uint8_t red_byte, uint8_t green_byte, uint8_t blue_byte,
                          uint8_t alpha_byte) {
    const uint8_t expected[4] = { red_byte, green_byte, blue_byte, alpha_byte };

    GpuClear(gpu, red, green, blue, alpha);
    const uint8_t *pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        ReportCase(SUITE, name, false, "the readback handed back nothing");
        return;
    }

    char detail[256];
    const bool matched =
        EveryPixelIs(pixels, gpu->width, gpu->height, expected, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, matched, "%s", detail);
}

void RunClear(void) {
    GpuContext gpu;
    const bool opened = GpuOpen(&gpu, TARGET_WIDTH, TARGET_HEIGHT);
    CHECK_TRUE(SUITE, "graphics device opens", opened, "%s", gpu.failure);
    if (!opened) {
        // Every case below would report the same failure three more times and say nothing new.
        return;
    }

    ReportNote("clear target %ux%u RGBA8_Unorm, %u pixels checked per case", TARGET_WIDTH,
               TARGET_HEIGHT, TARGET_WIDTH * TARGET_HEIGHT);

    // Nought and one, which every implementation converts the same way because there is nothing
    // to round: they are the two ends of the unorm range.
    ClearAndCheck(&gpu, "clear to opaque red", 1.0f, 0.0f, 0.0f, 1.0f, 0xFF, 0x00, 0x00, 0xFF);
    ClearAndCheck(&gpu, "clear to transparent cyan", 0.0f, 1.0f, 1.0f, 0.0f, 0x00, 0xFF, 0xFF,
                  0x00);

    // The one value between the ends this case may claim exactly: 128/255 scaled back up lands
    // almost half a step from the nearest halfway point, so rounding to nearest and truncating
    // both give 128. Another mid value would have to earn that before it could be exact.
    ClearAndCheck(&gpu, "clear to a mid channel value", 128.0f / 255.0f, 0.0f, 0.0f, 1.0f, 0x80,
                  0x00, 0x00, 0xFF);

    GpuClose(&gpu);
}
