// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The ETC2 colour blocks the etc2 suite samples, one per mode and per punch-through rule, built
/// from their fields by source/etc2ref.c's builders (Etc2BuildTestBlock), and what Mesa's decoder
/// (26.1.6, src/mesa/main/texcompress_etc.c) makes of each. `kind` is 0 for RGB8, 1 for RGB8A1
/// and 2 for RGBA8, whose first eight bytes are the alpha block; `hash` is FNV-1a over Mesa's
/// sixteen RGBA8 texels, row major. The reference suite holds both the builders and etc2ref.c
/// to these, block by block.
typedef struct {
    const char *name;
    const char *sentence;
    uint32_t kind;
    uint8_t bytes[16];
    uint32_t hash;
} Etc2TestBlock;

#define ETC2_TEST_BLOCKS 10u

extern const Etc2TestBlock ETC2_TEST_BLOCK[ETC2_TEST_BLOCKS];

/// Builds test block @p index from its fields into @p bytes (eight bytes, sixteen for RGBA8).
void Etc2BuildTestBlock(uint32_t index, uint8_t bytes[16]);

#ifdef __cplusplus
}
#endif
