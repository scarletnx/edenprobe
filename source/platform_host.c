// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>

#include "platform.h"

// The host build's side of platform.h: a PC, whose C library has sockets, and no drawing surface,
// since its steps run no draw suite.

void PlatformLog(const char *line, size_t length) {
    (void)length;
    puts(line);
}

bool PlatformLogIsStandardOutput(void) {
    return true;
}

const char *PlatformError(void) {
    return "";
}

bool PlatformSocketsOpen(void) {
    return true;
}

void PlatformSocketsClose(void) {}

size_t PlatformNetworkOff(char *out, size_t out_size) {
    // A PC's loopback works with every network down, so there is nothing to ask.
    if (out != NULL && out_size > 0u) {
        out[0] = '\0';
    }
    return 0;
}
