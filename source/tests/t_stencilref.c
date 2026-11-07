// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include "../check.h"
#include "../report.h"
#include "../stencilref.h"
#include "tests.h"

// The stencil suite's expected answers, against answers worked out by hand: the compare against
// the three bands, each op at the values the op cases hold, the boundaries, and the two masks.

static const char *const SUITE = "reference";

void RunReferenceStencil(void) {
    // Which of the three bands (0x40, 0x80, 0xc0) each compare passes under 0x80, as bits 0-2.
    static const uint8_t want[9] = { 0, 0x0, 0x4, 0x2, 0x6, 0x1, 0x5, 0x3, 0x7 };
    uint32_t wrong = 0;
    uint32_t seen = 0;
    for (uint32_t op = STENCIL_NEVER; op <= STENCIL_ALWAYS; ++op) {
        uint32_t pattern = 0;
        for (uint32_t band = 0; band < 3u; ++band) {
            if (StencilPasses((StencilCompare)op, STENCIL_COMPARE_REF, STENCIL_COMPARE_BANDS[band],
                              0xFFu)) {
                pattern |= 1u << band;
            }
        }
        wrong += pattern != want[op];
        seen |= 1u << pattern;
    }
    CHECK_TRUE(SUITE, "the eight stencil compares pass the three bands in eight different ways",
               CHECK_NONE, wrong == 0u && seen == 0xFFu,
               "%u compares wrong, patterns seen 0x%02x", (unsigned)wrong, (unsigned)seen);

    // Every op on 0xa5 and on 0x5a under the reference 0x80, all bits writable.
    static const uint8_t on_failing[9] = { 0, 0xA5, 0x00, 0x80, 0xA6, 0xA4, 0x5A, 0xA6, 0xA4 };
    static const uint8_t on_passing[9] = { 0, 0x5A, 0x00, 0x80, 0x5B, 0x59, 0xA5, 0x5B, 0x59 };
    uint32_t op_wrong = 0;
    for (uint32_t op = STENCIL_KEEP; op <= STENCIL_DECR_WRAP; ++op) {
        op_wrong += StencilApply((StencilOp)op, STENCIL_OP_FAILING, STENCIL_OP_REF, 0xFFu) !=
                    on_failing[op];
        op_wrong += StencilApply((StencilOp)op, STENCIL_OP_PASSING, STENCIL_OP_REF, 0xFFu) !=
                    on_passing[op];
    }
    CHECK_TRUE(SUITE, "each stencil op leaves its own byte at 0xa5 and at 0x5a", CHECK_NONE,
               op_wrong == 0u && !StencilPasses(STENCIL_GREATER, STENCIL_OP_REF,
                                                STENCIL_OP_FAILING, 0xFFu) &&
                   StencilPasses(STENCIL_GREATER, STENCIL_OP_REF, STENCIL_OP_PASSING, 0xFFu),
               "%u results wrong, or GREATER against 0x80 does not fail 0xa5 and pass 0x5a",
               (unsigned)op_wrong);

    const uint8_t incr = StencilApply(STENCIL_INCR, 0xFF, 0, 0xFF);
    const uint8_t incr_wrap = StencilApply(STENCIL_INCR_WRAP, 0xFF, 0, 0xFF);
    const uint8_t decr = StencilApply(STENCIL_DECR, 0x00, 0, 0xFF);
    const uint8_t decr_wrap = StencilApply(STENCIL_DECR_WRAP, 0x00, 0, 0xFF);
    CHECK_TRUE(SUITE, "incr stops at 0xff and incr_wrap goes to nought; decr stops at nought and "
                      "decr_wrap goes to 0xff",
               CHECK_NONE, incr == 0xFFu && incr_wrap == 0u && decr == 0u && decr_wrap == 0xFFu,
               "got %02x %02x %02x %02x", incr, incr_wrap, decr, decr_wrap);

    const uint8_t masked = StencilApply(STENCIL_INVERT, 0x0F, 0, 0xF0);
    CHECK_TRUE(SUITE, "inverting 0x0f through the write mask 0xf0 gives 0xff", CHECK_NONE,
               masked == 0xFFu, "got %02x", masked);
    CHECK_TRUE(SUITE, "0x3a equals 0x5a under the compare mask 0x1f and not without it",
               CHECK_NONE,
               StencilPasses(STENCIL_EQUAL, 0x3A, 0x5A, 0x1F) &&
                   !StencilPasses(STENCIL_EQUAL, 0x3A, 0x5A, 0xFF),
               "the compare mask is not applied to both sides");
}
