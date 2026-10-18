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

/// reden's language table, the first few codes as the console spells them, little-endian.
static uint64_t Code(const char *text) {
    uint64_t code = 0;
    for (uint32_t i = 0; i < 8u && text[i] != '\0'; ++i) {
        code |= (uint64_t)(unsigned char)text[i] << (8u * i);
    }
    return code;
}

static void CheckLanguages(void) {
    static ServiceLanguages seen;
    memset(&seen, 0, sizeof(seen));
    static const char *const TABLE[] = { "ja", "en-US", "fr", "de", "it", "es" };
    seen.listed = 6;
    for (uint32_t i = 0; i < 6u; ++i) {
        seen.codes[i] = seen.made[i] = Code(TABLE[i]);
    }
    seen.language = Code("en-US");
    seen.past_rc = SERVICE_INVALID_LANGUAGE;
    seen.count = (int32_t)SERVICE_LANGUAGES_MAX; // reden counts what the list can hold
    char text[9];
    ServiceLanguageText(Code("zh-Hant"), text);
    const bool spelled = strcmp(text, "zh-Hant") == 0 && SERVICE_INVALID_LANGUAGE == 0x4E269u;

    const bool language = ServiceJudgeLanguage(&seen, said, sizeof(said)) &&
                          strcmp(said, "GetLanguageCode \"en-US\", code 1 of the 6 listed") == 0;
    ServiceLanguages other = seen;
    other.language = Code("pt-BR");
    const bool unlisted = !ServiceJudgeLanguage(&other, said, sizeof(said)) &&
                          strstr(said, "\"pt-BR\"") != NULL;
    other.language = 0u; // a stub's code of nought
    const bool zero = !ServiceJudgeLanguage(&other, said, sizeof(said));

    const bool made = ServiceJudgeMakeLanguage(&seen, said, sizeof(said)) &&
                      strcmp(said, "the 6 listed made right, index 6 refused with 0x4e269") == 0;
    other = seen;
    other.made[3] = Code("fr");
    const bool wrong = !ServiceJudgeMakeLanguage(&other, said, sizeof(said)) &&
                       strcmp(said, "index 3 made 0x0 \"fr\", where the list has \"de\"") == 0;
    other = seen;
    other.past_rc = 0u; // one past the table made into something
    const bool past = !ServiceJudgeMakeLanguage(&other, said, sizeof(said)) &&
                      strstr(said, "index 6 answered 0x0, not 0x4e269") != NULL;

    const bool room = ServiceJudgeLanguageCount(&seen, said, sizeof(said));
    other = seen;
    other.count = 5;
    const bool less = !ServiceJudgeLanguageCount(&other, said, sizeof(said)) &&
                      strstr(said, "less room than codes") == said;
    other.count = 6;
    const bool exact = ServiceJudgeLanguageCount(&other, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: set passes reden's language codes and fails what strays from them",
               CHECK_NONE,
               spelled && language && unlisted && zero && made && wrong && past && room && less &&
                   exact,
               "spelled %d, language %d, unlisted %d, zero %d, made %d, wrong %d, past %d, "
               "room %d, less %d, exact %d",
               spelled, language, unlisted, zero, made, wrong, past, room, less, exact);
}

static void CheckRandom(void) {
    uint8_t first[SERVICE_RANDOM_SIZE], second[SERVICE_RANDOM_SIZE];
    for (uint32_t i = 0; i < SERVICE_RANDOM_SIZE; ++i) {
        first[i] = (uint8_t)(i * 37u + 11u);
        second[i] = (uint8_t)(i * 91u + 5u);
    }
    const bool own =
        ServiceJudgeRandom(0u, 0u, first, second, said, sizeof(said)) &&
        strstr(said, "two buffers of their own: 0x0 and 0x0; first 0b30557a..") == said;
    const bool same = !ServiceJudgeRandom(0u, 0u, first, first, said, sizeof(said)) &&
                      strstr(said, "the same bytes twice") == said;
    uint8_t short_write[SERVICE_RANDOM_SIZE];
    memcpy(short_write, second, sizeof(short_write));
    memset(short_write + SERVICE_RANDOM_SIZE - 8u, SERVICE_RANDOM_FILL, 8u);
    const bool partial = !ServiceJudgeRandom(0u, 0u, first, short_write, said, sizeof(said)) &&
                         strstr(said, "the buffer's end not written") == said;
    uint8_t zero[SERVICE_RANDOM_SIZE];
    memset(zero, 0, sizeof(zero));
    const bool nought = !ServiceJudgeRandom(0u, 0u, zero, second, said, sizeof(said)) &&
                        strstr(said, "a buffer of nought") == said;
    const bool refused =
        !ServiceJudgeRandom(SERVICE_RESULT(26, 1), 0u, first, second, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: csrng passes two buffers written whole and apart", CHECK_NONE,
               own && same && partial && nought && refused,
               "own %d, same %d, partial %d, nought %d, refused %d", own, same, partial, nought,
               refused);
}

static void CheckNfpFilter(void) {
    // No device: both refuse alike (NFP, 115, 64: DeviceNotFound).
    const uint32_t not_found = SERVICE_RESULT(115, 64);
    const bool alike = ServiceJudgeNfpFilter(not_found, not_found, 0, true, said, sizeof(said)) &&
                       strcmp(said, "the same answer: 0 devices; StartDetection 0x8073, "
                                    "StartDetectionWithFilter 0x8073; firmware at least 23.0.0") ==
                           0;
    char alike_said[480];
    memcpy(alike_said, said, sizeof(said));
    // An unknown command's success where 3 refuses.
    const bool stub = !ServiceJudgeNfpFilter(not_found, 0u, 0, true, said, sizeof(said)) &&
                      strstr(said, "two answers") == said;
    // Older than 23.0.0: 25 refused passes, a success where 3 refuses does not.
    const bool old =
        ServiceJudgeNfpFilter(not_found, SERVICE_RESULT(10, 421), 0, false, said, sizeof(said));
    const bool old_stub = !ServiceJudgeNfpFilter(not_found, 0u, 0, false, said, sizeof(said));
    // On 23.0.0, 25 refused otherwise than 3 is two answers.
    const bool new_refused = !ServiceJudgeNfpFilter(not_found, SERVICE_RESULT(10, 421), 0, true,
                                                    said, sizeof(said)) &&
                             strstr(said, "two answers") == said;
    CHECK_TRUE(SUITE, "service: nfp passes 25 answering what 3 answers", CHECK_NONE,
               alike && stub && old && old_stub && new_refused,
               "alike %d \"%.160s\", stub %d, old %d, old stub %d, refused at 23 %d", alike,
               alike_said, stub, old, old_stub, new_refused);
}

static void CheckAocPast(void) {
    // AM is module 128 and the description 37: 128 + 37 * 512, worked out by hand.
    const bool code = SERVICE_AOC_PAST_END == 0x4A80u;
    // reden with no add-on content: the end (0) empty, one past it no record.
    ServiceAocList seen = { 0u, 0u, 0u, SERVICE_AOC_PAST_END };
    const bool reden =
        ServiceJudgeAocPast(&seen, said, sizeof(said)) &&
        strcmp(said, "the end empty, one past it no record: 0 add-on contents; from 0 0x0, 0 "
                     "listed; from 1 0x4a80 (no record is 0x4a80)") == 0;
    char reden_said[480];
    memcpy(reden_said, said, sizeof(said));
    // Three of them: the end is 3, one past it 4.
    seen = (ServiceAocList){ 3u, 0u, 0u, SERVICE_AOC_PAST_END };
    const bool three = ServiceJudgeAocPast(&seen, said, sizeof(said));
    // A stub: success and nothing listed past the end.
    seen = (ServiceAocList){ 0u, 0u, 0u, 0u };
    const bool stub = !ServiceJudgeAocPast(&seen, said, sizeof(said)) &&
                      strstr(said, "one past the end answered with success") == said;
    // reden before 54cd5fb8eb: ResultUnknown (0xFFFFFFFF in its log, any other result here).
    seen = (ServiceAocList){ 0u, 0u, 0u, 0xFFFFFFFFu };
    const bool unknown = !ServiceJudgeAocPast(&seen, said, sizeof(said)) &&
                         strstr(said, "one past the end refused otherwise") == said;
    // The end refused, or not empty.
    seen = (ServiceAocList){ 0u, SERVICE_AOC_PAST_END, 0u, SERVICE_AOC_PAST_END };
    const bool end_refused = !ServiceJudgeAocPast(&seen, said, sizeof(said)) &&
                             strstr(said, "the list's end refused") == said;
    seen = (ServiceAocList){ 2u, 0u, 1u, SERVICE_AOC_PAST_END };
    const bool end_listed = !ServiceJudgeAocPast(&seen, said, sizeof(said)) &&
                            strstr(said, "the list's end not empty") == said;
    CHECK_TRUE(SUITE, "service: aoc passes an empty end and no record past it", CHECK_NONE,
               code && reden && three && stub && unknown && end_refused && end_listed,
               "code %d, reden %d \"%.160s\", three %d, stub %d, unknown %d, end refused %d, "
               "end listed %d",
               code, reden, reden_said, three, stub, unknown, end_refused, end_listed);
}

