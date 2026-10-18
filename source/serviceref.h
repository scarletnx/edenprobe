// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The service family's judge: what each case saw a service answer, made into a verdict and the
/// line that says it. No service here, so both builds run it and the reference suite pins it on
/// answers made to order.
///
/// Every expectation is reden's (the commit is named beside it), since no console has run these
/// cases yet; the open question each one leaves is in docs/families/Service.md.

/// A result as the console writes it: the module, and the description nine bits up.
#define SERVICE_RESULT(module, description) ((uint32_t)(module) | ((uint32_t)(description) << 9))

/// hwopus refusing a decoder past its limit: ResultOutOfOpusDecoders, HwOpus (111) 385. reden
/// 00a1c392e7 read the limit, 24 live decoders, and the result from the firmware's
/// nn::codec::InitializeHardwareOpusDecoder, which falls back to the software decoder on it.
#define SERVICE_OPUS_REFUSED SERVICE_RESULT(111, 385)
#define SERVICE_OPUS_LIMIT 24u
/// Two past the limit, so a machine with no limit at all shows it with room to spare.
#define SERVICE_OPUS_TRIES (SERVICE_OPUS_LIMIT + 2u)

/// What the save size case extends both parts of the account save by, on top of what it reads
/// first: a whole number of the save's 16 KiB blocks, small enough for any memory card.
#define SERVICE_SAVE_STEP 0x100000u

/// The file system's answer for a path that is not there: Fs (2) 1, PathNotFound.
#define SERVICE_PATH_NOT_FOUND SERVICE_RESULT(2, 1)

/// set refusing a language past its table: Settings (105) 625, ResultInvalidLanguage (reden
/// settings_server.cpp). The most codes GetAvailableLanguageCodes2 can list, 0x40, is reden's
/// POST_4_0_0_MAX_ENTRIES.
#define SERVICE_INVALID_LANGUAGE SERVICE_RESULT(105, 625)
#define SERVICE_LANGUAGES_MAX 0x40u

typedef enum {
    SERVICE_OPUS_OPENS,   ///< hwopus: one decoder, 48 kHz stereo (none tier)
    SERVICE_OPUS_LIMITED, ///< hwopus: 24 live, the 25th refused
    SERVICE_OPUS_FREED,   ///< hwopus: one closed, the next one opens
    SERVICE_AM_OPENS,     ///< am: the application's functions and a user (none tier)
    SERVICE_SAVE_SIZE,    ///< am: ExtendSaveData, then GetSaveDataSize reads it back
    SERVICE_TEMP_OPENS,   ///< fsp: the temporary storage opens (none tier)
    SERVICE_TEMP_EMPTY,   ///< fsp: what the launch before left there is gone
    SERVICE_ACC_OPENS,    ///< account: acc:u0 and its user count (none tier)
    SERVICE_SELECT_USER,  ///< account: 52 answers as 51 does
    SERVICE_LDN_OPENS,    ///< ldn: ldn:u opens (none tier)
    SERVICE_LDN_MONITOR,  ///< ldn: CreateClientProcessMonitor and its RegisterClient
    SERVICE_SET_OPENS,    ///< set: set opens and lists its language codes (none tier)
    SERVICE_SET_LANGUAGE, ///< set: GetLanguageCode is one of them
    SERVICE_SET_MAKE,     ///< set: MakeLanguageCode gives the list's code at each index
    SERVICE_SET_COUNT,    ///< set: GetAvailableLanguageCodeCount2 leaves room for the list
    SERVICE_CASES
} ServiceCaseId;

/// The suite a case belongs to and its name, as its line carries them.
const char *ServiceCaseSuite(ServiceCaseId id);
const char *ServiceCaseName(ServiceCaseId id);

/// The decoders the limit case opened, in order: @p results[i] is what the (i + 1)th open
/// answered, @p tried how many it asked for, stopping at the first refusal or at
/// SERVICE_OPUS_TRIES. Passes when the first 24 opened and the 25th was refused with
/// SERVICE_OPUS_REFUSED.
bool ServiceJudgeOpusLimit(const uint32_t *results, uint32_t tried, char *out, size_t out_size);

