// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include "../check.h"
#include "../coverageref.h"
#include "../report.h"
#include "tests.h"

// The coverage suite's expected answers, against answers worked out by hand from the patterns'
// bytes: deko3d's standard four (0xEAA26E26: bytes 26, 6E, A2, EA) puts the samples at (6,2),
// (14,6), (2,10) and (10,14) sixteenths, so a rectangle to a pixel's centre (8 sixteenths in)
// covers samples 0 and 2.

static const char *const SUITE = "reference";

void RunReferenceCoverage(void) {
    const uint8_t s0 = CoverageLocation(COVERAGE_STANDARD_4X, 4, 0, 0, 0);
    const uint8_t s1 = CoverageLocation(COVERAGE_STANDARD_4X, 4, 0, 0, 1);
    const uint8_t s2 = CoverageLocation(COVERAGE_STANDARD_4X, 4, 0, 0, 2);
    const uint8_t s3 = CoverageLocation(COVERAGE_STANDARD_4X, 4, 0, 0, 3);
    CHECK_TRUE(SUITE, "the standard four samples sit at (6,2) (14,6) (2,10) (10,14) sixteenths",
               CHECK_NONE, s0 == 0x26u && s1 == 0x6Eu && s2 == 0xA2u && s3 == 0xEAu,
               "got %02x %02x %02x %02x", s0, s1, s2, s3);

    struct {
        const char *name;
        const uint32_t *words;
        uint32_t samples;
        uint32_t px;
        uint32_t edge;
        uint32_t want;
    } splits[] = {
        { "a rectangle to a pixel's centre covers 0x5 of the standard four", COVERAGE_STANDARD_4X,
          4, 8, COVERAGE_EDGE_CENTRE_8, 0x5u },
        { "a rectangle to a pixel's centre covers 0x2 of the standard two", COVERAGE_STANDARD_2X, 2,
          8, COVERAGE_EDGE_CENTRE_8, 0x2u },
        { "a rectangle to a pixel's centre covers 0x3a of the standard eight", COVERAGE_STANDARD_8X,
          8, 8, COVERAGE_EDGE_CENTRE_8, 0x3Au },
        { "a rectangle past a pixel covers every one of eight", COVERAGE_STANDARD_8X, 8, 7,
          COVERAGE_EDGE_CENTRE_8, 0xFFu },
        { "every sample at the centre leaves a pixel cut a quarter in uncovered", COVERAGE_CENTRE,
          4, 8, COVERAGE_EDGE_QUARTER_8, 0x0u },
        { "the standard four put sample 2 inside a pixel cut a quarter in", COVERAGE_STANDARD_4X,
          4, 8, COVERAGE_EDGE_QUARTER_8, 0x4u },
    };
    for (size_t i = 0; i < sizeof(splits) / sizeof(splits[0]); ++i) {
        const uint32_t got =
            CoverageLeftOf(splits[i].words, splits[i].samples, splits[i].px, 0, splits[i].edge);
        const uint32_t margin =
            CoverageEdgeMargin(splits[i].words, splits[i].samples, splits[i].px, 0, splits[i].edge);
        CHECK_TRUE(SUITE, splits[i].name, CHECK_NONE, got == splits[i].want && margin >= 1u,
                   "mask 0x%x, wanted 0x%x; nearest sample %u sixteenths from the edge",
                   (unsigned)got, (unsigned)splits[i].want, (unsigned)margin);
    }

    uint32_t pairs[4];
    uint32_t grid[4];
    CoverageSuitePatterns(pairs, grid);
    const uint32_t pair_split = CoverageLeftOf(pairs, 4, 8, 0, COVERAGE_EDGE_CENTRE_8);
    CHECK_TRUE(SUITE, "the coverage suite's own four split a pixel at its centre as 0x3",
               CHECK_NONE,
               pair_split == 0x3u &&
                   CoverageEdgeMargin(pairs, 4, 8, 0, COVERAGE_EDGE_CENTRE_8) >= 6u,
               "mask 0x%x, words %08x %08x %08x %08x", (unsigned)pair_split, (unsigned)pairs[0],
               (unsigned)pairs[1], (unsigned)pairs[2], (unsigned)pairs[3]);
    // Pixel 8 is an even column of the grid and pixel 9 an odd one, in both of its rows.
    const uint32_t even0 = CoverageLeftOf(grid, 4, 8, 0, COVERAGE_EDGE_CENTRE_8);
    const uint32_t even1 = CoverageLeftOf(grid, 4, 8, 1, COVERAGE_EDGE_CENTRE_8);
    const uint32_t odd0 = CoverageLeftOf(grid, 4, 9, 0, COVERAGE_EDGE_CENTRE_9);
    const uint32_t odd1 = CoverageLeftOf(grid, 4, 9, 1, COVERAGE_EDGE_CENTRE_9);
    CHECK_TRUE(SUITE,
               "the grid pattern splits even columns as 0x3 and odd ones as 0xc in both rows",
               CHECK_NONE, even0 == 0x3u && even1 == 0x3u && odd0 == 0xCu && odd1 == 0xCu,
               "even 0x%x 0x%x, odd 0x%x 0x%x", (unsigned)even0, (unsigned)even1, (unsigned)odd0,
               (unsigned)odd1);

    const uint32_t entries[4] = { CoverageTableEntry(1, 4), CoverageTableEntry(2, 4),
                                  CoverageTableEntry(3, 4), CoverageTableEntry(4, 4) };
    CHECK_TRUE(SUITE, "a quarter, a half, three quarters and all read table entries 3, 7, 11, 15",
               CHECK_NONE,
               entries[0] == 3u && entries[1] == 7u && entries[2] == 11u && entries[3] == 15u,
               "got %u %u %u %u", (unsigned)entries[0], (unsigned)entries[1],
               (unsigned)entries[2], (unsigned)entries[3]);

    uint32_t unsafe = 0;
    uint32_t first = 0;
    for (uint32_t c = 0; c < 8u; ++c) {
        const uint8_t code = c < 4u ? COVERAGE_COLOUR_A[c] : COVERAGE_COLOUR_B[c - 4u];
        for (uint32_t k = 1; k <= 4u; ++k) {
            if (!CoverageScaleSafe(code, k, 4)) {
                if (unsafe++ == 0u) {
                    first = (uint32_t)code << 8 | k;
                }
            }
        }
    }
    CHECK_TRUE(SUITE, "every modulated channel is a whole byte with room under an eight bit factor",
               CHECK_NONE, unsafe == 0u, "%u products are not; the first is %u times %u quarters",
               (unsigned)unsafe, (unsigned)(first >> 8), (unsigned)(first & 0xFFu));
    uint8_t half = 0;
    const bool whole = CoverageScale(0xA0u, 1, 2, &half);
    const bool refused = !CoverageScale(0x81u, 1, 2, &half);
    CHECK_TRUE(SUITE, "half of 0xa0 is 0x50 and half of 0x81 is refused", CHECK_NONE,
               whole && half == 0x50u && refused, "0xa0 halved %s, 0x81 halved %s",
               whole ? "whole" : "refused", refused ? "refused" : "taken");
}
