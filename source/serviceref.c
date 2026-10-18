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
