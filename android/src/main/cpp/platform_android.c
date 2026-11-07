// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <android/log.h>

#include "platform.h"

// Android's side of platform.h: logcat for the log, bionic's sockets, which need nothing brought up
// (the manifest's INTERNET permission is what lets an app open one). No drawing surface yet: the
// graphics family here is the gles suite, Android's own.

void PlatformLog(const char *line, size_t length) {
    (void)length;
    __android_log_write(ANDROID_LOG_INFO, "edenprobe", line);
}

bool PlatformLogIsStandardOutput(void) {
    return false;
}

const char *PlatformError(void) {
    return "";
}

bool PlatformSocketsOpen(void) {
    return true;
}

void PlatformSocketsClose(void) {}
