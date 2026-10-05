// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "settle.h"

static SettleStats run_stats;

void SettleFill(volatile uint8_t *slot, size_t bytes, uint8_t preset) {
    for (size_t i = 0; slot != NULL && i < bytes; ++i) {
        slot[i] = preset;
    }
}

bool SettleArrived(const volatile uint8_t *slot, size_t bytes, uint8_t preset) {
    if (slot == NULL || bytes == 0u) {
        return true;
    }
    for (size_t i = 0; i < bytes; ++i) {
        if (slot[i] != preset) {
            return true;
        }
    }
    return false;
}

void SettleRecord(SettleStats *stats, uint64_t waited_ns, bool arrived) {
    if (stats == NULL) {
        return;
    }
    stats->closes++;
    if (!arrived) {
        stats->lost++;
    } else if (waited_ns > 0u) {
        stats->late++;
    }
    if (waited_ns > stats->longest_ns) {
        stats->longest_ns = waited_ns;
    }
}

bool SettleDescribe(char *out, size_t out_size, const SettleStats *stats) {
    if (out == NULL || out_size == 0u) {
        return false;
    }
    out[0] = '\0';
    if (stats == NULL || (stats->late == 0u && stats->lost == 0u)) {
        return false;
    }
    // Rounded up, so a wait under a millisecond does not read as none at all.
    const unsigned long long longest_ms = (stats->longest_ns + 999999ull) / 1000000ull;
    int used = snprintf(out, out_size,
                        "readbacks: %u of %u device closes waited for a copy the machine "
                        "delivered after its queue was idle, the longest %llu ms",
                        (unsigned)stats->late + (unsigned)stats->lost, (unsigned)stats->closes,
                        longest_ms);
    if (stats->lost != 0u && used > 0 && (size_t)used < out_size) {
        snprintf(out + used, out_size - (size_t)used, "; %u never arrived within %llu ms",
                 (unsigned)stats->lost, (unsigned long long)(SETTLE_BUDGET_NS / 1000000ull));
    }
    return true;
}

SettleStats *SettleRun(void) {
    return &run_stats;
}
