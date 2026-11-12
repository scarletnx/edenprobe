// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The two ways the console's frontend asks the services a machine may answer in a game's own
/// code instead (nn::fs, nn::time, nn::pl, nn::ngc and nn::nsd, sdk.h), as the mode file's word
/// `hooks` chooses (docs/ModeFile.md):
///
///   IPC    the program opens fsp-srv and time:u at its start, as libnx does, and the cases send
///          the services their commands: an emulator, or a recompiler that serves the services.
///   hooks  the program opens neither: the memory card is nn::fs's, through the SDK's functions
///          the program exports (the C library's sdmc: device too), and the cases ask those
///          functions. A machine that takes a game's SDK functions by their symbols takes these;
///          where none does, their own code sends the IPC mode's requests.
///
/// The mode file is read before either: through nn::fs's functions, which answer in both.

/// Where the run leaves its files on the machine it ran on, and where the mode file is.
#define PROBE_DIRECTORY "sdmc:/edenprobe"

/// Whether the program started in hooks mode.
bool HooksOn(void);

/// The support suite's case that says which way the run asks the services, and what answered the
/// SDK's functions when it asks through them.
void RunSupportMode(void);

/// In hooks mode, a report note naming the exported functions whose own code answered a call, with
/// how many: the ones the machine left to the program. Nothing in IPC mode.
void HooksReportOwnCode(void);

#ifdef __cplusplus
}
#endif
