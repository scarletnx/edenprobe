// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The processor family's expected answers, worked out in plain C so the host build can pin them
/// before the console build compares the processor against them.

/// Leading zero bits of a lane, the lane's width for zero.
uint8_t CpuRefClz8(uint8_t value);
uint16_t CpuRefClz16(uint16_t value);
uint32_t CpuRefClz32(uint32_t value);

/// What the cpu suite feeds each lane width: every answer differs from its neighbours, and the
/// upper half of each register holds answers too, since a translator that loses half a
/// register is the failure these exist to catch.
extern const uint8_t CPU_CLZ8_INPUT[16];
extern const uint16_t CPU_CLZ16_INPUT[8];
extern const uint32_t CPU_CLZ32_INPUT[4];

#ifdef __cplusplus
}
#endif
