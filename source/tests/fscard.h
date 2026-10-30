// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the fs suite shares with the suites that read its files: apart from tests.h, which every
/// suite includes, since libnx's names would land in all of them.

/// Makes the fs family's 64 MiB file and its 256 small ones on @p fs unless they are there with
/// the right sizes, as the fs suite does before its first read. False with the reason in @p why.
bool FsPrepareCard(FsFileSystem *fs, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif
