// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The NEON instructions dynarmic had no translation for, run in the order of the CpuGapForm
/// tables in cpuref.h: each stores v2 and FPSR after its instruction. Console build only.
#if defined(__aarch64__)
void CpuGapRoundingShifts(const uint64_t value[4][2], const uint64_t shift[4][2], uint64_t out[16][2],
                          uint64_t fpsr[16]);
void CpuGapDoublingMultiplies(const uint64_t a[2], const uint64_t b[2], const uint64_t acc[2],
                              uint64_t out[26][2], uint64_t fpsr[26]);
void CpuGapRoundingNarrows(const uint64_t value[3][2], uint64_t out[18][2], uint64_t fpsr[18]);
#endif

#ifdef __cplusplus
}
#endif
