// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
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

bool PlatformLogIsStandardOutput(void) {
    return false;
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

size_t PlatformNetworkOff(char *out, size_t out_size) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) {
        return 0;
    }
    bool wireless = true;
    NifmInternetConnectionType type = 0;
    u32 strength = 0;
    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_Connected;
    const Result asked_wireless = nifmIsWirelessCommunicationEnabled(&wireless);
    const Result asked_status = nifmGetInternetConnectionStatus(&type, &strength, &status);
    nifmExit();
    // Eden answers wireless off in its flight mode, its default, and refuses every stream socket
    // then (bsd.cpp); a console's own answer in flight mode is not known here.
    if (R_SUCCEEDED(asked_wireless) && !wireless) {
        snprintf(out, out_size, "nifm says wireless communication is off, the flight mode");
    } else if (R_SUCCEEDED(asked_status) && status != NifmInternetConnectionStatus_Connected) {
        snprintf(out, out_size, "nifm says no connection is up (status %d)", (int)status);
    } else if (R_FAILED(asked_status)) {
        snprintf(out, out_size, "nifm has no connection status (0x%x)", (unsigned)asked_status);
    }
    return strlen(out);
}
