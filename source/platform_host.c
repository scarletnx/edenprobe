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