static void CheckAocMore(void) {
    // reden with no add-on content: none counted, none listed from 0.
    ServiceAocNone none = { 0u, 0u, 0u };
    const bool none_reden =
        ServiceJudgeAocNone(&none, said, sizeof(said)) &&
        strcmp(said, "none counted, none listed: 0 add-on contents counted; from 0 0x0, 0 "
                     "listed") == 0;
    char none_said[480];
    memcpy(none_said, said, sizeof(said));
    // One counted with an empty list, the list refused, and one listed with none counted.
    none = (ServiceAocNone){ 1u, 0u, 0u };
    const bool counted = !ServiceJudgeAocNone(&none, said, sizeof(said)) &&
                         strstr(said, "add-on content counted") == said;
    none = (ServiceAocNone){ 0u, SERVICE_AOC_PAST_END, 0u };
    const bool none_refused = !ServiceJudgeAocNone(&none, said, sizeof(said)) &&
                              strstr(said, "the list from 0 refused") == said;
    none = (ServiceAocNone){ 0u, 0u, 1u };
    const bool listed = !ServiceJudgeAocNone(&none, said, sizeof(said)) &&
                        strstr(said, "add-on content listed") == said;

    // reden: both lists succeed with none, and nothing is written past the room.
    ServiceAocRoom room = { 0u, 0u, 0u, 0u, true };
    const bool room_reden =
        ServiceJudgeAocRoom(&room, said, sizeof(said)) &&
        strcmp(said, "none listed, nothing past the room: 1 asked into room for 1 0x0, 0 "
                     "listed, past it kept; 0 asked into room for 64 0x0, 0 listed") == 0;
    char room_said[480];
    memcpy(room_said, said, sizeof(said));
    // A list of one refused (with Ryujinx's InvalidBufferSize, as a stand-in), a list of none
    // refused, one listed into the room for one or for none asked, and a word written past the
    // room.
    room = (ServiceAocRoom){ 0x190A6u, 0u, 0u, 0u, true };
    const bool one_refused = !ServiceJudgeAocRoom(&room, said, sizeof(said)) &&
                             strstr(said, "a list of one refused") == said;
    room = (ServiceAocRoom){ 0u, 0u, 0xFFFFFFFFu, 0u, true };
    const bool zero_refused = !ServiceJudgeAocRoom(&room, said, sizeof(said)) &&
                              strstr(said, "a list of none refused") == said;
    room = (ServiceAocRoom){ 0u, 1u, 0u, 0u, true };
    const bool one_listed = !ServiceJudgeAocRoom(&room, said, sizeof(said)) &&
                            strstr(said, "something listed") == said;
    room = (ServiceAocRoom){ 0u, 0u, 0u, 1u, true };
    const bool zero_listed = !ServiceJudgeAocRoom(&room, said, sizeof(said)) &&
                             strstr(said, "something listed") == said;
    room = (ServiceAocRoom){ 0u, 0u, 0u, 0u, false };
    const bool past = !ServiceJudgeAocRoom(&room, said, sizeof(said)) &&
                      strstr(said, "written past the room") == said;

    // reden's program of id 0 gets 0x1000; an application's program 3 gets the application's
    // 0x1000; an id with bit 12 set is its own base and 0x1000.
    const bool base_reden =
        ServiceJudgeAocBase(0u, 0u, 0u, 0x1000u, said, sizeof(said)) &&
        strcmp(said, "the program's base with 0x1000 added: program 0x0000000000000000 "
                     "(svcGetInfo 0x0); GetAddOnContentBaseId 0x0 0x0000000000001000, its base "
                     "and 0x1000 0x0000000000001000") == 0;
    char base_said[480];
    memcpy(base_said, said, sizeof(said));
    const bool indexed = ServiceJudgeAocBase(0u, 0x0100ABCD00002003ull, 0u,
                                             0x0100ABCD00003000ull, said, sizeof(said));
    const bool high = ServiceJudgeAocBase(0u, 0x0100000000001000ull, 0u, 0x0100000000001000ull,
                                          said, sizeof(said)) &&
                      strstr(said, "the program's base with 0x1000 added") == said;
    // The program's own id given back, the program index kept, a refusal, and no program id.
    const bool own = !ServiceJudgeAocBase(0u, 0x0100ABCD00002000ull, 0u, 0x0100ABCD00002000ull,
                                          said, sizeof(said)) &&
                     strstr(said, "the program's own id") == said;
    const bool kept = !ServiceJudgeAocBase(0u, 0x0100ABCD00002003ull, 0u,
                                           0x0100ABCD00003003ull, said, sizeof(said)) &&
                      strstr(said, "another id") == said;
    const bool base_refused =
        !ServiceJudgeAocBase(0u, 0x0100ABCD00002000ull, 0xFFFFFFFFu, 0x0100ABCD00003000ull, said,
                             sizeof(said)) &&
        strstr(said, "refused") == said;
    // svcGetInfo's InvalidEnumValue, Kernel (1) 120, on a machine older than 3.0.0.
    const bool no_info = !ServiceJudgeAocBase(SERVICE_RESULT(1, 120), 0u, 0u, 0x1000u, said,
                                              sizeof(said)) &&
                         strstr(said, "no program id") == said;
    CHECK_TRUE(SUITE, "service: aoc passes none listed, lists that fit their room and the base id",
               CHECK_NONE,
               none_reden && counted && none_refused && listed && room_reden && one_refused &&
                   zero_refused && one_listed && zero_listed && past && base_reden && indexed &&
                   high && own && kept && base_refused && no_info,
               "none %d \"%.100s\", counted %d, refused %d, listed %d; room %d \"%.100s\", "
               "one refused %d, none refused %d, one listed %d %d, past %d; base %d "
               "\"%.100s\", indexed %d, high %d, own %d, kept %d, refused %d, no info %d",
               none_reden, none_said, counted, none_refused, listed, room_reden, room_said,
               one_refused, zero_refused, one_listed, zero_listed, past, base_reden, base_said,
               indexed, high, own, kept, base_refused, no_info);
}

static void CheckAocPrepare(void) {
    // reden's stub: success for any index.
    const bool reden = ServiceJudgeAocPrepare(0u, said, sizeof(said)) &&
                       strcmp(said, "prepared with success: PrepareAddOnContent(1) 0x0") == 0;
    char reden_said[480];
    memcpy(reden_said, said, sizeof(said));
    // A refusal, as no record past the list's end.
    const bool refused = !ServiceJudgeAocPrepare(SERVICE_AOC_PAST_END, said, sizeof(said)) &&
                         strstr(said, "refused") == said;
    CHECK_TRUE(SUITE, "service: aoc passes a prepare of a missing index answered with success",
               CHECK_NONE, reden && refused, "reden %d \"%.160s\", refused %d", reden, reden_said,
               refused);
}

