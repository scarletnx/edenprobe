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
