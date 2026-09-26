// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The cases known to end the program on a machine that gets them wrong, in one table: skipped
/// with the reason `may crash` unless the mode word `fatal` or the start screen asks for them.
///
/// A machine that ends inside one of them goes on with the run after it all the same (see the
/// journal); the table is for not asking at all, since a case a known defect ends costs a
/// relaunch on every machine that has the defect, and on a console a trip back to the menu.
typedef struct {
    const char *suite;
    const char *name;
    /// The family the case's suite belongs to, one of the MODE_FAMILY bits: the start screen
    /// lists only the cases of the families a run has.
    uint32_t family;
    /// Why it may end the program, and where that was seen.
    const char *why;
} MayCrash;

size_t MayCrashCount(void);
const MayCrash *MayCrashAt(size_t index);

/// The entry for a case, or NULL when the table does not name it.
const MayCrash *MayCrashFind(const char *suite, const char *name);

#ifdef __cplusplus
}
#endif
