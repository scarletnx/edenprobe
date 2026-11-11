// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The cpu suite's floating point rules: the reciprocal and square root estimates, the
/// reciprocal exponent, conversions to and from half precision under FPCR's AHP and DN, and the
/// order FMAX, FMIN, FMAXNM and FMINNM put NaNs and noughts in. Each is the Arm ARM's shared
/// pseudocode (DDI0487, shared/functions/float, Armv8.0: no FEAT_RPRES, no FEAT_AFP, as the
/// Cortex-A57 has it) worked out in integer arithmetic, with FPCR otherwise clear: round to
/// nearest, no flush to zero.
///
/// Touches no floating point, so the reference suite pins all of it on the host.

/// FPSR's cumulative flags the cases read: Invalid, divide by zero, overflow, underflow, inexact.
#define CPU_FPSR_IOC (1u << 0)
#define CPU_FPSR_DZC (1u << 1)
#define CPU_FPSR_OFC (1u << 2)
#define CPU_FPSR_UFC (1u << 3)
#define CPU_FPSR_IXC (1u << 4)
#define CPU_FPSR_FLAGS 0x1Fu

/// FPCR's alternative half precision and default NaN bits.
#define CPU_FPCR_AHP (1u << 26)
#define CPU_FPCR_DN (1u << 25)

/// RecipEstimate and RecipSqrtEstimate: @p a a fraction in 256ths to 512ths, the estimate in
/// 256..511 for 1.0 to 2.0.
uint32_t CpuRefRecipEstimate(uint32_t a);
uint32_t CpuRefRecipSqrtEstimate(uint32_t a);

/// URECPE and URSQRTE on one 32-bit lane.
uint32_t CpuRefUrecpe(uint32_t x);
uint32_t CpuRefUrsqrte(uint32_t x);

/// FRECPE, FRSQRTE and FRECPX on one single, the flags they raise ORed into @p fpsr.
uint32_t CpuRefFrecpe(uint32_t x, uint32_t *fpsr);
uint32_t CpuRefFrsqrte(uint32_t x, uint32_t *fpsr);
uint32_t CpuRefFrecpx(uint32_t x, uint32_t *fpsr);

/// FCVT of a single to half precision, and of a half to single, FPCR's AHP and DN as @p fpcr has
/// them.
uint16_t CpuRefToHalf(uint32_t x, uint32_t fpcr, uint32_t *fpsr);
uint32_t CpuRefFromHalf(uint16_t h, uint32_t fpcr, uint32_t *fpsr);

/// FMAX (@p max) or FMIN, or with @p number FMAXNM or FMINNM, of two singles.
uint32_t CpuRefMinMax(uint32_t a, uint32_t b, bool max, bool number, uint32_t *fpsr);

/// What the estimate cases feed each instruction: four registers of four singles, each register
/// one class of input so its flags say which lanes raised them.
extern const uint32_t CPU_FRECPE_INPUT[16];
extern const uint32_t CPU_FRSQRTE_INPUT[16];
/// URECPE's and URSQRTE's eight lanes, two registers.
extern const uint32_t CPU_UESTIMATE_INPUT[8];
/// FRECPX's eight lanes.
extern const uint32_t CPU_FRECPX_INPUT[8];
/// The singles FCVTN narrows under AHP, eight lanes, and the halves FCVTL widens under it.
extern const uint32_t CPU_AHP_SINGLES[8];
extern const uint16_t CPU_AHP_HALVES[8];
/// Four NaNs FCVTN narrows with DN clear, then set.
extern const uint32_t CPU_DN_NANS[4];
/// The pairs FMAX, FMIN, FMAXNM and FMINNM take, as two registers of first operands and two of
/// second ones, lane by lane.
extern const uint32_t CPU_MINMAX_A[8];
extern const uint32_t CPU_MINMAX_B[8];

#ifdef __cplusplus
}
#endif
