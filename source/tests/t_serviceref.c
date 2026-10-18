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

static void CheckSaveSize(void) {
    // reden: 25 keeps the size it is asked for, 26 hands it back.
    ServiceSaveSize seen;
    memset(&seen, 0, sizeof(seen));
    seen.asked_normal = SERVICE_SAVE_STEP;
    seen.asked_journal = SERVICE_SAVE_STEP;
    seen.after_normal = SERVICE_SAVE_STEP;
    seen.after_journal = SERVICE_SAVE_STEP;
    const bool reden =
        ServiceJudgeSaveSize(&seen, said, sizeof(said)) &&
        strcmp(said, "the size asked for reads back: before 0x0 0/0; ExtendSaveData(0x100000, "
                     "0x100000) 0x0, required 0; after 0x0 0x100000/0x100000") == 0;
    char reden_said[480];
    memcpy(reden_said, said, sizeof(said));

    // 25 answering success and keeping nothing: 26 says what it said before.
    ServiceSaveSize kept = seen;
    kept.before_normal = 0x400000u;
    kept.before_journal = 0x200000u;
    kept.asked_normal = 0x500000u;
    kept.asked_journal = 0x300000u;
    kept.after_normal = 0x400000u;
    kept.after_journal = 0x200000u;
    const bool stale = !ServiceJudgeSaveSize(&kept, said, sizeof(said)) &&
                       strstr(said, "the size before ExtendSaveData reads back") == said;
    // Only the journal kept: still the wrong answer.
    kept.after_normal = 0x500000u;
    const bool half = !ServiceJudgeSaveSize(&kept, said, sizeof(said)) &&
                      strstr(said, "another size reads back") == said;
    // 25 refused, and 26 refused after it.
    ServiceSaveSize refused = seen;
    refused.extend_rc = SERVICE_RESULT(2, 1003);
    const bool extend = !ServiceJudgeSaveSize(&refused, said, sizeof(said)) &&
                        strstr(said, "ExtendSaveData refused") == said;
    refused.extend_rc = 0u;
    refused.after_rc = SERVICE_RESULT(2, 1002);
    const bool after = !ServiceJudgeSaveSize(&refused, said, sizeof(said)) &&
                       strstr(said, "GetSaveDataSize refused after it") == said;
    CHECK_TRUE(SUITE, "service: am passes a save size that reads back as ExtendSaveData asked",
               CHECK_NONE, reden && stale && half && extend && after,
               "reden %d \"%.160s\", stale %d, half %d, extend %d, after %d", reden, reden_said,
               stale, half, extend, after);
}

static void CheckTempEmpty(void) {
    // reden: the marker is gone, the open answers PathNotFound (2 1, worked out by hand: 0x202).
    const bool code = SERVICE_PATH_NOT_FOUND == 0x202u;
    const bool reden =
        ServiceJudgeTempEmpty(0x1234u, SERVICE_PATH_NOT_FOUND, 0u, said, sizeof(said)) &&
        strcmp(said, "the marker 0000000000001234 the launch before left is gone") == 0;
    // Never emptied: the marker opens, holding what was promised, or an older one.
    const bool kept = !ServiceJudgeTempEmpty(0x1234u, 0u, 0x1234u, said, sizeof(said)) &&
                      strcmp(said, "the marker the launch before left is still there, holding "
                                   "0000000000001234") == 0;
    const bool older = !ServiceJudgeTempEmpty(0x1234u, 0u, 0x99u, said, sizeof(said)) &&
                       strstr(said, ", not the one it said it left") != NULL;
    // Another refusal says nothing about the storage being empty.
    const bool other =
        !ServiceJudgeTempEmpty(0x1234u, SERVICE_RESULT(2, 1001), 0u, said, sizeof(said)) &&
        strstr(said, "could not be looked for: opening it answered 0x7d202") != NULL;
    CHECK_TRUE(SUITE, "service: fsp passes a temporary storage whose last marker is gone",
               CHECK_NONE, code && reden && kept && older && other,
               "code %d, reden %d, kept %d, older %d, other %d \"%.120s\"", code, reden, kept,
               older, other, said);
}

