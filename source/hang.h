// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The cases known to hang the program on a machine that gets them wrong, in one table:
/// skipped with the reason `hangs` unless the mode word `hang` asks for them.
///
/// A hang never ends the program, so the journal cannot mark it until something else kills
/// the process, and the mode word `retry` would run it again and hang again. The word `hang`
/// is the only way in: the start screen does not offer it, because a pad that arrives late
/// reports held buttons as one edge and must not be able to ask for a hang.
#define HANG_BSD_TIMEOUT "a receive timeout gives up with EAGAIN within its time"

typedef struct {
    const char *suite;
    const char *name;
    /// Why it hangs, and where that was seen.
    const char *why;
} Hang;

size_t HangCount(void);
const Hang *HangAt(size_t index);

/// The entry for a case, or NULL when the table does not name it.
const Hang *HangFind(const char *suite, const char *name);

#ifdef __cplusplus
}
#endif
