// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Waiting out a readback the machine delivers after its queue already said it was idle.
///
/// On the console a copy has landed by the time the queue is idle. An emulator at its fastest
/// accuracy may signal first and write the pixels from a thread of its own later; closing the
/// device before that write has landed took Eden's Android build down with the whole run
/// (README, "Android"). So a device is closed only after one last copy, of a target cleared to
/// zero onto a slot filled with SETTLE_PRESET, has been seen to land: copies land in the order
/// they were queued, so every earlier one has landed too. No libnx here, so it runs on the host.

/// What the slot holds until the copy lands. Not zero, which is every byte of the cleared pixel.
#define SETTLE_PRESET 0xa5u

/// How long a close waits for the copy, and how often it looks.
#define SETTLE_BUDGET_NS (250ull * 1000000ull)
#define SETTLE_POLL_NS (1000000ull)

typedef struct {
    uint32_t closes;  ///< devices closed with a usable queue
    uint32_t late;    ///< of them, how many had to wait for the copy
    uint32_t lost;    ///< of them, how many gave up after SETTLE_BUDGET_NS
    uint64_t longest_ns;
} SettleStats;

/// Fills @p bytes bytes of @p slot with @p preset.
void SettleFill(volatile uint8_t *slot, size_t bytes, uint8_t preset);

/// True once any byte of @p slot differs from @p preset, or when there is nothing to wait for.
bool SettleArrived(const volatile uint8_t *slot, size_t bytes, uint8_t preset);

/// Counts one close that waited @p waited_ns and saw its copy land or not.
void SettleRecord(SettleStats *stats, uint64_t waited_ns, bool arrived);

/// The note a run carries when any close had to wait, in @p out. False, and @p out empty, when
/// every copy had landed by the time the queue was idle: then the report says nothing.
bool SettleDescribe(char *out, size_t out_size, const SettleStats *stats);

/// The counts for this run's closes, kept by gpu.c.
SettleStats *SettleRun(void);

#ifdef __cplusplus
}
#endif
