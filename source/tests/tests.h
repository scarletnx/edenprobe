// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Every suite is one function that runs its cases and reports each one. They take no arguments
/// and return nothing: a suite that cannot run says so through a failed case, so a run always
/// produces a line per case whatever happened.
void RunSelfcheck(void);

#ifdef __SWITCH__
/// Needs a graphics device, so it exists only in the build that runs on the console. The host
/// makefile names its sources one at a time and this suite is not among them, which is the same
/// gate that keeps gpu.c out of that build.
void RunClear(void);
#endif

#ifdef __cplusplus
}
#endif
