// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serviceref.h"

#include <stdio.h>

static const struct {
    const char *suite;
    const char *name;
} CASES[SERVICE_CASES] = {
    [SERVICE_OPUS_OPENS] = { "hwopus", "hwopus opens a decoder for 48 kHz stereo" },
    [SERVICE_OPUS_LIMITED] = { "hwopus",
                               "hwopus keeps 24 decoders and refuses the 25th as out of decoders" },
    [SERVICE_OPUS_FREED] = { "hwopus", "a decoder closed makes room for the next one" },
    [SERVICE_AM_OPENS] = { "am", "an application's functions open, with a user to ask about" },
    [SERVICE_SAVE_SIZE] = { "am",
                            "GetSaveDataSize reads back the size ExtendSaveData gave the save" },
    [SERVICE_TEMP_OPENS] = { "fsp", "the temporary storage opens" },
    [SERVICE_TEMP_EMPTY] = { "fsp", "the temporary storage starts each launch empty" },
    [SERVICE_ACC_OPENS] = { "account", "acc:u0 opens and counts its users" },
    [SERVICE_SELECT_USER] = { "account",
                              "TrySelectUserWithoutInteraction answers as its deprecated twin" },
    [SERVICE_LDN_OPENS] = { "ldn", "ldn:u opens" },
    [SERVICE_LDN_MONITOR] = { "ldn",
                              "CreateClientProcessMonitor hands out a monitor this process joins" },
    [SERVICE_SET_OPENS] = { "set", "set opens and lists its language codes" },
    [SERVICE_SET_LANGUAGE] = { "set", "GetLanguageCode is one of the codes set lists" },
    [SERVICE_SET_MAKE] = { "set", "MakeLanguageCode makes each index into the code listed there" },
    [SERVICE_SET_COUNT] = { "set",
                            "GetAvailableLanguageCodeCount2 leaves room for every code listed" },
    [SERVICE_CSRNG_OPENS] = { "csrng", "csrng opens" },
    [SERVICE_RANDOM] = { "csrng",
                         "GenerateRandomBytes fills the buffer, and differently each time" },
    [SERVICE_APM_OPENS] = { "apm", "apm opens, with a session" },
    [SERVICE_NFP_OPENS] = { "nfp", "nfp opens and initialises for a user" },
    [SERVICE_NFP_FILTER] = { "nfp",
                             "StartDetectionWithFilter answers what StartDetection answers" },
    [SERVICE_AOC_OPENS] = { "aoc", "aoc:u opens and counts this program's add-on content" },
    [SERVICE_AOC_PAST] = { "aoc",
                           "ListAddOnContent lists nothing at the end and refuses one past it" },
};

const char *ServiceCaseSuite(ServiceCaseId id) {
    return id < SERVICE_CASES ? CASES[id].suite : "service";
}

const char *ServiceCaseName(ServiceCaseId id) {
    return id < SERVICE_CASES ? CASES[id].name : "unknown";
}

bool ServiceJudgeOpusLimit(const uint32_t *results, uint32_t tried, char *out, size_t out_size) {
    uint32_t opened = 0;
    while (opened < tried && results[opened] == 0u) {
        ++opened;
    }
    if (opened == tried) {
        snprintf(out, out_size,
                 "all %u opened: no limit, where reden keeps %u and refuses the next with 0x%x",
                 (unsigned)tried, (unsigned)SERVICE_OPUS_LIMIT, (unsigned)SERVICE_OPUS_REFUSED);
        return false;
    }
    const uint32_t refused = results[opened];
    if (opened != SERVICE_OPUS_LIMIT) {
        snprintf(out, out_size, "%u opened, then decoder %u refused with 0x%x; reden keeps %u",
                 (unsigned)opened, (unsigned)(opened + 1u), (unsigned)refused,
                 (unsigned)SERVICE_OPUS_LIMIT);
        return false;
    }
    if (refused != SERVICE_OPUS_REFUSED) {
        snprintf(out, out_size,
                 "%u opened, then decoder %u refused with 0x%x, not 0x%x (HwOpus 385)",
                 (unsigned)opened, (unsigned)(opened + 1u), (unsigned)refused,
                 (unsigned)SERVICE_OPUS_REFUSED);
        return false;
    }
    snprintf(out, out_size, "%u opened, decoder %u refused with 0x%x (HwOpus 385)",
             (unsigned)opened, (unsigned)(opened + 1u), (unsigned)refused);
    return true;
}