/// After the limit case's refusal and one live decoder closed: @p reopened is what the next open
/// answered. Passes when it opened.
bool ServiceJudgeOpusFreed(uint32_t reopened, char *out, size_t out_size);

/// ExtendSaveData (25) and GetSaveDataSize (26) of IApplicationFunctions, both for the account
/// save of one user: what 26 said before, what 25 was asked and answered, and what 26 said after.
/// Passes when 25 succeeded and 26 then reads back the sizes 25 was asked for, reden d2279cbf8d's
/// pair (the user read at byte 8 of both requests, the size kept); the required size 25 hands
/// back is on the line, not judged.
typedef struct {
    uint32_t before_rc, extend_rc, after_rc;
    uint64_t before_normal, before_journal;
    uint64_t asked_normal, asked_journal;
    uint64_t required;
    uint64_t after_normal, after_journal;
} ServiceSaveSize;
bool ServiceJudgeSaveSize(const ServiceSaveSize *seen, char *out, size_t out_size);

/// The temporary storage at the start of a launch. @p promised is the marker the launch before
/// wrote there, as it said on the memory card (never nought: with no promise there is nothing to
/// look for, and the caller skips the case); @p found_rc what opening the marker answered and
/// @p found what it held when it opened. Passes when the marker is not there, PathNotFound, which
/// is reden f3af5d0c25's rule: the storage is emptied once at each launch of the application.
bool ServiceJudgeTempEmpty(uint64_t promised, uint32_t found_rc, uint64_t found, char *out,
                           size_t out_size);

/// acc:u0's TrySelectUserWithoutInteractionDeprecated (51) and TrySelectUserWithoutInteraction
/// (52, 19.0.0+), both asked with no network account required, and how many users there are.
/// Passes when 52 answers what 51 answers, reden 87e5d0b6e4's two handlers: the user when there
/// is exactly one, an id of nought otherwise. A machine that says it is older than 19.0.0 may
/// refuse 52 instead, as a console of that age would, and passes that way too.
typedef struct {
    uint32_t rc51, rc52;
    uint64_t uid51[2], uid52[2];
    int32_t users;
    bool firmware19;
} ServiceSelectUser;
bool ServiceJudgeSelectUser(const ServiceSelectUser *seen, char *out, size_t out_size);

/// ldn:u's CreateClientProcessMonitor (1, 18.0.0+): what it answered and the handle of the object
/// it handed out, and what that object's RegisterClient (0, with this process's id) answered.
/// Passes when both succeeded on a handle that is not nought, which is what libnx's ldnInitialize
/// asks on 18.0.0 and later and reden 23585820bc answers; a machine that says it is older than
/// 18.0.0 may refuse 1 instead.
typedef struct {
    uint32_t create_rc, register_rc;
    uint32_t handle;
    bool registered; ///< RegisterClient was asked
    bool firmware18;
} ServiceLdnMonitor;
bool ServiceJudgeLdnMonitor(const ServiceLdnMonitor *seen, char *out, size_t out_size);

/// set's language codes: what GetAvailableLanguageCodes2 (5) listed, GetLanguageCode (0), what
/// MakeLanguageCode (2) made of each index of the list and of the first index past it, and
/// GetAvailableLanguageCodeCount2 (6). reden lists its table, answers the console's language from
/// it, makes each index into the table's code and refuses one past it with
/// SERVICE_INVALID_LANGUAGE, and counts the most the list can hold.
typedef struct {
    uint32_t list_rc, language_rc, past_rc, count_rc;
    int32_t listed;
    uint64_t codes[SERVICE_LANGUAGES_MAX];
    uint64_t language;
    uint32_t made_rc[SERVICE_LANGUAGES_MAX];
    uint64_t made[SERVICE_LANGUAGES_MAX];
    int32_t count;
} ServiceLanguages;
/// The code as text: up to eight characters, '?' for a byte that is not printable.
void ServiceLanguageText(uint64_t code, char out[9]);
bool ServiceJudgeLanguage(const ServiceLanguages *seen, char *out, size_t out_size);
bool ServiceJudgeMakeLanguage(const ServiceLanguages *seen, char *out, size_t out_size);
bool ServiceJudgeLanguageCount(const ServiceLanguages *seen, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
