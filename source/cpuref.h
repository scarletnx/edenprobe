// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
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

/// What the cpu suite divides by zero: a 32-bit dividend, a zero, a 64-bit dividend, a zero. SDIV
/// and UDIV answer zero whatever the dividend (Arm ARM, SDIV: "If the divisor is zero, the result
/// is zero"), which is the whole expectation, so it takes no function.
extern const uint64_t CPU_DIV_INPUT[4];

/// Doubles, as bit patterns, that FRINTX rounds at FPCR's round to nearest, ties to even, and
/// that FRINTA rounds with ties away from zero. Every one has a fraction, so FRINTX raises
/// Inexact, and each set has a halfway case the two rules round apart.
extern const uint64_t CPU_FRINTX_INPUT[4];
extern const uint64_t CPU_FRINTA_INPUT[4];

/// A double rounded to a whole number with ties to even, or away from zero, as bit patterns and
/// in integer arithmetic only, so a translator that rounds wrongly cannot round the reference
/// wrongly too. @p inexact says whether the value had a fraction.
uint64_t CpuRefRoundDouble(uint64_t bits, bool ties_away, bool *inexact);

/// SQSHLU's inputs: an 8B register shifted by 3, a 4H register by 4, a 2D register by 0 and a
/// scalar D by 24, in that order, 2D as its low then its high half. Each vector has a lane that
/// saturates high or low; the scalar saturates nothing, so its QC stays clear.
extern const uint64_t CPU_SQSHLU_INPUT[5];
extern const uint8_t CPU_SQSHLU_SHIFT[4];

/// Each @p esize bit lane of @p lanes, signed, shifted left by @p shift and saturated to the
/// unsigned range of the lane, as SQSHLU does. @p saturated says whether any lane saturated,
/// which is when QC is set.
uint64_t CpuRefSqshlu(uint64_t lanes, unsigned esize, unsigned shift, bool *saturated);

/// Three 2D registers of doubles that FCVTZS and FCVTZU convert with 64 fraction bits: exact
/// values, a value that lands on the most negative integer, and two that saturate.
extern const uint64_t CPU_FCVTZ_INPUT[6];

/// A double times 2^64, rounded toward zero and saturated to a signed or unsigned 64-bit
/// integer, as FCVTZS and FCVTZU with 64 fraction bits give it; integer arithmetic only.
uint64_t CpuRefFcvtzFixed64(uint64_t bits, bool is_unsigned);

/// The worked example of the SM4 standard (GB/T 32907-2016, appendix A): the key
/// 0123456789abcdeffedcba9876543210 exclusive-or FK, the first four CK, and the same bytes as
/// plaintext, each as four 32-bit lanes, lane 0 first; then the round keys rk0 to rk3 SM4EKEY
/// gives and the words X4 to X7 SM4E gives, as the standard prints them.
extern const uint32_t CPU_SM4_KEY[4];
extern const uint32_t CPU_SM4_CK[4];
extern const uint32_t CPU_SM4_PLAIN[4];
extern const uint32_t CPU_SM4_RK[4];
extern const uint32_t CPU_SM4_X[4];

#ifdef __cplusplus
}
#endif