static void CheckTime(void) {
    // reden: the context's offset past the steady clock, on its source.
    ServiceUserClock clock;
    memset(&clock, 0, sizeof(clock));
    clock.offset = 1700000000 - 5000;
    clock.context.seconds = 120;
    clock.context.source[0] = 0x5a;
    clock.steady = clock.context;
    clock.steady.seconds = 5000;
    clock.time = 1700000000;
    const bool user = ServiceJudgeUserClock(&clock, said, sizeof(said)) &&
                      strstr(said, "the offset past the steady clock") == said;
    char user_said[480];
    memcpy(user_said, said, sizeof(said));
    clock.time += 1;
    const bool tick = ServiceJudgeUserClock(&clock, said, sizeof(said));
    clock.time += 1;
    const bool late = !ServiceJudgeUserClock(&clock, said, sizeof(said)) &&
                      strstr(said, "another time") == said;
    clock.time -= 2;
    clock.context.source[0] = 0x5b;
    const bool other_source = !ServiceJudgeUserClock(&clock, said, sizeof(said)) &&
                              strstr(said, "the context is on another source") == said;
    // A stand-in: every answer nought.
    memset(&clock, 0, sizeof(clock));
    const bool nought = !ServiceJudgeUserClock(&clock, said, sizeof(said)) &&
                        strstr(said, "the steady clock's source is nought") == said;
    CHECK_TRUE(SUITE, "service: time passes a user clock its context's offset past steady",
               CHECK_NONE, user && tick && late && other_source && nought,
               "reden %d \"%.200s\", a second later %d, two %d, another source %d, nought %d",
               user, user_said, tick, late, other_source, nought);

    ServiceTimePoint first = { 5000, { 0x5a } }, second = { 5001, { 0x5a } };
    const bool one = ServiceJudgeSteadyClock(0u, &first, 0u, &second, said, sizeof(said));
    second.seconds = 5002;
    const bool two = ServiceJudgeSteadyClock(0u, &first, 0u, &second, said, sizeof(said));
    second.seconds = 5000;
    const bool still = !ServiceJudgeSteadyClock(0u, &first, 0u, &second, said, sizeof(said));
    // Nanoseconds where seconds go: a second and a half is 1500000000.
    second.seconds = 5000 + 1500000000;
    const bool nanos = !ServiceJudgeSteadyClock(0u, &first, 0u, &second, said, sizeof(said)) &&
                       strstr(said, "not a second's count") == said;
    second = (ServiceTimePoint){ 5001, { 0x5b } };
    const bool sources = !ServiceJudgeSteadyClock(0u, &first, 0u, &second, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: time passes a steady clock one or two seconds on", CHECK_NONE,
               one && two && still && nanos && sources,
               "one %d, two %d, still %d, nanoseconds %d, two sources %d", one, two, still, nanos,
               sources);

    // The dates, worked out by hand: 1970-01-01 was a Thursday; 1700000000 is 19675 days and
    // 80000 seconds, 2023-11-14 (a Tuesday, the 318th day) at 22:13:20.
    ServiceUtcDates dates;
    memset(&dates, 0, sizeof(dates));
    dates.date[0] = (ServiceCalendarTime){ 1970, 1, 1, 0, 0, 0, 0 };
    dates.info[0] = (ServiceCalendarInfo){ 4, 0, "UTC", 0, 0 };
    dates.date[1] = (ServiceCalendarTime){ 2023, 11, 14, 22, 13, 20, 0 };
    dates.info[1] = (ServiceCalendarInfo){ 2, 317, "UTC", 0, 0 };
    const bool reden = ServiceJudgeUtcDates(&dates, said, sizeof(said)) &&
                       strstr(said, "both dates right") == said;
    char dates_said[480];
    memcpy(dates_said, said, sizeof(said));
    dates.info[1].day_of_year = 318;
    const bool one_based = !ServiceJudgeUtcDates(&dates, said, sizeof(said));
    dates.info[1].day_of_year = 317;
    dates.info[0].day_of_week = 3;
    const bool weekday = !ServiceJudgeUtcDates(&dates, said, sizeof(said)) &&
                         strstr(said, "the epoch dated otherwise") == said;
    dates.info[0].day_of_week = 4;
    dates.info[1].utc_offset = -10800;
    const bool offset = !ServiceJudgeUtcDates(&dates, said, sizeof(said));
    memset(&dates, 0, sizeof(dates));
    const bool stub = !ServiceJudgeUtcDates(&dates, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: time passes the epoch and the known time dated under UTC",
               CHECK_NONE, reden && one_based && weekday && offset && stub,
               "reden %d \"%.220s\", a day of the year from one %d, another weekday %d, an "
               "offset %d, a stand-in %d",
               reden, dates_said, one_based, weekday, offset, stub);

    const bool back = ServiceJudgeUtcPosix(0u, 0u, 1, 1700000000, said, sizeof(said));
    const bool none = !ServiceJudgeUtcPosix(0u, 0u, 0, 0, said, sizeof(said)) &&
                      strstr(said, "not one time") == said;
    const bool shifted = !ServiceJudgeUtcPosix(0u, 0u, 1, 1700000000 + 10800, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: time passes ToPosixTime giving the known time back", CHECK_NONE,
               back && none && shifted, "back %d, none %d, shifted %d", back, none, shifted);

    ServiceMyRule mine;
    memset(&mine, 0, sizeof(mine));
    strcpy(mine.name, "America/Sao_Paulo");
    mine.time = 1700000000;
    mine.mine_date = mine.loaded_date = (ServiceCalendarTime){ 2023, 11, 14, 19, 13, 20, 0 };
    mine.mine_info = mine.loaded_info = (ServiceCalendarInfo){ 2, 317, "-03", 0, -10800 };
    const bool agree = ServiceJudgeMyRule(&mine, said, sizeof(said)) &&
                       strstr(said, "the same date under both") == said;
    mine.mine_info.utc_offset = 0;
    const bool disagree = !ServiceJudgeMyRule(&mine, said, sizeof(said)) &&
                          strstr(said, "two dates") == said;
    mine.mine_info.utc_offset = -10800;
    mine.name[0] = '\0';
    const bool unnamed = !ServiceJudgeMyRule(&mine, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: time passes the device's rule dating as the service's own",
               CHECK_NONE, agree && disagree && unnamed, "agree %d, disagree %d, unnamed %d", agree,
               disagree, unnamed);
}

/// Six fonts as reden lays them out: one after another, each behind its 8-byte header.
static void FontsAsReden(ServiceFont fonts[SERVICE_FONTS]) {
    memset(fonts, 0, sizeof(ServiceFont) * SERVICE_FONTS);
    uint32_t at = 0;
    for (uint32_t i = 0; i < SERVICE_FONTS; ++i) {
        ServiceFont *f = &fonts[i];
        f->state = 1;
        f->size = 0x1000u * (i + 1u);
        f->offset = at + 8u;
        // 0x18029a7f little-endian, and the size XORed with the key as reden stores it.
        const uint8_t header[8] = { 0x7f, 0x9a, 0x02, 0x18, 0, 0, 0, 0 };
        memcpy(f->header, header, sizeof(header));
        const uint32_t size = f->size ^ SERVICE_FONT_KEY;
        f->header[4] = (uint8_t)size;
        f->header[5] = (uint8_t)(size >> 8);
        f->header[6] = (uint8_t)(size >> 16);
        f->header[7] = (uint8_t)(size >> 24);
        const uint8_t start[4] = { 0x00, 0x01, 0x00, 0x00 };
        memcpy(f->start, start, sizeof(start));
        at = f->offset + f->size;
    }
}

static void CheckFonts(void) {
    static ServiceFont fonts[SERVICE_FONTS];
    FontsAsReden(fonts);
    const bool reden = ServiceJudgeFonts(fonts, said, sizeof(said)) &&
                       strstr(said, "six fonts, each inside behind its header") == said;
    char reden_said[480];
    memcpy(reden_said, said, sizeof(said));
    // The .bfttf's own magic, 0x36f81a1e big-endian, where the decrypted one goes.
    const uint8_t encrypted[4] = { 0x36, 0xf8, 0x1a, 0x1e };
    memcpy(fonts[2].header, encrypted, sizeof(encrypted));
    const bool magic = !ServiceJudgeFonts(fonts, said, sizeof(said)) &&
                       strstr(said, "font 2 behind another magic") == said;
    FontsAsReden(fonts);
    fonts[5].state = 0;
    const bool unloaded = !ServiceJudgeFonts(fonts, said, sizeof(said)) &&
                          strstr(said, "font 5 not loaded") == said;
    FontsAsReden(fonts);
    fonts[0].offset = 0;
    const bool no_header = !ServiceJudgeFonts(fonts, said, sizeof(said)) &&
                           strstr(said, "font 0 outside the memory") == said;
    FontsAsReden(fonts);
    fonts[1].size = SERVICE_FONT_MEMORY;
    const bool past = !ServiceJudgeFonts(fonts, said, sizeof(said));
    FontsAsReden(fonts);
    memset(fonts[3].start, 0xEE, sizeof(fonts[3].start));
    const bool garbage = !ServiceJudgeFonts(fonts, said, sizeof(said)) &&
                         strstr(said, "font 3 not a font") == said;
    // The size word as Ryujinx writes it, big-endian: on the line, not judged.
    FontsAsReden(fonts);
    const uint32_t size = fonts[4].size ^ SERVICE_FONT_KEY;
    fonts[4].header[4] = (uint8_t)(size >> 24);
    fonts[4].header[5] = (uint8_t)(size >> 16);
    fonts[4].header[6] = (uint8_t)(size >> 8);
    fonts[4].header[7] = (uint8_t)size;
    const bool ryujinx = ServiceJudgeFonts(fonts, said, sizeof(said)) &&
                         strstr(said, "be 0x5000 ") != NULL;
    // A stand-in: success and nought everywhere.
    memset(fonts, 0, sizeof(fonts));
    const bool stub = !ServiceJudgeFonts(fonts, said, sizeof(said)) &&
                      strstr(said, "font 0 not loaded") == said;
    CHECK_TRUE(SUITE, "service: pl passes six loaded fonts behind the decrypted magic", CHECK_NONE,
               reden && magic && unloaded && no_header && past && garbage && ryujinx && stub,
               "reden %d \"%.200s\", encrypted magic %d, unloaded %d, no room for a header %d, "
               "past the memory %d, not a font %d, the size big-endian %d, a stand-in %d",
               reden, reden_said, magic, unloaded, no_header, past, garbage, ryujinx, stub);

    FontsAsReden(fonts);
    ServiceFontOrder order;
    memset(&order, 0, sizeof(order));
    order.language = 0x53552d6e65ull; // "en-US"
    order.loaded = 1;
    order.count = SERVICE_FONTS;
    for (uint32_t i = 0; i < SERVICE_FONTS; ++i) {
        // Another order than the types', as a console's priority may give.
        const uint32_t type = (i + 3u) % SERVICE_FONTS;
        order.types[i] = type;
        order.offsets[i] = fonts[type].offset;
        order.sizes[i] = fonts[type].size;
    }
    const bool listed = ServiceJudgeFontOrder(&order, fonts, said, sizeof(said)) &&
                        strstr(said, "the six, as told: language 0x0 \"en-US\"") == said;
    char order_said[480];
    memcpy(order_said, said, sizeof(said));
    order.types[5] = order.types[0];
    const bool twice = !ServiceJudgeFontOrder(&order, fonts, said, sizeof(said)) &&
                       strstr(said, "not the six once each") == said;
    order.types[5] = (5u + 3u) % SERVICE_FONTS;
    order.offsets[1] += 4u;
    const bool moved = !ServiceJudgeFontOrder(&order, fonts, said, sizeof(said)) &&
                       strstr(said, "another offset or size") == said;
    order.offsets[1] -= 4u;
    order.loaded = 0;
    const bool unloaded_order = !ServiceJudgeFontOrder(&order, fonts, said, sizeof(said));
    memset(&order, 0, sizeof(order));
    const bool stub_order = !ServiceJudgeFontOrder(&order, fonts, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: pl passes the six in order of priority, each as told", CHECK_NONE,
               listed && twice && moved && unloaded_order && stub_order,
               "listed %d \"%.200s\", one twice %d, an offset moved %d, not loaded %d, a stand-in "
               "%d",
               listed, order_said, twice, moved, unloaded_order, stub_order);
}

