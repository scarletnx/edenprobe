// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <switch.h>

#include "platform.h"

// The console's side of platform.h. The drawing surface is not here: the console's drawing cases
// are its own graphics family, on deko3d, and its steps run no draw suite.

static char error[160];

void PlatformLog(const char *line, size_t length) {
    // The debug string service: an emulator logs it, and a console ignores it unless a debugger
    // listens, which is why every line also goes to the report file.
    svcOutputDebugString(line, length);
}

const char *PlatformError(void) {
    return error;
}

bool PlatformSocketsOpen(void) {
    const Result opened = socketInitializeDefault();
    if (R_FAILED(opened)) {
        snprintf(error, sizeof(error),
                 "socketInitializeDefault() returned 0x%x: bsd cannot be opened from this program",
                 (unsigned)opened);
        return false;
    }
    return true;
}

void PlatformSocketsClose(void) {
    socketExit();
}
