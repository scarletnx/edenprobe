// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../report.h"
#include "../serviceref.h"
#include "../ui.h"
#include "tests.h"

// The service family's judge on answers made to order: reden's, and the ones a machine without
// reden's rule would give, each worked out by hand from the commit that sets the rule.

static const char *const SUITE = "reference";

static char said[480];

static void CheckCases(void) {
    // Every case under a suite the screens put in the service family, and no name twice.
    bool families = true;
    bool unique = true;
    for (uint32_t i = 0; i < SERVICE_CASES; ++i) {
        families = families && UiFamilyOf(ServiceCaseSuite((ServiceCaseId)i)) == UI_FAMILY_SERVICE;
        for (uint32_t j = 0; j < i; ++j) {
            unique = unique && strcmp(ServiceCaseName((ServiceCaseId)i),
                                      ServiceCaseName((ServiceCaseId)j)) != 0;
        }
    }
    CHECK_TRUE(SUITE, "service: every case is under a suite of the service family, once",
               CHECK_NONE, families && unique, "families %d, unique names %d", families, unique);
}

static void CheckOpus(void) {
    uint32_t results[SERVICE_OPUS_TRIES];
    // HwOpus is module 111 and the description 385: 111 + 385 * 512, worked out by hand.
    const bool code = SERVICE_OPUS_REFUSED == 0x3026Fu && SERVICE_OPUS_LIMIT == 24u &&
                      SERVICE_OPUS_TRIES == 26u;

    // reden: 24 open, the 25th refused with ResultOutOfOpusDecoders.
    memset(results, 0, sizeof(results));
    results[24] = SERVICE_OPUS_REFUSED;
    const bool reden = ServiceJudgeOpusLimit(results, 25u, said, sizeof(said)) &&
                       strcmp(said, "24 opened, decoder 25 refused with 0x3026f (HwOpus 385)") == 0;
    char reden_said[480];
    memcpy(reden_said, said, sizeof(said));

    // No limit: all 26 open.
    memset(results, 0, sizeof(results));
    const bool unlimited = !ServiceJudgeOpusLimit(results, 26u, said, sizeof(said)) &&
                           strstr(said, "all 26 opened: no limit") == said;
    // A limit, but another one: 25 open, or 3.
    results[25] = SERVICE_OPUS_REFUSED;
    const bool more = !ServiceJudgeOpusLimit(results, 26u, said, sizeof(said)) &&
                      strstr(said, "25 opened, then decoder 26 refused") == said;
    memset(results, 0, sizeof(results));
    results[3] = SERVICE_OPUS_REFUSED;
    const bool fewer = !ServiceJudgeOpusLimit(results, 4u, said, sizeof(said));
    // The 24 reden keeps, refused with another result: the session limit, say (Kernel 7).
    memset(results, 0, sizeof(results));
    results[24] = SERVICE_RESULT(1, 7);
    const bool other = !ServiceJudgeOpusLimit(results, 25u, said, sizeof(said)) &&
                       strstr(said, "refused with 0xe01, not 0x3026f") != NULL;
    CHECK_TRUE(SUITE, "service: hwopus passes 24 decoders and the 25th refused out of decoders",
               CHECK_NONE, code && reden && unlimited && more && fewer && other,
               "code %d, reden %d \"%.100s\", unlimited %d, more %d, fewer %d, other %d", code,
               reden, reden_said, unlimited, more, fewer, other);

    const bool freed = ServiceJudgeOpusFreed(0u, said, sizeof(said)) &&
                       !ServiceJudgeOpusFreed(SERVICE_OPUS_REFUSED, said, sizeof(said)) &&
                       strstr(said, "still refused with 0x3026f") != NULL;
    CHECK_TRUE(SUITE, "service: a closed decoder passes only when the next one opens", CHECK_NONE,
               freed, "\"%.120s\"", said);
}

void RunReferenceService(void) {
    CheckCases();
    CheckOpus();
}