static void CheckProfile(void) {
    const uint64_t asked[2] = { 0x1122334455667788ull, 0x99aabbccddeeff00ull };
    ServiceProfileBase get = { { asked[0], asked[1] }, 1700000000ull, "eden" };
    ServiceProfileBase base = get;
    const bool reden = ServiceJudgeProfileBase(asked, 0u, &get, 0u, &base, said, sizeof(said)) &&
                       strstr(said, "the user, by a name") == said;
    char base_said[480];
    memcpy(base_said, said, sizeof(said));
    base.edited += 1u;
    const bool two = !ServiceJudgeProfileBase(asked, 0u, &get, 0u, &base, said, sizeof(said)) &&
                     strstr(said, "two bases") == said;
    base = get;
    get.uid[1] ^= 1u;
    base.uid[1] ^= 1u;
    const bool other = !ServiceJudgeProfileBase(asked, 0u, &get, 0u, &base, said, sizeof(said));
    get = (ServiceProfileBase){ { asked[0], asked[1] }, 0u, "" };
    base = get;
    const bool unnamed = !ServiceJudgeProfileBase(asked, 0u, &get, 0u, &base, said, sizeof(said));
    memset(get.nickname, 'a', sizeof(get.nickname));
    base = get;
    const bool unended = !ServiceJudgeProfileBase(asked, 0u, &get, 0u, &base, said, sizeof(said));
    memset(&get, 0, sizeof(get));
    base = get;
    const bool stub = !ServiceJudgeProfileBase(asked, 0u, &get, 0u, &base, said, sizeof(said)) &&
                      strstr(said, "another user") == said;
    CHECK_TRUE(SUITE, "service: profile passes one base twice, of the user, by a name", CHECK_NONE,
               reden && two && other && unnamed && unended && stub,
               "reden %d \"%.160s\", two bases %d, another user %d, no name %d, a name with no "
               "end %d, a stand-in %d",
               reden, base_said, two, other, unnamed, unended, stub);

    // A JPEG of 256x256 as a frame header writes it: SOI, an APP0 of 16 bytes, SOF0 of 17, EOI.
    static uint8_t jpeg[64];
    const uint8_t made[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0,
                             0, 1, 0, 1, 0, 0, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x01, 0x00, 0x01,
                             0x00, 0x03, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1, 0xFF, 0xD9 };
    memcpy(jpeg, made, sizeof(made));
    const uint32_t n = (uint32_t)sizeof(made);
    const bool picture = ServiceJudgeProfileImage(0u, n, 0u, n, jpeg, said, sizeof(said)) &&
                         strstr(said, "a 256x256 JPEG: ") == said &&
                         strstr(said, "frame 256x256") != NULL;
    char picture_said[480];
    memcpy(picture_said, said, sizeof(said));
    const bool told = !ServiceJudgeProfileImage(0u, n + 4u, 0u, n, jpeg, said, sizeof(said)) &&
                      strstr(said, "a size the load does not keep") == said;
    jpeg[25] = 0x00; // 128 high
    jpeg[26] = 0x80;
    const bool smaller = !ServiceJudgeProfileImage(0u, n, 0u, n, jpeg, said, sizeof(said)) &&
                         strstr(said, "a JPEG of another size") == said;
    jpeg[25] = 0x01;
    jpeg[26] = 0x00;
    jpeg[n - 1u] = 0x00;
    const bool cut = !ServiceJudgeProfileImage(0u, n, 0u, n, jpeg, said, sizeof(said)) &&
                     strstr(said, "not a JPEG") == said;
    memset(jpeg, 0, sizeof(jpeg));
    const bool none = !ServiceJudgeProfileImage(0u, 0u, 0u, 0u, jpeg, said, sizeof(said)) &&
                      strstr(said, "no picture") == said;
    CHECK_TRUE(SUITE, "service: profile passes a 256x256 JPEG of the size told", CHECK_NONE,
               picture && told && smaller && cut && none,
               "picture %d \"%.160s\", another size told %d, 256x128 %d, no end %d, a stand-in %d",
               picture, picture_said, told, smaller, cut, none);

    const bool exists = ServiceJudgeUserExistence(0u, true, 0u, false, said, sizeof(said));
    const bool everyone = !ServiceJudgeUserExistence(0u, true, 0u, true, said, sizeof(said)) &&
                          strstr(said, "a made up user found") == said;
    const bool nobody = !ServiceJudgeUserExistence(0u, false, 0u, false, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: profile passes the user found and nobody made up", CHECK_NONE,
               exists && everyone && nobody, "exists %d, everyone %d, nobody %d", exists,
               everyone, nobody);
}

static void CheckAmState(void) {
    const bool handheld = ServiceJudgeOperationMode(0u, 0, 0u, 1280, 720, said, sizeof(said));
    const bool docked = ServiceJudgeOperationMode(0u, 1, 0u, 1920, 1080, said, sizeof(said));
    const bool crossed = !ServiceJudgeOperationMode(0u, 1, 0u, 1280, 720, said, sizeof(said)) &&
                         strstr(said, "a resolution of the other mode") == said;
    const bool stub = !ServiceJudgeOperationMode(0u, 0, 0u, 0, 0, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-state passes a mode with its own resolution", CHECK_NONE,
               handheld && docked && crossed && stub, "handheld %d, docked %d, crossed %d, a "
               "stand-in %d", handheld, docked, crossed, stub);

    const bool focus = ServiceJudgeFocus(0u, 1, said, sizeof(said)) &&
                       !ServiceJudgeFocus(0u, 0, said, sizeof(said)) &&
                       !ServiceJudgeFocus(0u, 2, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-state passes an application in focus", CHECK_NONE, focus,
               "\"%.120s\"", said);

    // AM is module 128: 128 + 3 * 512 and 128 + 2 * 512, worked out by hand.
    const bool codes = SERVICE_AM_NO_MESSAGES == 0x680u && SERVICE_AM_NO_DATA == 0x480u;
    ServiceLogoMessage logo;
    memset(&logo, 0, sizeof(logo));
    logo.firmware21 = true;
    logo.end_rc = SERVICE_AM_NO_MESSAGES;
    logo.count = 2;
    logo.messages[0] = 15; // FocusStateChanged, left from before
    logo.messages[1] = SERVICE_AM_LOGO_GONE;
    const bool reden = ServiceJudgeLogoMessage(&logo, said, sizeof(said)) &&
                       strstr(said, "message 95 came: 130 0x0; 2 received, then 0x680") == said;
    char logo_said[480];
    memcpy(logo_said, said, sizeof(said));
    logo.count = 1;
    const bool missing = !ServiceJudgeLogoMessage(&logo, said, sizeof(said)) &&
                         strstr(said, "130 succeeded and no message 95 came") == said;
    logo.firmware21 = false;
    const bool missing_old = !ServiceJudgeLogoMessage(&logo, said, sizeof(said));
    logo.enable_rc = SERVICE_RESULT(10, 221);
    const bool refused_old = ServiceJudgeLogoMessage(&logo, said, sizeof(said));
    logo.firmware21 = true;
    const bool refused_new = !ServiceJudgeLogoMessage(&logo, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-state passes message 95 after 130", CHECK_NONE,
               codes && reden && missing && missing_old && refused_old && refused_new,
               "codes %d, reden %d \"%.160s\", no 95 %d, no 95 before 21 %d, refused before 21 "
               "%d, refused at 21 %d",
               codes, reden, logo_said, missing, missing_old, refused_old, refused_new);

    ServiceVrMode vr = { 0u, 0u, 0u, 0u, 1, 0, true };
    const bool on_off = ServiceJudgeVrMode(&vr, said, sizeof(said));
    vr.on = 0;
    const bool never_on = !ServiceJudgeVrMode(&vr, said, sizeof(said)) &&
                          strstr(said, "never on") == said;
    vr.firmware19 = false;
    const bool stub_old = !ServiceJudgeVrMode(&vr, said, sizeof(said));
    vr.begin_rc = SERVICE_RESULT(10, 221);
    const bool refused_vr = ServiceJudgeVrMode(&vr, said, sizeof(said));
    vr = (ServiceVrMode){ 0u, 0u, 0u, 0u, 1, 1, true };
    const bool never_off = !ServiceJudgeVrMode(&vr, said, sizeof(said)) &&
                           strstr(said, "never off") == said;
    CHECK_TRUE(SUITE, "service: am-state passes 3D VR mode on, then off", CHECK_NONE,
               on_off && never_on && stub_old && refused_vr && never_off,
               "on and off %d, never on %d, a stand-in before 19 %d, refused before 19 %d, "
               "never off %d",
               on_off, never_on, stub_old, refused_vr, never_off);

    const bool global = ServiceJudgePlatformRegion(0u, 1u, true, said, sizeof(said));
    const bool nought = !ServiceJudgePlatformRegion(0u, 0u, true, said, sizeof(said)) &&
                        strstr(said, "another region") == said;
    const bool old = ServiceJudgePlatformRegion(SERVICE_RESULT(10, 221), 0u, false, said,
                                                sizeof(said));
    const bool refused_region =
        !ServiceJudgePlatformRegion(SERVICE_RESULT(10, 221), 0u, true, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-state passes the region Global", CHECK_NONE,
               global && nought && old && refused_region,
               "Global %d, nought %d, refused before 9 %d, refused at 9 %d", global, nought, old,
               refused_region);
}

