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

typedef enum {
    SERVICE_OPUS_OPENS,   ///< hwopus: one decoder, 48 kHz stereo (none tier)
    SERVICE_OPUS_LIMITED, ///< hwopus: 24 live, the 25th refused
    SERVICE_OPUS_FREED,   ///< hwopus: one closed, the next one opens
    SERVICE_AM_OPENS,     ///< am: the application's functions and a user (none tier)
    SERVICE_SAVE_SIZE,    ///< am: ExtendSaveData, then GetSaveDataSize reads it back
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

#ifdef __cplusplus
}
#endif
