// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the fs family's big file holds, for every suite that reads it: the fs suite, which checks
/// each byte it reads, and the game scene, whose file thread streams from it. No libnx, so the
/// host build checks the scene's expected reads with the same arithmetic.

/// Where the fs family keeps its files on the card, as the file system service names them, and
/// the big file there: 64 MiB, written once by whichever suite finds it missing first.
#define FS_DIRECTORY "/edenprobe/fs"
#define FS_BIG_PATH FS_DIRECTORY "/big.bin"
#define FS_BIG_SIZE (64u * 1024u * 1024u)

/// Word @p index of the big file: (index + 1) * 2654435761 ^ 0x5EED1234, stored little endian.
static inline uint32_t FsRefPatternWord(uint64_t index) {
    return (uint32_t)(index + 1u) * 2654435761u ^ 0x5EED1234u;
}

/// Byte @p offset of the big file.
static inline uint8_t FsRefPatternByte(uint64_t offset) {
    return (uint8_t)(FsRefPatternWord(offset >> 2) >> ((offset & 3u) * 8u));
}

#ifdef __cplusplus
}
#endif