static void CheckAmSelf(void) {
    ServiceSelfSetting seen = { 0u, 0u, 0u, 0u, 0u, 1u, 1u };
    const bool kept = ServiceJudgeSelfSetting("x", &seen, said, sizeof(said)) &&
                      strcmp(said, "read back as set: x 0x0 0, set to 1 0x0, read 0x0 1, put back "
                                   "0x0") == 0;
    char kept_said[480];
    memcpy(kept_said, said, sizeof(said));
    // A stub: the write succeeds and the read stays nought.
    seen.after = 0u;
    const bool stub = !ServiceJudgeSelfSetting("x", &seen, said, sizeof(said)) &&
                      strstr(said, "the setting before read back") == said;
    seen.after = 2u;
    const bool other = !ServiceJudgeSelfSetting("x", &seen, said, sizeof(said)) &&
                       strstr(said, "another setting read back") == said;
    seen = (ServiceSelfSetting){ 0u, SERVICE_RESULT(10, 221), 0u, 0u, 0u, 1u, 1u };
    const bool refused = !ServiceJudgeSelfSetting("x", &seen, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-self passes a setting read back as set", CHECK_NONE,
               kept && stub && other && refused,
               "kept %d \"%.160s\", a stub %d, another %d, refused %d", kept, kept_said, stub,
               other, refused);
}

static void CheckAmFunctions(void) {
    const uint64_t codes[3] = { 0x53552d6e65ull, 0x616aull, 0x7266ull }; // en-US, ja, fr
    const bool listed = ServiceJudgeDesiredLanguage(0u, 0x616aull, 0u, 3, codes, said,
                                                    sizeof(said)) &&
                        strstr(said, "a code set lists: GetDesiredLanguage 0x0 \"ja\"") == said;
    char listed_said[480];
    memcpy(listed_said, said, sizeof(said));
    const bool unlisted =
        !ServiceJudgeDesiredLanguage(0u, 0x6564ull, 0u, 3, codes, said, sizeof(said)) &&
        strstr(said, "a code set does not list") == said;
    const bool nought = !ServiceJudgeDesiredLanguage(0u, 0u, 0u, 3, codes, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-functions passes a desired language set lists", CHECK_NONE,
               listed && unlisted && nought, "listed %d \"%.160s\", unlisted %d, nought %d",
               listed, listed_said, unlisted, nought);

    ServiceStartEvent events[SERVICE_START_EVENTS] = {
        { true, 0u, 0x10u }, { true, 0u, 0x11u }, { true, 0u, 0x12u }, { true, 0u, 0x13u }
    };
    const bool all = ServiceJudgeStartEvents(events, said, sizeof(said)) &&
                     strstr(said, "every event handed out, 4 of 4 asked: 130 0x0 handle 10;") ==
                         said;
    char events_said[480];
    memcpy(events_said, said, sizeof(said));
    events[2].handle = 0u; // an unknown command's success
    const bool missing = !ServiceJudgeStartEvents(events, said, sizeof(said)) &&
                         strstr(said, "an event not handed out, 3 of 4") == said;
    events[2] = (ServiceStartEvent){ false, 0u, 0u };
    events[1] = events[3] = events[2];
    const bool old = ServiceJudgeStartEvents(events, said, sizeof(said)) &&
                     strstr(said, "140 newer than the firmware") != NULL;
    events[0].asked = false;
    const bool none = !ServiceJudgeStartEvents(events, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-functions passes every start event handed out", CHECK_NONE,
               all && missing && old && none,
               "all %d \"%.160s\", one missing %d, before 9.0.0 %d, none asked %d", all,
               events_said, missing, old, none);

    const bool empty = ServiceJudgeEmptyChannels(true, SERVICE_AM_NO_DATA, false,
                                                 SERVICE_AM_NO_DATA, false, said, sizeof(said));
    const bool stub_storage = !ServiceJudgeEmptyChannels(true, 0u, false, SERVICE_AM_NO_DATA,
                                                         false, said, sizeof(said)) &&
                              strstr(said, "the friend invitation channel not refused") == said;
    const bool user_left = !ServiceJudgeEmptyChannels(true, SERVICE_AM_NO_DATA, false, 0u, true,
                                                      said, sizeof(said)) &&
                           strstr(said, "the user channel not refused as empty") == said;
    const bool old_friend = ServiceJudgeEmptyChannels(false, 0u, false, SERVICE_AM_NO_DATA, false,
                                                      said, sizeof(said));
    const bool other = !ServiceJudgeEmptyChannels(true, SERVICE_AM_NO_MESSAGES, false,
                                                  SERVICE_AM_NO_DATA, false, said, sizeof(said));
    const bool user_other = !ServiceJudgeEmptyChannels(true, SERVICE_AM_NO_DATA, false,
                                                       SERVICE_AM_NO_MESSAGES, false, said,
                                                       sizeof(said));
    CHECK_TRUE(SUITE, "service: am-functions passes two empty channels refused as empty",
               CHECK_NONE, empty && stub_storage && user_left && old_friend && other && user_other,
               "empty %d, a stub's success %d, a user storage %d, before 9.0.0 %d, another "
               "result %d, the user channel's %d",
               empty, stub_storage, user_left, old_friend, other, user_other);
}

static void CheckAuddev(void) {
    static ServiceAudioDevices seen;
    memset(&seen, 0, sizeof(seen));
    seen.listed = 3;
    strcpy(seen.names[0], "AudioStereoJackOutput");
    strcpy(seen.names[1], "AudioBuiltInSpeakerOutput");
    strcpy(seen.names[2], "AudioTvOutput");
    strcpy(seen.active, "AudioTvOutput");
    const bool reden = ServiceJudgeAudioActive(&seen, said, sizeof(said)) &&
                       strcmp(said, "the active one listed: list 0x0, 3 listed: "
                                    "AudioStereoJackOutput AudioBuiltInSpeakerOutput "
                                    "AudioTvOutput; active 0x0 \"AudioTvOutput\"") == 0;
    char reden_said[480];
    memcpy(reden_said, said, sizeof(said));
    strcpy(seen.active, "AudioUsbDeviceOutput");
    const bool unlisted = !ServiceJudgeAudioActive(&seen, said, sizeof(said)) &&
                          strstr(said, "the active one not listed") == said;
    seen.active[0] = '\0';
    const bool unnamed = !ServiceJudgeAudioActive(&seen, said, sizeof(said));
    memset(&seen, 0, sizeof(seen));
    const bool stub = !ServiceJudgeAudioActive(&seen, said, sizeof(said)) &&
                      strstr(said, "nothing listed") == said;
    CHECK_TRUE(SUITE, "service: auddev passes the active output among those listed", CHECK_NONE,
               reden && unlisted && unnamed && stub,
               "reden %d \"%.200s\", not listed %d, no name %d, a stand-in %d", reden, reden_said,
               unlisted, unnamed, stub);

    const bool kept = ServiceJudgeAudioVolume(0u, 1.0f, 0u, 0.5f, 0u, 0.5f, 0u, said, sizeof(said));
    const bool fixed = !ServiceJudgeAudioVolume(0u, 1.0f, 0u, 0.5f, 0u, 1.0f, 0u, said,
                                                sizeof(said)) &&
                       strstr(said, "the volume before reads back") == said;
    const bool other = !ServiceJudgeAudioVolume(0u, 1.0f, 0u, 0.5f, 0u, 0.25f, 0u, said,
                                                sizeof(said));
    const bool refused = !ServiceJudgeAudioVolume(0u, 1.0f, SERVICE_RESULT(10, 221), 0.5f, 0u,
                                                  1.0f, 0u, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: auddev passes a volume read back as set", CHECK_NONE,
               kept && fixed && other && refused,
               "kept %d, the volume before %d, another %d, refused %d", kept, fixed, other,
               refused);
}

