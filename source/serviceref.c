// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serviceref.h"

#include <stdio.h>
#include <string.h>

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
    [SERVICE_TIME_OPENS] = { "time", "time:u hands out the user clock, the steady clock and the "
                                     "time zones" },
    [SERVICE_TIME_USER] = { "time",
                            "the user clock's time is its context's offset past the steady clock" },
    [SERVICE_TIME_STEADY] = { "time", "the steady clock counts seconds on one source" },
    [SERVICE_TIME_UTC] = { "time", "ToCalendarTime under UTC dates the epoch and a known time" },
    [SERVICE_TIME_POSIX] = { "time",
                             "ToPosixTime under UTC turns the known date back into its time" },
    [SERVICE_TIME_MY_RULE] = { "time",
                               "the device's location loads as the rule the service dates with" },
    [SERVICE_PL_OPENS] = { "pl", "pl:u opens and maps its shared fonts" },
    [SERVICE_PL_FONTS] = { "pl", "the six shared fonts are loaded, each inside behind its header" },
    [SERVICE_PL_ORDER] = { "pl", "the fonts in order of priority are the six, as told one by one" },
    [SERVICE_PROFILE_OPENS] = { "profile", "acc:u0 hands out the profile of a user" },
    [SERVICE_PROFILE_BASE] = { "profile", "Get and GetBase tell the same user, by a name" },
    [SERVICE_PROFILE_IMAGE] = { "profile",
                                "the user's picture is a 256x256 JPEG of the size GetImageSize "
                                "tells" },
    [SERVICE_PROFILE_EXISTS] = { "profile",
                                 "GetUserExistence finds the user and not one made up" },
    [SERVICE_STATE_MODE] = { "am-state",
                             "the operation mode and the default resolution agree" },
    [SERVICE_STATE_FOCUS] = { "am-state", "a running application is in focus" },
    [SERVICE_STATE_LOGO] = { "am-state",
                             "EnableStartupLogoDisappearedMessage brings message 95" },
    [SERVICE_STATE_VR] = { "am-state", "BeginVrMode3d turns 3D VR mode on and EndVrMode3d off" },
    [SERVICE_STATE_REGION] = { "am-state", "the settings platform region is Global" },
    [SERVICE_SELF_IDLE] = { "am-self",
                            "the idle time detection extension set is the one read back" },
    [SERVICE_SELF_SLEEP] = { "am-self", "auto sleep disabled is read back as set" },
    [SERVICE_FUNC_LANGUAGE] = { "am-functions",
                                "GetDesiredLanguage is one of the codes set lists" },
    [SERVICE_FUNC_EVENTS] = { "am-functions",
                              "the events an application asks for at start are handed out" },
    [SERVICE_FUNC_CHANNELS] = { "am-functions",
                                "nothing waits in the friend invitation channel or the user "
                                "channel" },
    [SERVICE_AUDDEV_OPENS] = { "auddev", "audren:u hands out the audio device service" },
    [SERVICE_AUDDEV_ACTIVE] = { "auddev", "the active output is one of the devices listed" },
    [SERVICE_AUDDEV_VOLUME] = { "auddev", "a volume set on the active device reads back" },
    [SERVICE_DIR_RENAME] = { "fsdir", "a directory renamed takes what it holds" },
    [SERVICE_DIR_LIST] = { "fsdir", "the renamed directory lists its one file, once" },
    [SERVICE_DIR_DELETE] = { "fsdir",
                             "DeleteDirectoryRecursively takes a directory and what it holds" },
    [SERVICE_AUDIOCTL_EXPECTED] = { "am-audio",
                                    "the expected master volumes set are the ones read back" },
    [SERVICE_AUDIOCTL_CHANGE] = { "am-audio", "a main applet volume changed is read back" },
    [SERVICE_CHANNEL_UNPOP] = { "am-channel",
                                "a storage unpopped to the user channel pops back with its bytes" },
    [SERVICE_CHANNEL_ORDER] = { "am-channel",
                                "the last storage unpopped pops first and a clear empties the "
                                "channel" },
    [SERVICE_CHANNEL_PREVIOUS] = { "am-channel",
                                   "the previous program index is -1 for the first program" },
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