static void CheckSelectUser(void) {
    // reden, one user: both hand it back.
    ServiceSelectUser seen;
    memset(&seen, 0, sizeof(seen));
    seen.users = 1;
    seen.firmware19 = true;
    seen.uid51[0] = seen.uid52[0] = 0x0123456789abcdefull;
    seen.uid51[1] = seen.uid52[1] = 0xfedcba9876543210ull;
    const bool one = ServiceJudgeSelectUser(&seen, said, sizeof(said)) &&
                     strcmp(said, "52 answers what 51 answers: users 1; 51 0x0 "
                                  "0123456789abcdeffedcba9876543210, 52 0x0 "
                                  "0123456789abcdeffedcba9876543210; firmware at least 19.0.0") ==
                         0;
    char one_said[480];
    memcpy(one_said, said, sizeof(said));
    // reden, two users: both an id of nought.
    ServiceSelectUser two;
    memset(&two, 0, sizeof(two));
    two.users = 2;
    two.firmware19 = true;
    const bool none = ServiceJudgeSelectUser(&two, said, sizeof(said));
    // A machine without 52 that answers it as a stub: success, and nought for one user.
    ServiceSelectUser stub = seen;
    stub.uid52[0] = stub.uid52[1] = 0u;
    const bool stubbed = !ServiceJudgeSelectUser(&stub, said, sizeof(said)) &&
                         strstr(said, "52 answers another user than 51") == said;
    // Older than 19.0.0: 52 refused passes there, and fails on a machine that says it is 19.
    ServiceSelectUser refused = seen;
    refused.rc52 = SERVICE_RESULT(10, 420);
    refused.firmware19 = false;
    const bool old = ServiceJudgeSelectUser(&refused, said, sizeof(said)) &&
                     strstr(said, "52 refused, by a machine older than 19.0.0") == said;
    refused.firmware19 = true;
    const bool new_refused = !ServiceJudgeSelectUser(&refused, said, sizeof(said)) &&
                             strstr(said, "52 refused") == said;
    // 51 itself refused is never a pass.
    ServiceSelectUser deprecated = seen;
    deprecated.rc51 = SERVICE_RESULT(124, 0);
    deprecated.firmware19 = false;
    const bool first = !ServiceJudgeSelectUser(&deprecated, said, sizeof(said)) &&
                       strstr(said, "51 refused") == said;
    CHECK_TRUE(SUITE, "service: account passes 52 answering what 51 answers", CHECK_NONE,
               one && none && stubbed && old && new_refused && first,
               "one %d \"%.200s\", none %d, stubbed %d, old %d, new refused %d, 51 refused %d",
               one, one_said, none, stubbed, old, new_refused, first);
}

static void CheckLdnMonitor(void) {
    // reden: an object, and RegisterClient succeeds on it.
    ServiceLdnMonitor seen;
    memset(&seen, 0, sizeof(seen));
    seen.handle = 0xd0000u;
    seen.registered = true;
    seen.firmware18 = true;
    const bool reden =
        ServiceJudgeLdnMonitor(&seen, said, sizeof(said)) &&
        strcmp(said, "the monitor came and RegisterClient took this process: "
                     "CreateClientProcessMonitor 0x0, handle 0xd0000; RegisterClient 0x0; "
                     "firmware at least 18.0.0") == 0;
    char reden_said[480];
    memcpy(reden_said, said, sizeof(said));
    // Success with no object, as an unknown command answers: nothing to register with.
    ServiceLdnMonitor empty = seen;
    empty.handle = 0u;
    empty.registered = false;
    const bool none = !ServiceJudgeLdnMonitor(&empty, said, sizeof(said)) &&
                      strstr(said, "CreateClientProcessMonitor answered with no object") == said &&
                      strstr(said, "RegisterClient not asked") != NULL;
    // A handle of nought never passes, even said to have registered; nor does a monitor nobody
    // registered with.
    ServiceLdnMonitor nought = seen;
    nought.handle = 0u;
    const bool zero_handle = !ServiceJudgeLdnMonitor(&nought, said, sizeof(said));
    ServiceLdnMonitor unasked = seen;
    unasked.registered = false;
    const bool not_registered = !ServiceJudgeLdnMonitor(&unasked, said, sizeof(said));
    // A handle that is no object of the service's: the kernel refuses it (InvalidHandle).
    ServiceLdnMonitor stray = seen;
    stray.register_rc = SERVICE_RESULT(1, 114);
    const bool invalid = !ServiceJudgeLdnMonitor(&stray, said, sizeof(said)) &&
                         strstr(said, "RegisterClient refused") == said;
    // Refused: a pass only where the machine says it is older than 18.0.0.
    ServiceLdnMonitor refused = seen;
    refused.create_rc = SERVICE_RESULT(10, 421);
    refused.handle = 0u;
    refused.registered = false;
    const bool new_refused = !ServiceJudgeLdnMonitor(&refused, said, sizeof(said)) &&
                             strstr(said, "CreateClientProcessMonitor refused") == said;
    refused.firmware18 = false;
    const bool old = ServiceJudgeLdnMonitor(&refused, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: ldn passes a monitor that comes and takes this process",
               CHECK_NONE,
               reden && none && zero_handle && not_registered && invalid && new_refused && old,
               "reden %d \"%.200s\", no object %d, handle 0 %d, unregistered %d, invalid %d, "
               "refused at 18 %d, older %d",
               reden, reden_said, none, zero_handle, not_registered, invalid, new_refused, old);
}

void RunReferenceService(void) {
    CheckCases();
    CheckOpus();
    CheckSaveSize();
    CheckTempEmpty();
    CheckSelectUser();
    CheckLdnMonitor();
}