static void CheckFsdir(void) {
    ServiceDirRename seen = { 0u, 0u, 0u, 0u, SERVICE_PATH_NOT_FOUND, 0u, 0u, 1u };
    const bool reden = ServiceJudgeDirRename(&seen, said, sizeof(said)) &&
                       strstr(said, "renamed with what it holds") == said;
    // A rename that copies the name and leaves the old directory.
    seen.old_rc = 0u;
    const bool copied = !ServiceJudgeDirRename(&seen, said, sizeof(said)) &&
                        strstr(said, "the old name still there") == said;
    seen.old_rc = SERVICE_PATH_NOT_FOUND;
    seen.inside_rc = SERVICE_PATH_NOT_FOUND;
    const bool emptied = !ServiceJudgeDirRename(&seen, said, sizeof(said)) &&
                         strstr(said, "renamed without what it held") == said;
    // The file found under the new name as a directory.
    seen.inside_rc = 0u;
    seen.inside_type = 0u;
    const bool as_directory = !ServiceJudgeDirRename(&seen, said, sizeof(said));
    seen = (ServiceDirRename){ 0u, 0u, SERVICE_PATH_NOT_FOUND, SERVICE_PATH_NOT_FOUND,
                               0u, SERVICE_PATH_NOT_FOUND, 0u, 0u };
    const bool refused = !ServiceJudgeDirRename(&seen, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: fsdir passes a directory renamed with what it holds", CHECK_NONE,
               reden && copied && emptied && as_directory && refused,
               "reden %d, the old one left %d, emptied %d, the file a directory %d, refused %d",
               reden, copied, emptied, as_directory, refused);

    ServiceDirList list = { 0u, 0u, 0u, 0u, 1, 1, 0, "f", 1, SERVICE_DIR_FILE_SIZE };
    const bool one = ServiceJudgeDirList(&list, "f", said, sizeof(said)) &&
                     strstr(said, "its one file, once") == said;
    list.again = 1;
    const bool twice = !ServiceJudgeDirList(&list, "f", said, sizeof(said)) &&
                       strstr(said, "the file again") == said;
    list.again = 0;
    list.type = 0;
    const bool as_dir = !ServiceJudgeDirList(&list, "f", said, sizeof(said));
    list.type = 1;
    list.size = 0;
    const bool no_size = !ServiceJudgeDirList(&list, "f", said, sizeof(said));
    list.size = SERVICE_DIR_FILE_SIZE;
    list.count = 2;
    const bool counted = !ServiceJudgeDirList(&list, "f", said, sizeof(said)) &&
                         strstr(said, "another count") == said;
    CHECK_TRUE(SUITE, "service: fsdir passes the one file listed once", CHECK_NONE,
               one && twice && as_dir && no_size && counted,
               "one %d, twice %d, as a directory %d, no size %d, counted two %d", one, twice,
               as_dir, no_size, counted);

    const bool gone = ServiceJudgeDirDelete(0u, SERVICE_PATH_NOT_FOUND, said, sizeof(said));
    const bool left = !ServiceJudgeDirDelete(0u, 0u, said, sizeof(said)) &&
                      strstr(said, "still there") == said;
    // A refused delete.
    const bool not_empty = !ServiceJudgeDirDelete(SERVICE_RESULT(2, 8), 0u, said, sizeof(said)) &&
                           strstr(said, "refused") == said;
    CHECK_TRUE(SUITE, "service: fsdir passes a directory gone with what it held", CHECK_NONE,
               gone && left && not_empty, "gone %d, left %d, not empty %d", gone, left,
               not_empty);
}

static void CheckAmAudio(void) {
    ServiceExpectedVolume seen = { 0u, 0u, 0u, 0u, 0.25f, 1.0f, 0.5f, 0.25f, 0.5f, 0.25f };
    const bool kept = ServiceJudgeExpectedVolume(&seen, said, sizeof(said)) &&
                      strcmp(said, "read back as set: main 250/1000 library 1000/1000 (0x0), set "
                                   "to 500 and 250 (0x0), read 500 and 250 (0x0), put back 0x0") ==
                          0;
    char kept_said[480];
    memcpy(kept_said, said, sizeof(said));
    // A write that does nothing: the reads stay what they were.
    seen.main_after = 0.25f;
    seen.library_after = 1.0f;
    const bool stub = !ServiceJudgeExpectedVolume(&seen, said, sizeof(said)) &&
                      strstr(said, "the volumes before read back") == said;
    // The main one kept, the library one not.
    seen.main_after = 0.5f;
    const bool half = !ServiceJudgeExpectedVolume(&seen, said, sizeof(said)) &&
                      strstr(said, "other volumes read back") == said;
    seen = (ServiceExpectedVolume){ 0u, SERVICE_RESULT(10, 221), 0u, 0u, 0.25f, 1.0f,
                                    0.5f, 0.25f, 0.5f, 0.25f };
    const bool refused = !ServiceJudgeExpectedVolume(&seen, said, sizeof(said));
    ServiceMainVolume main = { 0u, 0u, 0u, 0u, 0.25f, 0.75f, 0.75f };
    const bool changed = ServiceJudgeMainVolume(&main, said, sizeof(said)) &&
                         strstr(said, "read back as changed") == said;
    main.after = 0.25f;
    const bool unchanged = !ServiceJudgeMainVolume(&main, said, sizeof(said)) &&
                           strstr(said, "the volume before read back") == said;
    main = (ServiceMainVolume){ 0u, 0u, SERVICE_RESULT(10, 221), 0u, 0.25f, 0.75f, 0.75f };
    const bool rate_refused = !ServiceJudgeMainVolume(&main, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-audio passes volumes read back as set", CHECK_NONE,
               kept && stub && half && refused && changed && unchanged && rate_refused,
               "kept %d \"%.160s\", a stub %d, half kept %d, refused %d, changed %d, unchanged %d, "
               "rate refused %d",
               kept, kept_said, stub, half, refused, changed, unchanged, rate_refused);
}

static void CheckAmChannel(void) {
    ServiceUserChannel seen = { 0u, 0u, 0u, 0u, 0u, SERVICE_AM_NO_DATA, true };
    const bool back = ServiceJudgeUserChannel(&seen, said, sizeof(said)) &&
                      strcmp(said, "popped back with its bytes: create 0x0, write 0x0, unpop 0x0, "
                                   "pop 0x0, read 0x0 same, pop again 0x480") == 0;
    char back_said[480];
    memcpy(back_said, said, sizeof(said));
    // An unpop that keeps nothing: the pop finds the channel empty.
    seen = (ServiceUserChannel){ 0u, 0u, 0u, SERVICE_AM_NO_DATA, 0u, SERVICE_AM_NO_DATA, false };
    const bool nothing = !ServiceJudgeUserChannel(&seen, said, sizeof(said)) &&
                         strstr(said, "the pop found nothing unpopped") == said;
    seen = (ServiceUserChannel){ 0u, 0u, 0u, 0u, 0u, SERVICE_AM_NO_DATA, false };
    const bool other = !ServiceJudgeUserChannel(&seen, said, sizeof(said)) &&
                       strstr(said, "the storage popped holds other bytes") == said;
    seen = (ServiceUserChannel){ 0u, 0u, 0u, 0u, 0u, 0u, true };
    const bool kept = !ServiceJudgeUserChannel(&seen, said, sizeof(said)) &&
                      strstr(said, "the channel is not empty after the pop") == said;
    ServiceChannelOrder order = { 0u, 0u, 0u, 0u, 0u, SERVICE_AM_NO_DATA, 2 };
    const bool stack = ServiceJudgeChannelOrder(&order, said, sizeof(said));
    order.got = 1;
    const bool queue = !ServiceJudgeChannelOrder(&order, said, sizeof(said)) &&
                       strstr(said, "the first storage unpopped came out first") == said;
    order = (ServiceChannelOrder){ 0u, 0u, 0u, 0u, 0u, 0u, 2 };
    const bool left = !ServiceJudgeChannelOrder(&order, said, sizeof(said)) &&
                      strstr(said, "the clear left a storage") == said;
    const bool first = ServiceJudgePreviousProgram(0u, -1, said, sizeof(said));
    const bool zero = !ServiceJudgePreviousProgram(0u, 0, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-channel passes a user channel that keeps what is unpopped",
               CHECK_NONE,
               back && nothing && other && kept && stack && queue && left && first && zero,
               "back %d \"%.160s\", nothing %d, other bytes %d, not emptied %d, a stack %d, a "
               "queue %d, a clear that leaves one %d, -1 %d, 0 %d",
               back, back_said, nothing, other, kept, stack, queue, left, first, zero);
}

