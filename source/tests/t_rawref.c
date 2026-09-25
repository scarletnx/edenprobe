// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>

#include "../check.h"
#include "../rawref.h"
#include "../report.h"
#include "tests.h"

// The raw method words the index and instance suites send, against words worked out by hand from
// deko3d's MakeCmdHeader (source/maxwell/command.h): method | subchannel << 13 | count << 16 |
// mode << 29, with the increasing mode 1 and the inline mode 4.

static const char *const SUITE = "reference";

void RunReferenceRaw(void) {
    // 0x5F4 | 0 << 13 | 2 << 16 | 1 << 29.
    CHECK_TRUE(SUITE, "the header that writes the two index limit words is 0x200205f4",
               CHECK_NONE,
               RawIncreasingHeader(RAW_SUBCHANNEL_3D, RAW_3D_INDEX_ARRAY_LIMIT, 2) ==
                   0x200205F4u,
               "got 0x%08x",
               (unsigned)RawIncreasingHeader(RAW_SUBCHANNEL_3D, RAW_3D_INDEX_ARRAY_LIMIT, 2));
    // 0x485 | 1 << 16 | 1 << 29.
    CHECK_TRUE(SUITE, "the header of one instanced array draw word is 0x20010485", CHECK_NONE,
               RawIncreasingHeader(RAW_SUBCHANNEL_3D, RAW_3D_VERTEX_ARRAY_INSTANCE_FIRST, 1) ==
                   0x20010485u,
               "got 0x%08x",
               (unsigned)RawIncreasingHeader(RAW_SUBCHANNEL_3D,
                                             RAW_3D_VERTEX_ARRAY_INSTANCE_FIRST, 1));
    // 0x044 | 0 << 16 | 4 << 29: an inline wait for idle, the word deko3d's barrier emits.
    CHECK_TRUE(SUITE, "an inline wait for idle is 0x80000044", CHECK_NONE,
               RawInlineHeader(RAW_SUBCHANNEL_3D, RAW_3D_WAIT_FOR_IDLE, 0) == 0x80000044u,
               "got 0x%08x", (unsigned)RawInlineHeader(RAW_SUBCHANNEL_3D, RAW_3D_WAIT_FOR_IDLE, 0));
    // Triangles (4) in bits 28-31, six vertices in 16-27, first vertex 6 in 0-15.
    CHECK_TRUE(SUITE, "six triangle list vertices from vertex 6 are the word 0x40060006",
               CHECK_NONE, RawInstanceArrayWord(4, 6, 6) == 0x40060006u, "got 0x%08x",
               (unsigned)RawInstanceArrayWord(4, 6, 6));
    uint32_t words[3] = { 0, 0, 0 };
    RawIndexLimit(words, 0x12345678ABull);
    CHECK_TRUE(SUITE, "an index limit goes high word first", CHECK_NONE,
               words[0] == 0x200205F4u && words[1] == 0x12u && words[2] == 0x345678ABu,
               "got 0x%08x 0x%08x 0x%08x", (unsigned)words[0], (unsigned)words[1],
               (unsigned)words[2]);
    // Six 16-bit indices from byte 0x1000 end at byte 0x100B; four more in front move it to
    // 0x1013.
    CHECK_TRUE(SUITE, "the tight limit of six 16-bit indices at 0x1000 is 0x100b", CHECK_NONE,
               RawTightIndexLimit(0x1000u, 0, 6, 2) == 0x100Bu &&
                   RawTightIndexLimit(0x1000u, 4, 6, 2) == 0x1013u,
               "got 0x%llx and 0x%llx",
               (unsigned long long)RawTightIndexLimit(0x1000u, 0, 6, 2),
               (unsigned long long)RawTightIndexLimit(0x1000u, 4, 6, 2));
}
