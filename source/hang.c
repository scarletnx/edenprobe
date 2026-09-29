// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <string.h>

#include "hang.h"

// Ryujinx 1.3.351 accepts SO_RCVTIMEO and then never returns from the receive. The rescue
// thread is queued on the core the receive holds, so it does not run either, and the launch
// stays there until something else kills it.
static const Hang TABLE[] = {
    { "bsd", HANG_BSD_TIMEOUT,
      "Ryujinx 1.3.351 accepts SO_RCVTIMEO and never returns from the receive, and the rescue "
      "thread is queued on the core that receive holds" },
};

size_t HangCount(void) {
    return sizeof(TABLE) / sizeof(TABLE[0]);
}

const Hang *HangAt(size_t index) {
    return index < HangCount() ? &TABLE[index] : NULL;
}

const Hang *HangFind(const char *suite, const char *name) {
    for (size_t i = 0; suite != NULL && name != NULL && i < HangCount(); ++i) {
        if (strcmp(TABLE[i].suite, suite) == 0 && strcmp(TABLE[i].name, name) == 0) {
            return &TABLE[i];
        }
    }
    return NULL;
}