bool ServiceJudgeOpusFreed(uint32_t reopened, char *out, size_t out_size) {
    if (reopened != 0u) {
        snprintf(out, out_size, "one of %u closed, the next open still refused with 0x%x",
                 (unsigned)SERVICE_OPUS_LIMIT, (unsigned)reopened);
        return false;
    }
    snprintf(out, out_size, "one of %u closed, the next opened", (unsigned)SERVICE_OPUS_LIMIT);
    return true;
}

bool ServiceJudgeSaveSize(const ServiceSaveSize *seen, char *out, size_t out_size) {
    const bool passed = seen->extend_rc == 0u && seen->after_rc == 0u &&
                        seen->after_normal == seen->asked_normal &&
                        seen->after_journal == seen->asked_journal;
    const char *const verdict =
        passed                   ? "the size asked for reads back"
        : seen->extend_rc != 0u  ? "ExtendSaveData refused"
        : seen->after_rc != 0u   ? "GetSaveDataSize refused after it"
        : seen->after_normal == seen->before_normal && seen->after_journal == seen->before_journal
            ? "the size before ExtendSaveData reads back"
            : "another size reads back";
    snprintf(out, out_size,
             "%s: before 0x%x %#llx/%#llx; ExtendSaveData(%#llx, %#llx) 0x%x, required %#llx; "
             "after 0x%x %#llx/%#llx",
             verdict, (unsigned)seen->before_rc, (unsigned long long)seen->before_normal,
             (unsigned long long)seen->before_journal, (unsigned long long)seen->asked_normal,
             (unsigned long long)seen->asked_journal, (unsigned)seen->extend_rc,
             (unsigned long long)seen->required, (unsigned)seen->after_rc,
             (unsigned long long)seen->after_normal, (unsigned long long)seen->after_journal);
    return passed;
}

bool ServiceJudgeTempEmpty(uint64_t promised, uint32_t found_rc, uint64_t found, char *out,
                           size_t out_size) {
    if (found_rc == SERVICE_PATH_NOT_FOUND) {
        snprintf(out, out_size, "the marker %016llx the launch before left is gone",
                 (unsigned long long)promised);
        return true;
    }
    if (found_rc == 0u) {
        snprintf(out, out_size,
                 "the marker the launch before left is still there, holding %016llx%s",
                 (unsigned long long)found,
                 found == promised ? "" : ", not the one it said it left");
        return false;
    }
    snprintf(out, out_size, "the marker %016llx could not be looked for: opening it answered 0x%x",
             (unsigned long long)promised, (unsigned)found_rc);
    return false;
}

bool ServiceJudgeSelectUser(const ServiceSelectUser *seen, char *out, size_t out_size) {
    const bool same = seen->uid51[0] == seen->uid52[0] && seen->uid51[1] == seen->uid52[1];
    const bool answered = seen->rc51 == 0u && seen->rc52 == 0u;
    const bool older = !seen->firmware19 && seen->rc51 == 0u && seen->rc52 != 0u;
    const bool passed = (answered && same) || older;
    const char *const verdict = answered && same ? "52 answers what 51 answers"
                                : older          ? "52 refused, by a machine older than 19.0.0"
                                : answered       ? "52 answers another user than 51"
                                : seen->rc51 != 0u ? "51 refused"
                                                   : "52 refused";
    snprintf(out, out_size,
             "%s: users %d; 51 0x%x %016llx%016llx, 52 0x%x %016llx%016llx; firmware %s 19.0.0",
             verdict, (int)seen->users, (unsigned)seen->rc51, (unsigned long long)seen->uid51[0],
             (unsigned long long)seen->uid51[1], (unsigned)seen->rc52,
             (unsigned long long)seen->uid52[0], (unsigned long long)seen->uid52[1],
             seen->firmware19 ? "at least" : "older than");
    return passed;
}