static bool SourceNought(const uint8_t source[16]) {
    for (uint32_t i = 0; i < 16u; ++i) {
        if (source[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool SameSource(const uint8_t a[16], const uint8_t b[16]) {
    for (uint32_t i = 0; i < 16u; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

bool ServiceJudgeUserClock(const ServiceUserClock *seen, char *out, size_t out_size) {
    const int64_t past = seen->time - (seen->offset + seen->steady.seconds);
    const bool refused = seen->context_rc != 0u || seen->steady_rc != 0u || seen->time_rc != 0u;
    const bool nought = SourceNought(seen->steady.source);
    const bool source = SameSource(seen->context.source, seen->steady.source);
    const bool passed = !refused && !nought && source && (past == 0 || past == 1);
    const char *const verdict = refused   ? "refused"
                                : nought  ? "the steady clock's source is nought"
                                : !source ? "the context is on another source"
                                : passed  ? "the offset past the steady clock"
                                          : "another time";
    snprintf(out, out_size,
             "%s: context 0x%x offset %lld at %lld, steady 0x%x at %lld, source %02x%02x%02x%02x "
             "(context's %02x%02x%02x%02x); GetCurrentTime 0x%x %lld, %lld past offset + steady",
             verdict, (unsigned)seen->context_rc, (long long)seen->offset,
             (long long)seen->context.seconds, (unsigned)seen->steady_rc,
             (long long)seen->steady.seconds, seen->steady.source[0], seen->steady.source[1],
             seen->steady.source[2], seen->steady.source[3], seen->context.source[0],
             seen->context.source[1], seen->context.source[2], seen->context.source[3],
             (unsigned)seen->time_rc, (long long)seen->time, (long long)past);
    return passed;
}

bool ServiceJudgeSteadyClock(uint32_t first_rc, const ServiceTimePoint *first, uint32_t second_rc,
                             const ServiceTimePoint *second, char *out, size_t out_size) {
    const int64_t moved = second->seconds - first->seconds;
    const bool refused = first_rc != 0u || second_rc != 0u;
    const bool nought = SourceNought(first->source);
    const bool source = SameSource(first->source, second->source);
    const bool passed = !refused && !nought && source && (moved == 1 || moved == 2);
    const char *const verdict = refused   ? "refused"
                                : nought  ? "the source is nought"
                                : !source ? "two sources"
                                : passed  ? "seconds on one source"
                                          : "not a second's count";
    snprintf(out, out_size,
             "%s: 0x%x at %lld, 0x%x at %lld %llu ms later, moved %lld; source %02x%02x%02x%02x",
             verdict, (unsigned)first_rc, (long long)first->seconds, (unsigned)second_rc,
             (long long)second->seconds, (unsigned long long)(SERVICE_TIME_WAIT_NS / 1000000u),
             (long long)moved, first->source[0], first->source[1], first->source[2],
             first->source[3]);
    return passed;
}

static bool DateIs(const ServiceCalendarTime *date, const ServiceCalendarInfo *info, uint16_t year,
                   uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second,
                   uint32_t day_of_week, uint32_t day_of_year) {
    return date->year == year && date->month == month && date->day == day && date->hour == hour &&
           date->minute == minute && date->second == second && info->day_of_week == day_of_week &&
           info->day_of_year == day_of_year &&
           strncmp(info->zone, "UTC", sizeof(info->zone)) == 0 &&
           info->dst == 0u && info->utc_offset == 0;
}

static void DateText(const ServiceCalendarTime *date, const ServiceCalendarInfo *info, char *out,
                     size_t out_size) {
    char zone[9];
    memcpy(zone, info->zone, 8);
    zone[8] = '\0';
    snprintf(out, out_size, "%04u-%02u-%02u %02u:%02u:%02u wday %u yday %u \"%s\" dst %u %+d",
             (unsigned)date->year, (unsigned)date->month, (unsigned)date->day,
             (unsigned)date->hour, (unsigned)date->minute, (unsigned)date->second,
             (unsigned)info->day_of_week, (unsigned)info->day_of_year, zone, (unsigned)info->dst,
             (int)info->utc_offset);
}

bool ServiceJudgeUtcDates(const ServiceUtcDates *seen, char *out, size_t out_size) {
    const bool refused = seen->load_rc != 0u || seen->rc[0] != 0u || seen->rc[1] != 0u;
    const bool epoch = DateIs(&seen->date[0], &seen->info[0], 1970, 1, 1, 0, 0, 0, 4, 0);
    const bool known = DateIs(&seen->date[1], &seen->info[1], 2023, 11, 14, 22, 13, 20, 2, 317);
    const bool passed = !refused && epoch && known;
    char first[96], second[96];
    DateText(&seen->date[0], &seen->info[0], first, sizeof(first));
    DateText(&seen->date[1], &seen->info[1], second, sizeof(second));
    snprintf(out, out_size, "%s: rule 0x%x; 0 0x%x %s; %d 0x%x %s",
             refused  ? "refused"
             : passed ? "both dates right"
             : !epoch ? "the epoch dated otherwise"
                      : "the known time dated otherwise",
             (unsigned)seen->load_rc, (unsigned)seen->rc[0], first, SERVICE_TIME_KNOWN,
             (unsigned)seen->rc[1], second);
    return passed;
}

bool ServiceJudgeUtcPosix(uint32_t load_rc, uint32_t rc, int32_t count, int64_t first, char *out,
                          size_t out_size) {
    const bool passed = load_rc == 0u && rc == 0u && count == 1 && first == SERVICE_TIME_KNOWN;
    snprintf(out, out_size, "%s: rule 0x%x; 2023-11-14 22:13:20 0x%x, %d times, the first %lld",
             load_rc != 0u || rc != 0u ? "refused"
             : passed                  ? "the known time back"
             : count != 1              ? "not one time"
                                       : "another time",
             (unsigned)load_rc, (unsigned)rc, (int)count, (long long)first);
    return passed;
}

bool ServiceJudgeMyRule(const ServiceMyRule *seen, char *out, size_t out_size) {
    const bool refused = seen->name_rc != 0u || seen->load_rc != 0u || seen->time_rc != 0u ||
                         seen->mine_rc != 0u || seen->loaded_rc != 0u;
    const bool named =
        memchr(seen->name, '\0', sizeof(seen->name)) != NULL && seen->name[0] != '\0';
    const bool same = memcmp(&seen->mine_date, &seen->loaded_date, sizeof(seen->mine_date)) == 0 &&
                      seen->mine_info.day_of_week == seen->loaded_info.day_of_week &&
                      seen->mine_info.day_of_year == seen->loaded_info.day_of_year &&
                      memcmp(seen->mine_info.zone, seen->loaded_info.zone, 8) == 0 &&
                      seen->mine_info.dst == seen->loaded_info.dst &&
                      seen->mine_info.utc_offset == seen->loaded_info.utc_offset;
    const bool passed = !refused && named && same;
    char name[0x25];
    memcpy(name, seen->name, sizeof(seen->name));
    name[sizeof(seen->name)] = '\0';
    char mine[96], loaded[96];
    DateText(&seen->mine_date, &seen->mine_info, mine, sizeof(mine));
    DateText(&seen->loaded_date, &seen->loaded_info, loaded, sizeof(loaded));
    snprintf(out, out_size,
             "%s: location 0x%x \"%.36s\", loaded 0x%x; time 0x%x %lld; my rule 0x%x %s; the "
             "loaded rule 0x%x %s",
             refused  ? "refused"
             : !named ? "no location name"
             : passed ? "the same date under both"
                      : "two dates",
             (unsigned)seen->name_rc, name, (unsigned)seen->load_rc, (unsigned)seen->time_rc,
             (long long)seen->time, (unsigned)seen->mine_rc, mine, (unsigned)seen->loaded_rc,
             loaded);
    return passed;
}

static uint32_t Little32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint32_t Big32(const uint8_t *b) {
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | (uint32_t)b[3];
}

/// A TrueType (00 01 00 00, "true"), OpenType ("OTTO") or collection ("ttcf") file's first word.
static bool FontStart(const uint8_t start[4]) {
    const uint32_t word = Big32(start);
    return word == 0x00010000u || word == 0x74727565u || word == 0x4F54544Fu ||
           word == 0x74746366u;
}

bool ServiceJudgeFonts(const ServiceFont fonts[SERVICE_FONTS], char *out, size_t out_size) {
    const char *verdict = "six fonts, each inside behind its header";
    uint32_t failed = SERVICE_FONTS;
    for (uint32_t i = 0; i < SERVICE_FONTS && failed == SERVICE_FONTS; ++i) {
        const ServiceFont *f = &fonts[i];
        const bool inside = f->offset >= 8u && f->offset <= SERVICE_FONT_MEMORY &&
                            f->size <= SERVICE_FONT_MEMORY - f->offset;
        const char *wrong =
            f->request_rc != 0u || f->state_rc != 0u || f->size_rc != 0u || f->offset_rc != 0u
                ? "refused"
            : f->state != 1u                              ? "not loaded"
            : f->size == 0u                               ? "empty"
            : !inside                                     ? "outside the memory"
            : Little32(f->header) != SERVICE_FONT_MAGIC   ? "behind another magic"
            : !FontStart(f->start)                        ? "not a font"
                                                          : NULL;
        if (wrong != NULL) {
            failed = i;
            verdict = wrong;
        }
    }
    int at = snprintf(out, out_size, "%s%s", failed == SERVICE_FONTS ? "" : "font ",
                      failed == SERVICE_FONTS ? verdict : "");
    if (failed != SERVICE_FONTS) {
        at += snprintf(out + at, out_size - (size_t)at, "%u %s", (unsigned)failed, verdict);
    }
    for (uint32_t i = 0; i < SERVICE_FONTS && at > 0 && (size_t)at < out_size; ++i) {
        const ServiceFont *f = &fonts[i];
        // The four results ORed: nought when all four succeeded.
        at += snprintf(out + at, out_size - (size_t)at,
                       "%s %u: %x %u %#x@%#x %08x le %#x be %#x %02x%02x%02x%02x",
                       i == 0u ? ":" : ";", (unsigned)i,
                       (unsigned)(f->request_rc | f->state_rc | f->size_rc | f->offset_rc),
                       (unsigned)f->state, (unsigned)f->size, (unsigned)f->offset,
                       (unsigned)Little32(f->header),
                       (unsigned)(Little32(f->header + 4) ^ SERVICE_FONT_KEY),
                       (unsigned)(Big32(f->header + 4) ^ SERVICE_FONT_KEY), f->start[0],
                       f->start[1], f->start[2], f->start[3]);
    }
    return failed == SERVICE_FONTS;
}

bool ServiceJudgeFontOrder(const ServiceFontOrder *seen, const ServiceFont fonts[SERVICE_FONTS],
                           char *out, size_t out_size) {
    uint32_t listed = 0, told = 0;
    const uint32_t count = seen->count < SERVICE_FONTS ? seen->count : SERVICE_FONTS;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t type = seen->types[i];
        if (type < SERVICE_FONTS && (listed & (1u << type)) == 0u) {
            listed |= 1u << type;
            if (seen->offsets[i] == fonts[type].offset && seen->sizes[i] == fonts[type].size) {
                ++told;
            }
        }
    }
    const bool refused = seen->language_rc != 0u || seen->rc != 0u;
    const bool all = seen->count == SERVICE_FONTS && listed == (1u << SERVICE_FONTS) - 1u;
    const bool passed = !refused && seen->loaded != 0u && all && told == SERVICE_FONTS;
    char language[9];
    ServiceLanguageText(seen->language, language);
    snprintf(out, out_size,
             "%s: language 0x%x \"%s\"; 0x%x, loaded %u, %u listed: %u %u %u %u %u %u; %u of 6 "
             "where their types were told",
             refused              ? "refused"
             : seen->loaded == 0u ? "not loaded"
             : !all               ? "not the six once each"
             : passed             ? "the six, as told"
                                  : "another offset or size",
             (unsigned)seen->language_rc, language, (unsigned)seen->rc, (unsigned)seen->loaded,
             (unsigned)seen->count, (unsigned)seen->types[0], (unsigned)seen->types[1],
             (unsigned)seen->types[2], (unsigned)seen->types[3], (unsigned)seen->types[4],
             (unsigned)seen->types[5], (unsigned)told);
    return passed;
}

static bool NicknameOk(const char nickname[0x20]) {
    return nickname[0] != '\0' && memchr(nickname, '\0', 0x20) != NULL;
}

bool ServiceJudgeProfileBase(const uint64_t asked[2], uint32_t get_rc,
                             const ServiceProfileBase *get, uint32_t base_rc,
                             const ServiceProfileBase *base, char *out, size_t out_size) {
    const bool refused = get_rc != 0u || base_rc != 0u;
    const bool user = get->uid[0] == asked[0] && get->uid[1] == asked[1];
    const bool same = memcmp(get, base, sizeof(*get)) == 0;
    const bool named = NicknameOk(get->nickname);
    const bool passed = !refused && user && same && named;
    char nickname[0x21];
    memcpy(nickname, get->nickname, 0x20);
    nickname[0x20] = '\0';
    for (uint32_t i = 0; nickname[i] != '\0'; ++i) {
        nickname[i] = nickname[i] < 0x20 || nickname[i] == '"' ? '?' : nickname[i];
    }
    snprintf(out, out_size,
             "%s: Get 0x%x user %016llx%016llx \"%s\" edited %llu; GetBase 0x%x user "
             "%016llx%016llx, %s",
             refused  ? "refused"
             : !user  ? "another user"
             : !named ? "no name"
             : passed ? "the user, by a name"
                      : "two bases",
             (unsigned)get_rc, (unsigned long long)get->uid[0], (unsigned long long)get->uid[1],
             nickname, (unsigned long long)get->edited, (unsigned)base_rc,
             (unsigned long long)base->uid[0], (unsigned long long)base->uid[1],
             same ? "the same base" : "another base");
    return passed;
}

/// The frame of a JPEG: the first start-of-frame segment (C0 to C2) before the scan, its height
/// and width. False when the markers run out first.
static bool JpegFrame(const uint8_t *bytes, uint32_t size, uint32_t *width, uint32_t *height) {
    uint32_t at = 2;
    while (at + 4u <= size && bytes[at] == 0xFFu) {
        const uint8_t marker = bytes[at + 1u];
        const uint32_t length = (uint32_t)bytes[at + 2u] << 8 | bytes[at + 3u];
        if (marker >= 0xC0u && marker <= 0xC2u && at + 9u <= size) {
            *height = (uint32_t)bytes[at + 5u] << 8 | bytes[at + 6u];
            *width = (uint32_t)bytes[at + 7u] << 8 | bytes[at + 8u];
            return true;
        }
        if (marker == 0xDAu || length < 2u) {
            return false;
        }
        at += 2u + length;
    }
    return false;
}

bool ServiceJudgeProfileImage(uint32_t size_rc, uint32_t size, uint32_t load_rc, uint32_t loaded,
                              const uint8_t *bytes, char *out, size_t out_size) {
    const bool refused = size_rc != 0u || load_rc != 0u;
    const bool fits = loaded >= 4u && loaded <= SERVICE_PICTURE_MAX;
    const bool jpeg = fits && bytes[0] == 0xFFu && bytes[1] == 0xD8u &&
                      bytes[loaded - 2u] == 0xFFu && bytes[loaded - 1u] == 0xD9u;
    uint32_t width = 0, height = 0;
    const bool frame = jpeg && JpegFrame(bytes, loaded, &width, &height);
    const bool square = frame && width == SERVICE_PICTURE_SIDE && height == SERVICE_PICTURE_SIDE;
    const bool passed = !refused && size == loaded && square;
    snprintf(out, out_size,
             "%s: GetImageSize 0x%x %u bytes, LoadImage 0x%x %u bytes, %02x%02x..%02x%02x, "
             "frame %ux%u",
             refused          ? "refused"
             : !fits          ? "no picture"
             : size != loaded ? "a size the load does not keep"
             : !jpeg          ? "not a JPEG"
             : !frame         ? "a JPEG with no frame"
             : passed         ? "a 256x256 JPEG"
                              : "a JPEG of another size",
             (unsigned)size_rc, (unsigned)size, (unsigned)load_rc, (unsigned)loaded,
             fits ? bytes[0] : 0u, fits ? bytes[1] : 0u, fits ? bytes[loaded - 2u] : 0u,
             fits ? bytes[loaded - 1u] : 0u, (unsigned)width, (unsigned)height);
    return passed;
}

bool ServiceJudgeUserExistence(uint32_t user_rc, bool user, uint32_t made_up_rc, bool made_up,
                               char *out, size_t out_size) {
    const bool passed = user_rc == 0u && made_up_rc == 0u && user && !made_up;
    snprintf(out, out_size, "%s: the user 0x%x %s, one made up 0x%x %s",
             user_rc != 0u || made_up_rc != 0u ? "refused"
             : passed                          ? "the user and nobody else"
             : !user                           ? "the user not found"
                                               : "a made up user found",
             (unsigned)user_rc, user ? "exists" : "does not", (unsigned)made_up_rc,
             made_up ? "exists" : "does not");
    return passed;
}

bool ServiceJudgeOperationMode(uint32_t mode_rc, uint8_t mode, uint32_t resolution_rc,
                               int32_t width, int32_t height, char *out, size_t out_size) {
    const bool refused = mode_rc != 0u || resolution_rc != 0u;
    const bool handheld = mode == 0u && width == 1280 && height == 720;
    const bool docked = mode == 1u && width == 1920 && height == 1080;
    const bool passed = !refused && (handheld || docked);
    snprintf(out, out_size, "%s: GetOperationMode 0x%x %u (0 Handheld, 1 Console), "
                            "GetDefaultDisplayResolution 0x%x %dx%d",
             refused   ? "refused"
             : passed  ? "they agree"
             : mode > 1u ? "a mode that is neither"
                       : "a resolution of the other mode",
             (unsigned)mode_rc, (unsigned)mode, (unsigned)resolution_rc, (int)width, (int)height);
    return passed;
}

bool ServiceJudgeFocus(uint32_t rc, uint8_t state, char *out, size_t out_size) {
    const bool passed = rc == 0u && state == 1u;
    snprintf(out, out_size, "%s: GetCurrentFocusState 0x%x %u (1 InFocus)",
             rc != 0u ? "refused" : passed ? "in focus" : "out of focus", (unsigned)rc,
             (unsigned)state);
    return passed;
}

bool ServiceJudgeLogoMessage(const ServiceLogoMessage *seen, char *out, size_t out_size) {
    bool gone = false;
    const uint32_t count = seen->count < SERVICE_AM_MESSAGES ? seen->count : SERVICE_AM_MESSAGES;
    for (uint32_t i = 0; i < count; ++i) {
        gone = gone || seen->messages[i] == SERVICE_AM_LOGO_GONE;
    }
    const bool older = !seen->firmware21 && seen->enable_rc != 0u;
    const bool passed = (seen->enable_rc == 0u && gone) || older;
    int at = snprintf(out, out_size, "%s: 130 0x%x; %u received, then 0x%x (none left is 0x%x):",
                      seen->enable_rc == 0u && gone ? "message 95 came"
                      : older                       ? "130 refused, by a machine older than 21.0.0"
                      : seen->enable_rc != 0u        ? "130 refused"
                                                    : "130 succeeded and no message 95 came",
                      (unsigned)seen->enable_rc, (unsigned)count, (unsigned)seen->end_rc,
                      (unsigned)SERVICE_AM_NO_MESSAGES);
    for (uint32_t i = 0; i < count && at > 0 && (size_t)at < out_size; ++i) {
        at += snprintf(out + at, out_size - (size_t)at, " %u", (unsigned)seen->messages[i]);
    }
    if (at > 0 && (size_t)at < out_size) {
        snprintf(out + at, out_size - (size_t)at, "; firmware %s 21.0.0",
                 seen->firmware21 ? "at least" : "older than");
    }
    return passed;
}

bool ServiceJudgeVrMode(const ServiceVrMode *seen, char *out, size_t out_size) {
    const bool answered = seen->begin_rc == 0u && seen->on_rc == 0u && seen->end_rc == 0u &&
                          seen->off_rc == 0u;
    const bool both = answered && seen->on != 0u && seen->off == 0u;
    const bool older = !seen->firmware19 && seen->begin_rc != 0u;
    snprintf(out, out_size,
             "%s: BeginVrMode3d 0x%x, IsVrModeEnabled3d 0x%x %u; EndVrMode3d 0x%x, "
             "IsVrModeEnabled3d 0x%x %u; firmware %s 19.0.0",
             both                ? "on, then off"
             : older             ? "1000 refused, by a machine older than 19.0.0"
             : !answered         ? "refused"
             : seen->on == 0u    ? "never on"
                                 : "never off",
             (unsigned)seen->begin_rc, (unsigned)seen->on_rc, (unsigned)seen->on,
             (unsigned)seen->end_rc, (unsigned)seen->off_rc, (unsigned)seen->off,
             seen->firmware19 ? "at least" : "older than");
    return both || older;
}

bool ServiceJudgePlatformRegion(uint32_t rc, uint32_t region, bool firmware9, char *out,
                                size_t out_size) {
    const bool passed = rc == 0u && region == 1u;
    const bool older = !firmware9 && rc != 0u;
    snprintf(out, out_size, "%s: GetSettingsPlatformRegion 0x%x %u (1 Global, 2 China); "
                            "firmware %s 9.0.0",
             passed    ? "Global"
             : older   ? "300 refused, by a machine older than 9.0.0"
             : rc != 0u ? "refused"
                       : "another region",
             (unsigned)rc, (unsigned)region, firmware9 ? "at least" : "older than");
    return passed || older;
}

bool ServiceJudgeSelfSetting(const char *what, const ServiceSelfSetting *seen, char *out,
                             size_t out_size) {
    const bool refused = seen->before_rc != 0u || seen->set_rc != 0u || seen->after_rc != 0u ||
                         seen->restore_rc != 0u;
    const bool passed = !refused && seen->after == seen->asked;
    snprintf(out, out_size, "%s: %s 0x%x %u, set to %u 0x%x, read 0x%x %u, put back 0x%x",
             refused                       ? "refused"
             : passed                      ? "read back as set"
             : seen->after == seen->before ? "the setting before read back"
                                           : "another setting read back",
             what, (unsigned)seen->before_rc, (unsigned)seen->before, (unsigned)seen->asked,
             (unsigned)seen->set_rc, (unsigned)seen->after_rc, (unsigned)seen->after,
             (unsigned)seen->restore_rc);
    return passed;
}

bool ServiceJudgeDesiredLanguage(uint32_t rc, uint64_t code, uint32_t list_rc, int32_t listed,
                                 const uint64_t *codes, char *out, size_t out_size) {
    bool listed_there = false;
    const int32_t n = listed > (int32_t)SERVICE_LANGUAGES_MAX ? (int32_t)SERVICE_LANGUAGES_MAX
                                                             : listed;
    for (int32_t i = 0; i < n; ++i) {
        listed_there = listed_there || codes[i] == code;
    }
    const bool refused = rc != 0u || list_rc != 0u;
    const bool passed = !refused && code != 0u && listed_there;
    char text[9];
    ServiceLanguageText(code, text);
    snprintf(out, out_size, "%s: GetDesiredLanguage 0x%x \"%s\"; set lists 0x%x, %d codes",
             refused      ? "refused"
             : code == 0u ? "a code of nought"
             : passed     ? "a code set lists"
                          : "a code set does not list",
             (unsigned)rc, text, (unsigned)list_rc, (int)listed);
    return passed;
}

bool ServiceJudgeStartEvents(const ServiceStartEvent events[SERVICE_START_EVENTS], char *out,
                             size_t out_size) {
    static const uint32_t COMMANDS[SERVICE_START_EVENTS] = { 130, 140, 150, 160 };
    uint32_t asked = 0, given = 0;
    for (uint32_t i = 0; i < SERVICE_START_EVENTS; ++i) {
        if (events[i].asked) {
            ++asked;
            given += events[i].rc == 0u && events[i].handle != 0u ? 1u : 0u;
        }
    }
    const bool passed = asked > 0u && given == asked;
    int at = snprintf(out, out_size, "%s, %u of %u asked:",
                      passed       ? "every event handed out"
                      : asked == 0u ? "none asked"
                                   : "an event not handed out",
                      (unsigned)given, (unsigned)asked);
    for (uint32_t i = 0; i < SERVICE_START_EVENTS && at > 0 && (size_t)at < out_size; ++i) {
        if (events[i].asked) {
            at += snprintf(out + at, out_size - (size_t)at, " %u 0x%x handle %x;",
                           (unsigned)COMMANDS[i], (unsigned)events[i].rc,
                           (unsigned)events[i].handle);
        } else {
            at += snprintf(out + at, out_size - (size_t)at, " %u newer than the firmware;",
                           (unsigned)COMMANDS[i]);
        }
    }
    return passed;
}

bool ServiceJudgeEmptyChannels(bool friend_asked, uint32_t friend_rc, bool friend_storage,
                               uint32_t user_rc, bool user_storage, char *out, size_t out_size) {
    const bool friend_empty = !friend_asked || friend_rc == SERVICE_AM_NO_DATA;
    const bool user_empty = user_rc == SERVICE_AM_NO_DATA;
    const bool passed = friend_empty && user_empty;
    snprintf(out, out_size,
             "%s: friend invitation channel %s 0x%x%s; user channel 0x%x%s (empty is 0x%x)",
             passed           ? "both empty"
             : !friend_empty  ? "the friend invitation channel not refused as empty"
                              : "the user channel not refused as empty",
             friend_asked ? "asked" : "newer than the firmware", (unsigned)friend_rc,
             friend_storage ? ", a storage" : "", (unsigned)user_rc,
             user_storage ? ", a storage" : "", (unsigned)SERVICE_AM_NO_DATA);
    return passed;
}

bool ServiceJudgeAudioActive(const ServiceAudioDevices *seen, char *out, size_t out_size) {
    const bool refused = seen->list_rc != 0u || seen->active_rc != 0u;
    const int32_t n = seen->listed > (int32_t)SERVICE_AUDIO_DEVICES ? (int32_t)SERVICE_AUDIO_DEVICES
                                                                    : seen->listed;
    bool found = false;
    for (int32_t i = 0; i < n; ++i) {
        found = found || strncmp(seen->names[i], seen->active, SERVICE_AUDIO_NAME) == 0;
    }
    const bool named = seen->active[0] != '\0';
    const bool passed = !refused && n > 0 && named && found;
    int at = snprintf(out, out_size, "%s: list 0x%x, %d listed:",
                      refused  ? "refused"
                      : n <= 0 ? "nothing listed"
                      : !named ? "no active device"
                      : passed ? "the active one listed"
                               : "the active one not listed",
                      (unsigned)seen->list_rc, (int)seen->listed);
    for (int32_t i = 0; i < n && at > 0 && (size_t)at < out_size; ++i) {
        at += snprintf(out + at, out_size - (size_t)at, " %.40s", seen->names[i]);
    }
    if (at > 0 && (size_t)at < out_size) {
        snprintf(out + at, out_size - (size_t)at, "; active 0x%x \"%.40s\"",
                 (unsigned)seen->active_rc, seen->active);
    }
    return passed;
}

bool ServiceJudgeAudioVolume(uint32_t before_rc, float before, uint32_t set_rc, float asked,
                             uint32_t after_rc, float after, uint32_t restore_rc, char *out,
                             size_t out_size) {
    const bool refused = before_rc != 0u || set_rc != 0u || after_rc != 0u || restore_rc != 0u;
    const bool passed = !refused && after == asked;
    snprintf(out, out_size, "%s: volume 0x%x %.3f, set to %.3f 0x%x, read 0x%x %.3f, put back 0x%x",
             refused           ? "refused"
             : passed          ? "the volume set reads back"
             : after == before ? "the volume before reads back"
                               : "another volume reads back",
             (unsigned)before_rc, (double)before, (double)asked, (unsigned)set_rc,
             (unsigned)after_rc, (double)after, (unsigned)restore_rc);
    return passed;
}

bool ServiceJudgeDirRename(const ServiceDirRename *seen, char *out, size_t out_size) {
    const bool made = seen->make_rc == 0u && seen->file_rc == 0u;
    const bool moved = seen->rename_rc == 0u && seen->new_rc == 0u && seen->new_type == 0u;
    const bool gone = seen->old_rc == SERVICE_PATH_NOT_FOUND;
    const bool inside = seen->inside_rc == 0u && seen->inside_type == 1u;
    const bool passed = made && moved && gone && inside;
    snprintf(out, out_size,
             "%s: directory 0x%x, file 0x%x; RenameDirectory 0x%x; the new name 0x%x type %u, "
             "the old 0x%x, the file under the new 0x%x type %u (0 directory, 1 file)",
             !made    ? "the directory or its file not made"
             : !moved ? "the new name not a directory"
             : !gone  ? "the old name still there"
             : inside ? "renamed with what it holds"
                      : "renamed without what it held",
             (unsigned)seen->make_rc, (unsigned)seen->file_rc, (unsigned)seen->rename_rc,
             (unsigned)seen->new_rc, (unsigned)seen->new_type, (unsigned)seen->old_rc,
             (unsigned)seen->inside_rc, (unsigned)seen->inside_type);
    return passed;
}

bool ServiceJudgeDirList(const ServiceDirList *seen, const char *file, char *out,
                         size_t out_size) {
    const bool refused = seen->open_rc != 0u || seen->count_rc != 0u || seen->read_rc != 0u ||
                         seen->again_rc != 0u;
    const bool entry = seen->read == 1 && strncmp(seen->name, file, sizeof(seen->name)) == 0 &&
                       seen->type == 1 && seen->size == SERVICE_DIR_FILE_SIZE;
    const bool passed = !refused && seen->count == 1 && entry && seen->again == 0;
    char name[17];
    memcpy(name, seen->name, 16);
    name[16] = '\0';
    snprintf(out, out_size,
             "%s: open 0x%x; GetEntryCount 0x%x %lld; Read 0x%x %lld, \"%s\" type %d %lld bytes; "
             "again 0x%x %lld",
             refused            ? "refused"
             : seen->count != 1 ? "another count"
             : !entry           ? "not the file it holds"
             : passed           ? "its one file, once"
                                : "the file again",
             (unsigned)seen->open_rc, (unsigned)seen->count_rc, (long long)seen->count,
             (unsigned)seen->read_rc, (long long)seen->read, name, (int)seen->type,
             (long long)seen->size, (unsigned)seen->again_rc, (long long)seen->again);
    return passed;
}

bool ServiceJudgeDirDelete(uint32_t delete_rc, uint32_t gone_rc, char *out, size_t out_size) {
    const bool passed = delete_rc == 0u && gone_rc == SERVICE_PATH_NOT_FOUND;
    snprintf(out, out_size, "%s: DeleteDirectoryRecursively 0x%x; GetEntryType after 0x%x",
             delete_rc != 0u ? "refused"
             : passed        ? "gone with what it held"
                             : "still there",
             (unsigned)delete_rc, (unsigned)gone_rc);
    return passed;
}

/// A volume on the line: in thousandths, so a line never carries a float format.
static int Milli(float v) {
    return (int)(v * 1000.0f + (v < 0.0f ? -0.5f : 0.5f));
}

bool ServiceJudgeExpectedVolume(const ServiceExpectedVolume *seen, char *out, size_t out_size) {
    const bool refused = seen->before_rc != 0u || seen->set_rc != 0u || seen->after_rc != 0u ||
                         seen->restore_rc != 0u;
    const bool kept =
        seen->main_after == seen->main_asked && seen->library_after == seen->library_asked;
    const bool before =
        seen->main_after == seen->main_before && seen->library_after == seen->library_before;
    const bool passed = !refused && kept;
    snprintf(out, out_size,
             "%s: main %d/1000 library %d/1000 (0x%x), set to %d and %d (0x%x), read %d and %d "
             "(0x%x), put back 0x%x",
             refused  ? "refused"
             : passed ? "read back as set"
             : before ? "the volumes before read back"
                      : "other volumes read back",
             Milli(seen->main_before), Milli(seen->library_before), (unsigned)seen->before_rc,
             Milli(seen->main_asked), Milli(seen->library_asked), (unsigned)seen->set_rc,
             Milli(seen->main_after), Milli(seen->library_after), (unsigned)seen->after_rc,
             (unsigned)seen->restore_rc);
    return passed;
}

bool ServiceJudgeMainVolume(const ServiceMainVolume *seen, char *out, size_t out_size) {
    const bool refused = seen->change_rc != 0u || seen->after_rc != 0u || seen->rate_rc != 0u ||
                         seen->restore_rc != 0u;
    const bool passed = !refused && seen->after == seen->asked;
    snprintf(out, out_size,
             "%s: main %d/1000, changed to %d (0x%x), read %d (0x%x), transparent rate 0x%x, put "
             "back 0x%x",
             refused                       ? "refused"
             : passed                      ? "read back as changed"
             : seen->after == seen->before ? "the volume before read back"
                                           : "another volume read back",
             Milli(seen->before), Milli(seen->asked), (unsigned)seen->change_rc,
             Milli(seen->after), (unsigned)seen->after_rc, (unsigned)seen->rate_rc,
             (unsigned)seen->restore_rc);
    return passed;
}

bool ServiceJudgeUserChannel(const ServiceUserChannel *seen, char *out, size_t out_size) {
    const bool refused = seen->create_rc != 0u || seen->write_rc != 0u || seen->unpop_rc != 0u ||
                         (seen->pop_rc != 0u && seen->pop_rc != SERVICE_AM_NO_DATA) ||
                         seen->read_rc != 0u;
    const bool nothing = seen->pop_rc == SERVICE_AM_NO_DATA;
    const bool empty = seen->again_rc == SERVICE_AM_NO_DATA;
    const bool passed = !refused && !nothing && seen->same && empty;
    snprintf(out, out_size,
             "%s: create 0x%x, write 0x%x, unpop 0x%x, pop 0x%x, read 0x%x %s, pop again 0x%x",
             refused        ? "refused"
             : nothing      ? "the pop found nothing unpopped"
             : !seen->same  ? "the storage popped holds other bytes"
             : !empty       ? "the channel is not empty after the pop"
                            : "popped back with its bytes",
             (unsigned)seen->create_rc, (unsigned)seen->write_rc, (unsigned)seen->unpop_rc,
             (unsigned)seen->pop_rc, (unsigned)seen->read_rc, seen->same ? "same" : "other",
             (unsigned)seen->again_rc);
    return passed;
}

bool ServiceJudgeChannelOrder(const ServiceChannelOrder *seen, char *out, size_t out_size) {
    const bool refused = seen->first_rc != 0u || seen->second_rc != 0u ||
                         (seen->pop_rc != 0u && seen->pop_rc != SERVICE_AM_NO_DATA) ||
                         seen->read_rc != 0u || seen->clear_rc != 0u;
    const bool cleared = seen->after_rc == SERVICE_AM_NO_DATA;
    const bool passed = !refused && seen->pop_rc == 0u && seen->got == 2 && cleared;
    const char *verdict = "the last in came out first and the clear emptied it";
    if (refused) {
        verdict = "refused";
    } else if (seen->pop_rc == SERVICE_AM_NO_DATA) {
        verdict = "the pop found nothing unpopped";
    } else if (seen->got != 2) {
        verdict = "the first storage unpopped came out first";
    } else if (!cleared) {
        verdict = "the clear left a storage";
    }
    snprintf(out, out_size, "%s: unpop 0x%x and 0x%x, pop 0x%x gave %s, clear 0x%x, pop 0x%x",
             verdict,
             (unsigned)seen->first_rc, (unsigned)seen->second_rc, (unsigned)seen->pop_rc,
             seen->got == 2 ? "the second" : seen->got == 1 ? "the first" : "neither",
             (unsigned)seen->clear_rc, (unsigned)seen->after_rc);
    return passed;
}

bool ServiceJudgePreviousProgram(uint32_t rc, int32_t index, char *out, size_t out_size) {
    const bool passed = rc == 0u && index == -1;
    snprintf(out, out_size, "%s: GetPreviousProgramIndex 0x%x, %d",
             rc != 0u ? "refused" : passed ? "no program before" : "another index", (unsigned)rc,
             (int)index);
    return passed;
}
