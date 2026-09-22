// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "tests.h"

// The first cases that put work on the graphics unit. A 64 by 64 target is cleared, every pixel
// is read back, and every pixel is compared to the one answer it can have.
//
// Exact tier, and it qualifies: a normalised target, constant clear values, and no shader
// between the two, so there is no float result to disagree about. The three whole target cases
// each differ from the one before in all four channels, which a stale readback cannot survive;
// only the fourth, which clears a rectangle, tells a correct readback from a scrambled one.

static const char *const SUITE = "clear";

// Small on purpose. 64 by 64 is four of the 64-byte-wide blocks across and eight down, which is
// enough for the fourth case to straddle block boundaries in both directions, and small enough
// that checking every one of the 4096 pixels costs nothing.
#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u

// The rectangle the fourth case clears, picked to be moved by every way a readback can go wrong
// while still handing back plausible-looking pixels: aligned to nothing, not square, off centre
// on both axes, and crossing a block boundary in each direction.
#define RECT_X 7u
#define RECT_Y 45u
#define RECT_WIDTH 23u
#define RECT_HEIGHT 11u

/// Compares every pixel against @p inside within the given rectangle and @p outside everywhere
/// else, and on a mismatch writes a sentence naming how many were wrong and what the first wrong
/// one held.
///
/// Every pixel, not a sample: a target where only the first block came out right is a real
/// failure mode of a swizzled copy, and it is exactly the one that checking a corner misses.
static bool PixelsMatch(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t rect_x,
                        uint32_t rect_y, uint32_t rect_width, uint32_t rect_height,
                        const uint8_t inside[4], const uint8_t outside[4], char *detail,
                        size_t detail_size) {
    uint32_t wrong = 0;
    uint32_t first_x = 0;
    uint32_t first_y = 0;
    bool first_inside = false;
    uint8_t first[4] = { 0, 0, 0, 0 };
    const uint8_t *first_wanted = outside;
    // A rectangle holding the same colour as everything around it is not a rectangle, and saying
    // a pixel was inside one would only puzzle whoever reads the failure.
    const bool bounded = memcmp(inside, outside, 4) != 0;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const bool in_rect = x >= rect_x && x < rect_x + rect_width && y >= rect_y &&
                                 y < rect_y + rect_height;
            const uint8_t *const wanted = in_rect ? inside : outside;
            const uint8_t *const pixel = GpuPixel(pixels, width, x, y);
            // Threshold zero, which is what the Exact tier means: the same comparison the
            // selfcheck suite pins down, asked for no tolerance at all.
            if (CheckChannel(pixel[0], wanted[0], 0) && CheckChannel(pixel[1], wanted[1], 0) &&
                CheckChannel(pixel[2], wanted[2], 0) && CheckChannel(pixel[3], wanted[3], 0)) {
                continue;
            }
            if (wrong == 0) {
                first_x = x;
                first_y = y;
                first_inside = in_rect;
                first_wanted = wanted;
                memcpy(first, pixel, sizeof(first));
            }
            ++wrong;
        }
    }

    if (wrong == 0) {
        detail[0] = '\0';
        return true;
    }

    const char *where = "";
    if (bounded) {
        where = first_inside ? " inside the rectangle" : " outside the rectangle";
    }
    snprintf(detail, detail_size,
             "%u of %u pixels wrong, first at (%u,%u)%s is %02x%02x%02x%02x, expected "
             "%02x%02x%02x%02x",
             (unsigned)wrong, (unsigned)(width * height), (unsigned)first_x, (unsigned)first_y,
             where, first[0], first[1], first[2], first[3], first_wanted[0], first_wanted[1],
             first_wanted[2], first_wanted[3]);
    return false;
}

/// Reads the target back and reports one case, against @p inside within the rectangle and
/// @p outside everywhere else.
static void ReadBackAndCheck(GpuContext *gpu, const char *name, uint32_t rect_x, uint32_t rect_y,
                             uint32_t rect_width, uint32_t rect_height, const uint8_t inside[4],
                             const uint8_t outside[4]) {
    const uint8_t *pixels = GpuReadBack(gpu);
    if (pixels == NULL) {
        // Either the context was already unusable or the queue faulted on the way here, and in
        // both cases `failure` is the only thing worth saying. The fallback is for the case that
        // has not happened yet, where something hands back nothing without leaving a reason.
        ReportCase(SUITE, name, CHECK_EXACT, false,
                   gpu->failure[0] != '\0' ? gpu->failure : "the readback handed back nothing");
        return;
    }

    char detail[256];
    const bool matched = PixelsMatch(pixels, gpu->width, gpu->height, rect_x, rect_y, rect_width,
                                     rect_height, inside, outside, detail, sizeof(detail));
    CHECK_TRUE(SUITE, name, CHECK_EXACT, matched, "%s", detail);
}

/// Clears the whole target to one colour, reads it back, and reports one case.
static void ClearAndCheck(GpuContext *gpu, const char *name, float red, float green, float blue,
                          float alpha, uint8_t red_byte, uint8_t green_byte, uint8_t blue_byte,
                          uint8_t alpha_byte) {
    const uint8_t expected[4] = { red_byte, green_byte, blue_byte, alpha_byte };

    GpuClear(gpu, red, green, blue, alpha);
    // A rectangle covering everything, with the same colour on both sides of it, so a uniform
    // clear and a rectangle clear are read the same way.
    ReadBackAndCheck(gpu, name, 0, 0, gpu->width, gpu->height, expected, expected);
}

void RunClear(void) {
    GpuContext gpu;
    const bool opened = GpuOpen(&gpu, TARGET_WIDTH, TARGET_HEIGHT);
    CHECK_TRUE(SUITE, "graphics device opens", CHECK_NONE, opened, "%s", gpu.failure);
    if (!opened) {
        // Every case below would report the same failure four more times and say nothing new.
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

    // The case with somewhere to be: the whole target, then a rectangle aligned to nothing, and
    // every pixel checked against whichever colour its position says. The answer depends on
    // where a pixel is, so a readback that puts the right bytes in the wrong places has nowhere
    // to hide, and both colours differ in all four channels from what the case before left.
    const uint8_t outside[4] = { 0x00, 0xFF, 0xFF, 0x00 };
    const uint8_t inside[4] = { 0xFF, 0x00, 0x00, 0xFF };
    GpuClear(&gpu, 0.0f, 1.0f, 1.0f, 0.0f);
    GpuClearRect(&gpu, RECT_X, RECT_Y, RECT_WIDTH, RECT_HEIGHT, 1.0f, 0.0f, 0.0f, 1.0f);
    ReadBackAndCheck(&gpu, "clear bounded by a rectangle", RECT_X, RECT_Y, RECT_WIDTH,
                     RECT_HEIGHT, inside, outside);

    GpuClose(&gpu);
}