bool ServiceJudgeLdnMonitor(const ServiceLdnMonitor *seen, char *out, size_t out_size) {
    const bool joined = seen->create_rc == 0u && seen->handle != 0u && seen->registered &&
                        seen->register_rc == 0u;
    const bool older = !seen->firmware18 && seen->create_rc != 0u;
    const char *const verdict =
        joined               ? "the monitor came and RegisterClient took this process"
        : older              ? "1 refused, by a machine older than 18.0.0"
        : seen->create_rc != 0u ? "CreateClientProcessMonitor refused"
        : seen->handle == 0u ? "CreateClientProcessMonitor answered with no object"
                             : "RegisterClient refused";
    char registered[32] = "not asked";
    if (seen->registered) {
        snprintf(registered, sizeof(registered), "0x%x", (unsigned)seen->register_rc);
    }
    snprintf(out, out_size,
             "%s: CreateClientProcessMonitor 0x%x, handle 0x%x; RegisterClient %s; firmware %s "
             "18.0.0",
             verdict, (unsigned)seen->create_rc, (unsigned)seen->handle, registered,
             seen->firmware18 ? "at least" : "older than");
    return joined || older;
}

void ServiceLanguageText(uint64_t code, char out[9]) {
    uint32_t n = 0;
    for (; n < 8u; ++n) {
        const unsigned char c = (unsigned char)(code >> (8u * n));
        if (c == 0u) {
            break;
        }
        out[n] = c >= 0x20u && c < 0x7Fu ? (char)c : '?';
    }
    out[n] = '\0';
}

static int32_t Listed(const ServiceLanguages *seen) {
    if (seen->listed < 0) {
        return 0;
    }
    return seen->listed > (int32_t)SERVICE_LANGUAGES_MAX ? (int32_t)SERVICE_LANGUAGES_MAX
                                                          : seen->listed;
}

bool ServiceJudgeLanguage(const ServiceLanguages *seen, char *out, size_t out_size) {
    char code[9];
    ServiceLanguageText(seen->language, code);
    int32_t at = -1;
    for (int32_t i = 0; i < Listed(seen) && at < 0; ++i) {
        if (seen->codes[i] == seen->language) {
            at = i;
        }
    }
    const bool passed = seen->language_rc == 0u && seen->language != 0u && at >= 0;
    if (passed) {
        snprintf(out, out_size, "GetLanguageCode \"%s\", code %d of the %d listed", code,
                 (int)at, (int)seen->listed);
    } else {
        snprintf(out, out_size, "GetLanguageCode 0x%x \"%s\" (%#llx), not among the %d listed",
                 (unsigned)seen->language_rc, code, (unsigned long long)seen->language,
                 (int)seen->listed);
    }
    return passed;
}

bool ServiceJudgeMakeLanguage(const ServiceLanguages *seen, char *out, size_t out_size) {
    for (int32_t i = 0; i < Listed(seen); ++i) {
        if (seen->made_rc[i] != 0u || seen->made[i] != seen->codes[i]) {
            char made[9], listed[9];
            ServiceLanguageText(seen->made[i], made);
            ServiceLanguageText(seen->codes[i], listed);
            snprintf(out, out_size, "index %d made 0x%x \"%s\", where the list has \"%s\"",
                     (int)i, (unsigned)seen->made_rc[i], made, listed);
            return false;
        }
    }
    if (seen->past_rc != SERVICE_INVALID_LANGUAGE) {
        snprintf(out, out_size,
                 "the %d listed made right, but index %d answered 0x%x, not 0x%x (Settings 625)",
                 (int)seen->listed, (int)seen->listed, (unsigned)seen->past_rc,
                 (unsigned)SERVICE_INVALID_LANGUAGE);
        return false;
    }
    snprintf(out, out_size, "the %d listed made right, index %d refused with 0x%x",
             (int)seen->listed, (int)seen->listed, (unsigned)seen->past_rc);
    return true;
}

