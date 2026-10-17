// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The binder suite's judge: the reply to transaction 17 of the buffer producer
/// (GetBufferHistory), read from the raw parcel the call wrote, sorted into the layouts known
/// to answer it, and judged. No service here, so both builds run it and the reference suite
/// pins it on replies made to order.
///
/// Two layouts are known, both reden's: the status alone, a word of nought, which is what it
/// answers unless enable_buffer_history is on (layout A); and with the option, the status, a
/// count n, n entries of 32 bytes (frame number, queue time, presentation time, state, and four
/// of padding) newest first, and the status again (layout B). A request of nought or less is
/// refused there with BadValue (-22) and a count of nought before the closing status. Which one
/// the console answers is Q1: a reply in neither layout fails its case with its bytes.

#define BINDER_TRANSACTION_HISTORY 17u
#define BINDER_REPLY_MAX 0x400u
#define BINDER_ENTRY_SIZE 32u
#define BINDER_ENTRIES_MAX 16u
#define BINDER_BAD_VALUE (-22)

typedef enum {
    BINDER_STATUS_ONLY, ///< 4 bytes, nought
    BINDER_HISTORY,     ///< status nought, count, entries, status nought
    BINDER_REFUSED,     ///< BadValue, nought, status nought
    BINDER_UNKNOWN,
} BinderLayout;

typedef struct {
    BinderLayout layout;
    /// The parcel header: payload and objects, each a size and an offset.
    uint32_t payload_size, payload_off, objects_size, objects_off;
    bool framed; ///< the header points inside the reply
    const uint8_t *payload;
    uint32_t count;
    uint64_t frame[BINDER_ENTRIES_MAX];
    int64_t queued[BINDER_ENTRIES_MAX];
    int64_t presented[BINDER_ENTRIES_MAX];
    uint32_t state[BINDER_ENTRIES_MAX];
} BinderReply;

/// Reads the reply parcel @p raw of @p size bytes, as the call left it, header first.
void BinderParse(const uint8_t *raw, size_t size, BinderReply *out);

/// In the order they run on one layer, whose frames add up: none, then three, then ten.
typedef enum {
    BINDER_ZERO,         ///< nothing queued, nought asked for
    BINDER_AFTER_THREE,  ///< three frames queued, eight asked for
    BINDER_NEWEST_FIRST, ///< ten frames queued, eight asked for
    BINDER_CASES
} BinderCaseId;

const char *BinderCaseName(BinderCaseId id);
/// What the case queues and asks for.
uint32_t BinderCaseFrames(BinderCaseId id);
int32_t BinderCaseRequest(BinderCaseId id);

/// The verdict on case @p id from its raw reply (whose bytes the line always carries, for Q1),
/// and in @p answers the bit of the layout it fits (bit 0 A, bit 1 B).
bool BinderJudge(BinderCaseId id, const uint8_t *raw, size_t size, char *out, size_t out_size,
                 uint32_t *answers);

#ifdef __cplusplus
}
#endif