static void CheckAmStorage(void) {
    ServiceStorageKeeps seen = { 0u, 0u, 0u, 0u, SERVICE_STORAGE_SIZE, true };
    const bool keeps = ServiceJudgeStorageKeeps(&seen, said, sizeof(said)) &&
                       strcmp(said, "its size, and the bytes written read back: CreateStorage("
                                    "0x40) 0x0, GetSize 0x0 0x40, write at 0x20 0x0, read 0x0 "
                                    "same") == 0;
    char keeps_said[480];
    memcpy(keeps_said, said, sizeof(said));
    seen.size = 0x80;
    const bool size = !ServiceJudgeStorageKeeps(&seen, said, sizeof(said)) &&
                      strstr(said, "another size") == said;
    seen.size = SERVICE_STORAGE_SIZE;
    seen.same = false;
    const bool lost = !ServiceJudgeStorageKeeps(&seen, said, sizeof(said)) &&
                      strstr(said, "other bytes read back") == said;
    const bool past =
        ServiceJudgeStoragePast("a read", 0u, SERVICE_AM_INVALID_OFFSET, said, sizeof(said));
    // A read cut at the end and answered as a success, and a refusal of another kind.
    const bool cut = !ServiceJudgeStoragePast("a read", 0u, 0u, said, sizeof(said)) &&
                     strstr(said, "accepted") == said;
    const bool other =
        !ServiceJudgeStoragePast("a write", 0u, SERVICE_RESULT(128, 511), said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-storage passes a storage that keeps its size and its end",
               CHECK_NONE, keeps && size && lost && past && cut && other,
               "keeps %d \"%.160s\", another size %d, lost bytes %d, past %d, a cut read %d, "
               "another refusal %d",
               keeps, keeps_said, size, lost, past, cut, other);
}

static void CheckMii(void) {
    const bool six = ServiceJudgeMiiCount(0u, 6, said, sizeof(said)) &&
                     strcmp(said, "six default Miis: GetCount(Default) 0x0, 6") == 0;
    char six_said[480];
    memcpy(six_said, said, sizeof(said));
    // A stub's count of nought.
    const bool none = !ServiceJudgeMiiCount(0u, 0, said, sizeof(said));
    ServiceMiiDefaults seen = { 0u, 0u, 6, 6u, 6u };
    const bool given = ServiceJudgeMiiDefaults(&seen, said, sizeof(said));
    seen.count = 0;
    const bool empty = !ServiceJudgeMiiDefaults(&seen, said, sizeof(said)) &&
                       strstr(said, "another count of Miis") == said;
    seen = (ServiceMiiDefaults){ 0u, 0u, 6, 5u, 6u };
    const bool source = !ServiceJudgeMiiDefaults(&seen, said, sizeof(said)) &&
                        strstr(said, "a Mii of another source") == said;
    seen = (ServiceMiiDefaults){ 0u, 0u, 6, 6u, 5u };
    const bool other = !ServiceJudgeMiiDefaults(&seen, said, sizeof(said)) &&
                       strstr(said, "other Miis than BuildDefault builds") == said;
    const bool state = ServiceJudgeMiiState(0u, false, 0u, false, said, sizeof(said));
    const bool full = !ServiceJudgeMiiState(0u, false, 0u, true, said, sizeof(said));
    const bool past = ServiceJudgeMiiPast(SERVICE_MII_INVALID_ARGUMENT, said, sizeof(said));
    const bool built = !ServiceJudgeMiiPast(0u, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: mii passes the six default Miis", CHECK_NONE,
               six && none && given && empty && source && other && state && full && past && built,
               "six %d \"%.120s\", none %d, given %d, empty %d, another source %d, other Miis %d, "
               "state %d, full %d, past %d, built past %d",
               six, six_said, none, given, empty, source, other, state, full, past, built);
}

static void CheckAmMode(void) {
    ServiceVrTurn seen = { 0u, 0u, 0u, 0u, 0u, false, true, false };
    const bool turned = ServiceJudgeVrTurn(&seen, said, sizeof(said)) &&
                        strcmp(said, "on, then off, as turned: read 0x0 0, on 0x0, read 0x0 1, off "
                                     "0x0, read 0x0 0") == 0;
    char turned_said[480];
    memcpy(turned_said, said, sizeof(said));
    // A stub's read, off whatever was asked.
    seen.on = false;
    const bool stub = !ServiceJudgeVrTurn(&seen, said, sizeof(said)) &&
                      strstr(said, "still off after it was turned on") == said;
    seen = (ServiceVrTurn){ 0u, 0u, 0u, 0u, 0u, false, true, true };
    const bool stuck = !ServiceJudgeVrTurn(&seen, said, sizeof(said)) &&
                       strstr(said, "still on after it was turned off") == said;
    const bool normal = ServiceJudgeBootMode(0u, 0u, 0u, false, said, sizeof(said));
    const bool maintenance = !ServiceJudgeBootMode(0u, 1u, 0u, false, said, sizeof(said));
    const bool updating = !ServiceJudgeBootMode(0u, 0u, 0u, true, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: am-mode passes VR mode as turned and a normal boot", CHECK_NONE,
               turned && stub && stuck && normal && maintenance && updating,
               "turned %d \"%.160s\", a stub %d, stuck on %d, normal %d, maintenance %d, "
               "updating %d",
               turned, turned_said, stub, stuck, normal, maintenance, updating);
}

static void CheckBsdLocal(void) {
    const bool raw = ServiceJudgeRawSocket(-1, SERVICE_EINVAL, said, sizeof(said)) &&
                     strcmp(said,
                            "refused with EINVAL: socket(AF_INET, SOCK_RAW, 0) -1, errno 22") == 0;
    char raw_said[480];
    memcpy(raw_said, said, sizeof(said));
    const bool opened = !ServiceJudgeRawSocket(3, 0, said, sizeof(said));
    ServiceTimeoutRange range = { 3, -1, SERVICE_EDOM, -1, SERVICE_EDOM };
    const bool edom = ServiceJudgeTimeoutRange(&range, said, sizeof(said));
    // A tv_usec of a second refused as EINVAL, and a negative tv_sec taken.
    range.usec_errno = SERVICE_EINVAL;
    const bool usec = !ServiceJudgeTimeoutRange(&range, said, sizeof(said)) &&
                      strstr(said, "a tv_usec of a second not EDOM") == said;
    range = (ServiceTimeoutRange){ 3, -1, SERVICE_EDOM, 0, 0 };
    const bool sec = !ServiceJudgeTimeoutRange(&range, said, sizeof(said)) &&
                     strstr(said, "a negative tv_sec not EDOM") == said;
    const bool shorter = ServiceJudgeTimeoutShort(3, -1, SERVICE_EINVAL, said, sizeof(said));
    const bool taken = !ServiceJudgeTimeoutShort(3, 0, 0, said, sizeof(said));
    ServiceNameShort name = { 3, 0, 0, 0, 0, 8u, true };
    const bool cut = ServiceJudgeNameShort(&name, said, sizeof(said));
    // The whole length said for 8 bytes written, and a write past them.
    name.length = 16u;
    const bool whole = !ServiceJudgeNameShort(&name, said, sizeof(said)) &&
                       strstr(said, "another length") == said;
    name = (ServiceNameShort){ 3, 0, 0, 0, 0, 8u, false };
    const bool past = !ServiceJudgeNameShort(&name, said, sizeof(said)) &&
                      strstr(said, "written past the buffer") == said;
    CHECK_TRUE(SUITE, "service: bsd-local passes the answers of a BSD stack", CHECK_NONE,
               raw && opened && edom && usec && sec && shorter && taken && cut && whole && past,
               "raw %d \"%.120s\", opened %d, EDOM %d, usec EINVAL %d, sec taken %d, short %d, "
               "short taken %d, cut %d, whole length %d, past %d",
               raw, raw_said, opened, edom, usec, sec, shorter, taken, cut, whole, past);
}

