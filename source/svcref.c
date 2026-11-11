// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>

#include "svcref.h"

/// The kernel's module number in a result code's low nine bits.
#define KERNEL_MODULE 1u

uint32_t SvcCode(SvcResult result) {
    return result == SVC_SUCCESS ? 0u : KERNEL_MODULE | (uint32_t)result << 9;
}

static const struct {
    SvcResult result;
    const char *name;
} NAMES[] = {
    { SVC_INVALID_SIZE, "InvalidSize" },
    { SVC_INVALID_ADDRESS, "InvalidAddress" },
    { SVC_OUT_OF_MEMORY, "OutOfMemory" },
    { SVC_INVALID_CURRENT_MEMORY, "InvalidCurrentMemory" },
    { SVC_INVALID_HANDLE, "InvalidHandle" },
    { SVC_INVALID_POINTER, "InvalidPointer" },
    { SVC_INVALID_COMBINATION, "InvalidCombination" },
    { SVC_TIMED_OUT, "TimedOut" },
    { SVC_CANCELLED, "Cancelled" },
    { SVC_OUT_OF_RANGE, "OutOfRange" },
    { SVC_INVALID_ENUM_VALUE, "InvalidEnumValue" },
    { SVC_INVALID_STATE, "InvalidState" },
    { SVC_LIMIT_REACHED, "LimitReached" },
};

const char *SvcName(uint32_t code) {
    if (code == 0u) {
        return "Success";
    }
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); ++i) {
        if (SvcCode(NAMES[i].result) == code) {
            return NAMES[i].name;
        }
    }
    return NULL;
}

void SvcDescribe(uint32_t code, char *out, uint32_t size) {
    const char *const name = SvcName(code);
    if (name == NULL) {
        snprintf(out, size, "0x%x", (unsigned)code);
    } else if (code == 0u) {
        snprintf(out, size, "%s", name);
    } else {
        snprintf(out, size, "%s (0x%x)", name, (unsigned)code);
    }
}
