// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/// One lane of SQRSHL (signed) or UQRSHL (unsigned): the @p esize bit lane @p value shifted by
/// @p shift, the signed low byte of the shift operand's lane: left when positive, right with
/// rounding (half of the last bit shifted out added first) when negative, then saturated to the
/// lane (Arm ARM, SQRSHL and UQRSHL, and the shared SatQ functions). @p saturated says whether it
/// saturated, which is when QC is set.
uint64_t CpuRefRoundingShiftLane(uint64_t value, int shift, unsigned esize, bool is_unsigned,
                                 bool *saturated);

/// One lane of SQDMULL (@p accumulate 0), SQDMLAL (1) or SQDMLSL (-1): twice the product of the
/// signed @p esize bit lanes @p a and @p b, saturated to 2 * esize bits, then added to or taken
/// from the signed 2 * esize bit @p acc and saturated again.
uint64_t CpuRefDoublingMultiplyLane(uint64_t acc, uint64_t a, uint64_t b, unsigned esize,
                                    int accumulate, bool *saturated);

/// SQRSHRN, UQRSHRN or SQRSHRUN on one scalar: the 2 * @p esize bit @p value, read as unsigned
/// when @p source_unsigned, plus half of 2^shift, shifted right by @p shift (1 to esize) and
/// saturated to esize bits, unsigned when @p result_unsigned.
uint64_t CpuRefRoundingNarrow(uint64_t value, unsigned esize, unsigned shift, bool source_unsigned,
                              bool result_unsigned, bool *saturated);

/// What the rounding shift case feeds each lane width, 8 to 64 bits: two 64-bit halves of the
/// values, then two of the shifts. Every width has lanes that saturate both ways, round up, round
/// to zero and shift past the lane.
extern const uint64_t CPU_RSHL_VALUE[4][2];
extern const uint64_t CPU_RSHL_SHIFT[4][2];

/// The doubling multiply case's operands, as two 64-bit halves each: the multiplicands, the
/// multipliers and the accumulators. The halfword and word lanes hold the most negative value
/// against itself, which is the one product that saturates, and accumulators at the edges.
extern const uint64_t CPU_DMUL_A[2];
extern const uint64_t CPU_DMUL_B[2];
extern const uint64_t CPU_DMUL_ACC[2];

/// The narrowing case's scalars for halfword, word and doubleword sources, two each: one that
/// rounds, one that saturates.
extern const uint64_t CPU_RNARROW_VALUE[3][2];

/// One instruction of the NEON gap cases, in the order the case's assembly runs them: what the
/// reference needs to work out its answer. v0 and v1 are the sources and v2 the destination,
/// which holds the accumulator first for SQDMLAL and SQDMLSL.
typedef enum { CPU_GAP_KIND_RSHL, CPU_GAP_KIND_DMUL, CPU_GAP_KIND_RNARROW } CpuGapKind;
typedef struct {
    const char *text;   ///< the instruction as written in the case
    CpuGapKind kind;
    int op;             ///< RSHL: 1 unsigned; DMUL: 1 add, -1 subtract, 0 multiply only;
                        ///< RNARROW: 0 SQRSHRN, 1 UQRSHRN, 2 SQRSHRUN
    unsigned esize;     ///< source lane bits (the narrow side for DMUL and RNARROW)
    unsigned upper;     ///< the "2" forms: sources from the upper half
    int index;          ///< DMUL by element: the element of v1, else -1
    unsigned scalar;    ///< scalar form: lane 0 only, the rest of v2 zeroed
    unsigned shift;     ///< RNARROW: the immediate
    unsigned input;     ///< which row of the inputs v0 (and v1) come from
} CpuGapForm;

#define CPU_GAP_RSHL_FORMS 16u
#define CPU_GAP_DMUL_FORMS 26u
#define CPU_GAP_RNARROW_FORMS 18u
extern const CpuGapForm CPU_GAP_RSHL[CPU_GAP_RSHL_FORMS];
extern const CpuGapForm CPU_GAP_DMUL[CPU_GAP_DMUL_FORMS];
extern const CpuGapForm CPU_GAP_RNARROW[CPU_GAP_RNARROW_FORMS];

/// What @p form leaves in v2, as low and high halves, from sources @p v0 and @p v1 and v2's
/// previous value @p v2; @p qc says whether it saturated.
void CpuRefGapExpected(const CpuGapForm *form, const uint64_t v0[2], const uint64_t v1[2],
                       const uint64_t v2[2], uint64_t out[2], bool *qc);

/// The two cases that read system registers a game may reach from EL0 and that dynarmic does not
/// model: the may-crash table names them too.
#define CPU_CNTVCT_CASE "mrs cntvct_el0 reads a virtual count that moves with the physical one"
#define CPU_DAIF_CASE "mrs daif reads nothing outside the four interrupt mask bits"

/// The SM4 case's name, which the may-crash table names too.
#define CPU_SM4_CASE "sm4ekey and sm4e give the SM4 standard's worked example"

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