static void CheckIpc(void) {
    const bool room = ServiceJudgePointerBuffer(0u, 0u, 0x8000u, said, sizeof(said)) &&
                      strcmp(said, "room in it: set opened 0x0, QueryPointerBufferSize 0x0, "
                                   "0x8000") == 0;
    char room_said[480];
    memcpy(room_said, said, sizeof(said));
    const bool none = !ServiceJudgePointerBuffer(0u, 0u, 0u, said, sizeof(said));
    ServiceSessionClone seen = { 0u, 0u, 0u, 0u, true, 0x53552d6e65ull, 0x53552d6e65ull };
    const bool same = ServiceJudgeSessionClone(&seen, said, sizeof(said));
    // A clone that answers nought, as a session of no object would.
    seen.cloned = 0u;
    const bool other = !ServiceJudgeSessionClone(&seen, said, sizeof(said)) &&
                       strstr(said, "another answer") == said;
    seen = (ServiceSessionClone){ 0u, 0u, 0u, 0u, false, 1u, 1u };
    const bool handleless = !ServiceJudgeSessionClone(&seen, said, sizeof(said));
    const bool closed = ServiceJudgeSessionClose(0u, SERVICE_SESSION_CLOSED, SERVICE_SESSION_CLOSED,
                                                 said, sizeof(said));
    // A Close answered as a success, and a session that keeps answering after it.
    const bool success = !ServiceJudgeSessionClose(0u, 0u, SERVICE_SESSION_CLOSED, said,
                                                   sizeof(said)) &&
                         strstr(said, "the Close answered otherwise") == said;
    const bool alive =
        !ServiceJudgeSessionClose(0u, SERVICE_SESSION_CLOSED, 0u, said, sizeof(said)) &&
        strstr(said, "the session answered after its Close") == said;
    CHECK_TRUE(SUITE, "service: ipc passes the controller's answers and a Close", CHECK_NONE,
               room && none && same && other && handleless && closed && success && alive,
               "room %d \"%.160s\", none %d, same %d, another %d, no handle %d, closed %d, a "
               "success %d, alive %d",
               room, room_said, none, same, other, handleless, closed, success, alive);
}

static void CheckHid(void) {
    ServiceHidSampling seen = { 7u, 7u, 7u, 0u, 0x20u, 0x10u };
    const bool doubled = ServiceJudgeHidSampling(&seen, said, sizeof(said)) &&
                         strstr(said, "doubled and one apart: 7 LIFOs") == said;
    char doubled_said[480];
    memcpy(doubled_said, said, sizeof(said));
    // The entry numbered as its state, the bug 07f40d5cac fixed the other way round.
    seen.doubled = 6u;
    const bool same = !ServiceJudgeHidSampling(&seen, said, sizeof(said)) &&
                      strstr(said, "an entry not numbered twice its state") == said;
    seen = (ServiceHidSampling){ 7u, 7u, 5u, 0u, 0x20u, 0x10u };
    const bool apart = !ServiceJudgeHidSampling(&seen, said, sizeof(said)) &&
                       strstr(said, "two states not one apart") == said;
    seen = (ServiceHidSampling){ 0u, 0u, 0u, 0u, 0u, 0u };
    const bool none = !ServiceJudgeHidSampling(&seen, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: hid passes npad entries doubled and one apart", CHECK_NONE,
               doubled && same && apart && none,
               "doubled %d \"%.160s\", one not doubled %d, not one apart %d, none %d", doubled,
               doubled_said, same, apart, none);
}

static void CheckTimeSnap(void) {
    ServiceSnapshot seen = { 0u, 0u, 0u, true, true, true, 1760000100, 1760000000, 100, 0u };
    const bool whole = ServiceJudgeSnapshot(&seen, said, sizeof(said)) &&
                       strstr(said, "the user clock's, at its time, at the device's location") ==
                           said;
    char whole_said[480];
    memcpy(whole_said, said, sizeof(said));
    // A user time of another clock, a snapshot of another context, of another location.
    seen.user_time = 1760000101;
    const bool time = !ServiceJudgeSnapshot(&seen, said, sizeof(said)) &&
                      strstr(said, "another user time") == said;
    seen = (ServiceSnapshot){ 0u, 0u, 0u, false, true, true, 1760000100, 1760000000, 100, 0u };
    const bool context = !ServiceJudgeSnapshot(&seen, said, sizeof(said));
    seen = (ServiceSnapshot){ 0u, 0u, 0u, true, true, false, 1760000100, 1760000000, 100, 0u };
    const bool location = !ServiceJudgeSnapshot(&seen, said, sizeof(said));
    const bool span = ServiceJudgeSnapSpan(0u, 0u, 0, 0u, 5000000000ll, said, sizeof(said));
    // Seconds where nanoseconds belong, and a span to itself above nought.
    const bool seconds = !ServiceJudgeSnapSpan(0u, 0u, 0, 0u, 5, said, sizeof(said));
    const bool itself = !ServiceJudgeSnapSpan(0u, 0u, 1, 0u, 5000000000ll, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: time-snap passes a snapshot of the user clock and its span",
               CHECK_NONE, whole && time && context && location && span && seconds && itself,
               "whole %d \"%.160s\", another time %d, context %d, location %d, span %d, in "
               "seconds %d, to itself %d",
               whole, whole_said, time, context, location, span, seconds, itself);
}

static void CheckVi(void) {
    ServiceDisplayList seen = { 0u, 1, true, 1u, 1u, 1920u, 1080u };
    const bool one = ServiceJudgeDisplayList(&seen, said, sizeof(said)) &&
                     strstr(said, "the default display alone") == said;
    char one_said[480];
    memcpy(one_said, said, sizeof(said));
    // Every display Switchbrew names listed, and the default one in the docked size halved.
    seen.count = 5;
    const bool five = !ServiceJudgeDisplayList(&seen, said, sizeof(said));
    seen = (ServiceDisplayList){ 0u, 1, true, 1u, 1u, 1280u, 720u };
    const bool small = !ServiceJudgeDisplayList(&seen, said, sizeof(said));
    const uint32_t ok[6] = { 0u, 0u, 0u, 0u, 0u, SERVICE_VI_OPERATION_FAILED };
    const uint64_t window[6] = { 3u, 0u, 1u, 2u, 4u, 0u };
    const bool table = ServiceJudgeScalingModes(ok, window, said, sizeof(said));
    // The game's own numbering given back, and the sixth taken.
    const uint64_t same[6] = { 0u, 1u, 2u, 3u, 4u, 0u };
    const bool identity = !ServiceJudgeScalingModes(ok, same, said, sizeof(said));
    const uint32_t taken[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    const bool past = !ServiceJudgeScalingModes(taken, window, said, sizeof(said));
    const bool rounded = ServiceJudgeIndirectMemory(0u, 0x3A0000, 0x1000, said, sizeof(said));
    const bool exact = !ServiceJudgeIndirectMemory(0u, 1280 * 720 * 4, 0x1000, said, sizeof(said));
    CHECK_TRUE(SUITE, "service: vi passes the default display, the scaling table and the rounding",
               CHECK_NONE, one && five && small && table && identity && past && rounded && exact,
               "one %d \"%.160s\", five %d, small %d, table %d, identity %d, past %d, rounded %d, "
               "exact %d",
               one, one_said, five, small, table, identity, past, rounded, exact);
}

static void CheckRenderer(void) {
    ServiceRendererParams params = { 0u, 0u, 0u, 48000u, 240u, 2u };
    const bool opened = ServiceJudgeRendererParams(&params, said, sizeof(said)) &&
                        strstr(said, "as opened") == said;
    char opened_said[480];
    memcpy(opened_said, said, sizeof(said));
    // A frame of 32 kHz's, and the mix buffers of another renderer.
    params.count = 160u;
    const bool frame = !ServiceJudgeRendererParams(&params, said, sizeof(said));
    params = (ServiceRendererParams){ 0u, 0u, 0u, 48000u, 240u, 6u };
    const bool mixes = !ServiceJudgeRendererParams(&params, said, sizeof(said));
    ServiceRendererSetting seen = { 0u, 0u, 0u, 0u, 1000, 500, 500, 1000 };
    const bool kept = ServiceJudgeRendererSetting("x", &seen, said, sizeof(said)) &&
                      strstr(said, "the default, then as set") == said;
    // A stub's nought read back, and a first read that is not the default.
    seen.after = 0;
    const bool stub = !ServiceJudgeRendererSetting("x", &seen, said, sizeof(said)) &&
                      strstr(said, "the write not read back") == said;
    seen = (ServiceRendererSetting){ 0u, 0u, 0u, 0u, 0, 500, 500, 1000 };
    const bool initial = !ServiceJudgeRendererSetting("x", &seen, said, sizeof(said)) &&
                         strstr(said, "another value at first") == said;
    CHECK_TRUE(SUITE, "service: renderer passes its parameters and two settings kept", CHECK_NONE,
               opened && frame && mixes && kept && stub && initial,
               "opened %d \"%.160s\", another frame %d, other mixes %d, kept %d, a stub %d, "
               "another first %d",
               opened, opened_said, frame, mixes, kept, stub, initial);
}

void RunReferenceService(void) {
    CheckCases();
    CheckOpus();
    CheckSaveSize();
    CheckTempEmpty();
    CheckSelectUser();
    CheckLdnMonitor();
    CheckLanguages();
    CheckRandom();
    CheckNfpFilter();
    CheckAocPast();
    CheckAocMore();
    CheckAocPrepare();
    CheckTime();
    CheckFonts();
    CheckProfile();
    CheckAmState();
    CheckAmSelf();
    CheckAmFunctions();
    CheckAuddev();
    CheckFsdir();
    CheckAmAudio();
    CheckAmChannel();
    CheckAmStorage();
    CheckMii();
    CheckAmMode();
    CheckBsdLocal();
    CheckIpc();
    CheckHid();
    CheckTimeSnap();
    CheckVi();
    CheckRenderer();
}
