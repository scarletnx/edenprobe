// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Every suite is one function that runs its cases and reports each one. They take no arguments
/// and return nothing: a suite that cannot run says so through a failed case, so a run always
/// produces a line per case whatever happened.
void RunSelfcheck(void);

#ifdef __cplusplus
}
#endif