bool ServiceJudgeLanguageCount(const ServiceLanguages *seen, char *out, size_t out_size) {
    const bool passed = seen->count_rc == 0u && seen->count >= seen->listed && seen->listed > 0;
    snprintf(out, out_size, "%s: GetAvailableLanguageCodeCount2 0x%x, %d; %d listed",
             passed ? "room for every code" : "less room than codes", (unsigned)seen->count_rc,
             (int)seen->count, (int)seen->listed);
    return passed;
}

/// Whether the last eight bytes of a buffer still hold the fill, and whether it is all nought.
static bool Unwritten(const uint8_t *bytes) {
    for (uint32_t i = SERVICE_RANDOM_SIZE - 8u; i < SERVICE_RANDOM_SIZE; ++i) {
        if (bytes[i] != SERVICE_RANDOM_FILL) {
            return false;
        }
    }
    return true;
}

static bool Nought(const uint8_t *bytes) {
    for (uint32_t i = 0; i < SERVICE_RANDOM_SIZE; ++i) {
        if (bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

bool ServiceJudgeRandom(uint32_t rc1, uint32_t rc2, const uint8_t *first, const uint8_t *second,
                        char *out, size_t out_size) {
    bool same = true;
    for (uint32_t i = 0; i < SERVICE_RANDOM_SIZE; ++i) {
        same = same && first[i] == second[i];
    }
    const bool unwritten = Unwritten(first) || Unwritten(second);
    const bool nought = Nought(first) || Nought(second);
    const char *const verdict = rc1 != 0u || rc2 != 0u ? "refused"
                                : unwritten            ? "the buffer's end not written"
                                : nought               ? "a buffer of nought"
                                : same                 ? "the same bytes twice"
                                                       : "two buffers of their own";
    const bool passed = rc1 == 0u && rc2 == 0u && !unwritten && !nought && !same;
    snprintf(out, out_size,
             "%s: 0x%x and 0x%x; first %02x%02x%02x%02x..%02x%02x, "
             "second %02x%02x%02x%02x..%02x%02x",
             verdict, (unsigned)rc1, (unsigned)rc2, first[0], first[1], first[2], first[3],
             first[SERVICE_RANDOM_SIZE - 2u], first[SERVICE_RANDOM_SIZE - 1u], second[0],
             second[1], second[2], second[3], second[SERVICE_RANDOM_SIZE - 2u],
             second[SERVICE_RANDOM_SIZE - 1u]);
    return passed;
}

bool ServiceJudgeNfpFilter(uint32_t rc3, uint32_t rc25, int32_t devices, bool firmware23,
                           char *out, size_t out_size) {
    const bool same = rc3 == rc25;
    const bool older = !firmware23 && rc25 != 0u;
    snprintf(out, out_size, "%s: %d devices; StartDetection 0x%x, StartDetectionWithFilter 0x%x; "
                            "firmware %s 23.0.0",
             same    ? "the same answer"
             : older ? "25 refused, by a machine older than 23.0.0"
                     : "two answers",
             (int)devices, (unsigned)rc3, (unsigned)rc25, firmware23 ? "at least" : "older than");
    return same || older;
}

bool ServiceJudgeAocPast(const ServiceAocList *seen, char *out, size_t out_size) {
    const bool end = seen->end_rc == 0u && seen->end_listed == 0u;
    const bool past = seen->past_rc == SERVICE_AOC_PAST_END;
    const char *const verdict = seen->end_rc != 0u     ? "the list's end refused"
                                : !end                 ? "the list's end not empty"
                                : past                 ? "the end empty, one past it no record"
                                : seen->past_rc == 0u  ? "one past the end answered with success"
                                                       : "one past the end refused otherwise";
    snprintf(out, out_size,
             "%s: %u add-on contents; from %u 0x%x, %u listed; from %u 0x%x (no record is 0x%x)",
             verdict, (unsigned)seen->count, (unsigned)seen->count, (unsigned)seen->end_rc,
             (unsigned)seen->end_listed, (unsigned)seen->count + 1u, (unsigned)seen->past_rc,
             (unsigned)SERVICE_AOC_PAST_END);
    return end && past;
}
