// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../settle.h"
#include "tests.h"

// The wait a device close makes for its last copy. It decides no verdict, so these are support
// cases and the none tier: what they pin is when the wait ends and that a machine which never
// had to wait gets no new line in its report.

static const char *const SUITE = "support";

static void CheckArrival(void) {
    uint8_t slot[8];
    memset(slot, 0x11, sizeof(slot));
    SettleFill(slot, 4, SETTLE_PRESET);
    const bool filled = slot[0] == SETTLE_PRESET && slot[3] == SETTLE_PRESET && slot[4] == 0x11;
    CHECK_TRUE(SUITE, "a readback slot is filled with the preset and nothing past it", CHECK_NONE,
               filled, "bytes %02x %02x %02x", slot[0], slot[3], slot[4]);

    const bool waiting = !SettleArrived(slot, 4, SETTLE_PRESET);
    slot[3] = 0x00;
    const bool last_byte = SettleArrived(slot, 4, SETTLE_PRESET);
    CHECK_TRUE(SUITE, "a slot has arrived once any of its bytes is not the preset", CHECK_NONE,
               waiting && last_byte, "untouched read as %s, one byte changed read as %s",
               waiting ? "waiting" : "arrived", last_byte ? "arrived" : "waiting");

    CHECK_TRUE(SUITE, "a slot of no bytes has nothing to wait for", CHECK_NONE,
               SettleArrived(slot, 0, SETTLE_PRESET) && SettleArrived(NULL, 4, SETTLE_PRESET),
               "an empty slot kept a close waiting");

    // The copy is of a target cleared to raw zeros, which every colour format stores as zero
    // bytes: a preset of zero would read as waiting forever on a machine that had delivered.
    CHECK_TRUE(SUITE, "the preset is not the zero a cleared pixel holds", CHECK_NONE,
               SETTLE_PRESET != 0u, "preset %02x", (unsigned)SETTLE_PRESET);
}

static void CheckNote(void) {
    SettleStats stats;
    memset(&stats, 0, sizeof(stats));
    char text[256];
    SettleRecord(&stats, 0, true);
    SettleRecord(&stats, 0, true);
    const bool quiet = !SettleDescribe(text, sizeof(text), &stats) && text[0] == '\0';
    CHECK_TRUE(SUITE, "a run whose copies all landed by the idle says nothing", CHECK_NONE,
               quiet && stats.closes == 2u, "%u closes, note \"%s\"", (unsigned)stats.closes,
               text);

    SettleRecord(&stats, 3000000u, true);
    SettleRecord(&stats, 11200000u, true);
    const char *const late =
        "readbacks: 2 of 4 device closes waited for a copy the machine delivered after its "
        "queue was idle, the longest 12 ms";
    const bool said = SettleDescribe(text, sizeof(text), &stats);
    CHECK_TRUE(SUITE, "a late copy is counted, with the longest wait rounded up", CHECK_NONE,
               said && strcmp(text, late) == 0, "note \"%s\"", text);

    SettleRecord(&stats, SETTLE_BUDGET_NS + 500000u, false);
    const char *const lost =
        "readbacks: 3 of 5 device closes waited for a copy the machine delivered after its "
        "queue was idle, the longest 251 ms; 1 never arrived within 250 ms";
    SettleDescribe(text, sizeof(text), &stats);
    CHECK_TRUE(SUITE, "a copy that never arrived is counted apart", CHECK_NONE,
               strcmp(text, lost) == 0 && stats.lost == 1u && stats.late == 2u, "note \"%s\"",
               text);

    char small[16];
    SettleDescribe(small, sizeof(small), &stats);
    CHECK_TRUE(SUITE, "the note is cut to its buffer, never past it", CHECK_NONE,
               strlen(small) == sizeof(small) - 1u && strncmp(small, lost, sizeof(small) - 1u) == 0,
               "\"%s\"", small);
}

void RunSupportSettle(void) {
    CheckArrival();
    CheckNote();
}
