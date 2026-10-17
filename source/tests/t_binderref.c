// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../binderref.h"
#include "../check.h"
#include "../report.h"
#include "tests.h"

// The display family's judge on reply parcels made to order: the two layouts reden writes, with
// enable_buffer_history off and on, and replies neither explains. Each parcel is built word by
// word here: a header of four words, then the payload.

static const char *const SUITE = "reference";

static uint8_t raw[BINDER_REPLY_MAX];
static size_t used;
static char said[1400];
static uint32_t answers;

static void Word(uint32_t value) {
    for (uint32_t i = 0; i < 4u; ++i) {
        raw[used++] = (uint8_t)(value >> (8u * i));
    }
}

static void Begin(uint32_t payload_size) {
    memset(raw, 0, sizeof(raw));
    used = 0;
    Word(payload_size);
    Word(16u);
    Word(0u);
    Word(16u + payload_size);
}

/// reden's history: status, count, the entries newest first from @p newest down, status.
static void History(uint32_t count, uint64_t newest, int32_t step) {
    Begin(12u + count * BINDER_ENTRY_SIZE);
    Word(0u);
    Word(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t frame = newest - (uint64_t)((int64_t)step * (int64_t)i);
        Word((uint32_t)frame);
        Word((uint32_t)(frame >> 32));
        Word((uint32_t)(1000u * frame)); // queue time
        Word(0u);
        Word(0u); // presentation time
        Word(0u);
        Word(2u); // the state, queued
        Word(0u); // padding
    }
    Word(0u);
}

static bool Judge(BinderCaseId id) {
    return BinderJudge(id, raw, sizeof(raw), said, sizeof(said), &answers);
}

static void CheckLayouts(void) {
    // enable_buffer_history off: the status alone, every case.
    Begin(4u);
    Word(0u);
    const bool alone = Judge(BINDER_AFTER_THREE) && answers == 1u &&
                       strcmp(said, "the status alone (Q1 A), no frames to count or order; reply: "
                                    "payload 4 bytes at 16, objects 0 at 20: 00000000") == 0 &&
                       Judge(BINDER_NEWEST_FIRST) && Judge(BINDER_ZERO) && answers == 1u;
    char alone_said[1400];
    memcpy(alone_said, said, sizeof(said));

    // On: three entries after three frames, eight after ten, newest first.
    History(3u, 3u, 1);
    const bool three = Judge(BINDER_AFTER_THREE) && answers == 2u &&
                       strstr(said, "3 entries of 3 (Q1 B), frames 3 down to 1, newest first; "
                                    "reply: payload 108 bytes at 16, objects 0 at 124: 00000000 "
                                    "03000000 03000000") == said;
    History(8u, 10u, 1);
    const bool eight = Judge(BINDER_NEWEST_FIRST) && answers == 2u;
    History(8u, 3u, -1); // oldest first
    const bool backwards = !Judge(BINDER_NEWEST_FIRST) && strstr(said, "not newest first") != NULL;
    History(10u, 10u, 1); // more than asked for
    const bool many = !Judge(BINDER_NEWEST_FIRST) && strstr(said, "10 entries of 8") == said;
    History(2u, 3u, 1); // fewer than queued
    const bool few = !Judge(BINDER_AFTER_THREE);

    // A request of nought refused with BadValue and a count of nought, then the status.
    Begin(12u);
    Word((uint32_t)BINDER_BAD_VALUE);
    Word(0u);
    Word(0u);
    const bool refused = Judge(BINDER_ZERO) && answers == 2u &&
                         strstr(said, "refused with BadValue") == said && !Judge(BINDER_AFTER_THREE);
    CHECK_TRUE(SUITE, "binder: both of reden's layouts pass, and a history must be newest first",
               CHECK_NONE, alone && three && eight && backwards && many && few && refused,
               "alone %d \"%.120s\"; three %d, eight %d, backwards %d, many %d, few %d, refused %d",
               alone, alone_said, three, eight, backwards, many, few, refused);
}

static void CheckUnknown(void) {
    // BadValue alone, a layout neither of reden's: the line carries its bytes for Q1.
    Begin(4u);
    Word((uint32_t)BINDER_BAD_VALUE);
    const bool other = !Judge(BINDER_ZERO) && answers == 0u &&
                       strcmp(said, "a reply in neither layout known, the status alone or the "
                                    "history; reply: payload 4 bytes at 16, objects 0 at 20: "
                                    "eaffffff") == 0;
    char other_said[1400];
    memcpy(other_said, said, sizeof(said));
    // A header pointing past the reply.
    Begin(4u);
    Word(0u);
    raw[4] = 0xF0;
    raw[5] = 0x07;
    const bool outside = !Judge(BINDER_AFTER_THREE) &&
                         strstr(said, "a reply whose header points outside it") == said;
    // A history answered to a request of nought.
    History(1u, 1u, 1);
    const bool zero = !Judge(BINDER_ZERO) && strstr(said, "a history for a request of nought") == said;
    CHECK_TRUE(SUITE, "binder: a reply in no known layout fails with its bytes", CHECK_NONE,
               other && outside && zero, "other \"%.200s\"; outside %d, zero %d", other_said,
               outside, zero);
}

static void CheckCases(void) {
    const bool frames = BINDER_ZERO == 0 && BINDER_AFTER_THREE == 1 && BINDER_NEWEST_FIRST == 2 &&
                        BinderCaseFrames(BINDER_AFTER_THREE) == 3u &&
                        BinderCaseFrames(BINDER_NEWEST_FIRST) == 10u &&
                        BinderCaseFrames(BINDER_ZERO) == 0u &&
                        BinderCaseRequest(BINDER_AFTER_THREE) == 8 &&
                        BinderCaseRequest(BINDER_NEWEST_FIRST) == 8 &&
                        BinderCaseRequest(BINDER_ZERO) == 0;
    CHECK_TRUE(SUITE, "binder: the cases run with no frame, 3 and 10, asking for nought, 8 and 8",
               CHECK_NONE, frames, "the order, frames or requests differ from the handback's");
}

void RunReferenceBinder(void) {
    CheckLayouts();
    CheckUnknown();
    CheckCases();
}
