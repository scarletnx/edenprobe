// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the integer fragment shader is supposed to produce, worked out on the processor.
///
/// This file and source/shaders/intmath.frag.glsl are two statements of the same arithmetic, one
/// for each machine, and the whole value of the suite is that they were written separately and
/// have to agree. The enumeration's numbers are the mode the shader switches on, so they are a
/// contract between the two files and cannot be reordered on one side alone.
typedef enum {
    /// The bit pattern of the fragment coordinate itself, one axis each. Not arithmetic: these
    /// two are what establish that the shader is being told where it is, including the half
    /// pixel that puts the coordinate at the centre rather than at the corner.
    INTMATH_FRAGCOORD_X = 0,
    INTMATH_FRAGCOORD_Y = 1,
    /// The value out of the uniform buffer, unaltered. The guard case: every mode below reads
    /// the mode out of that same buffer, so if this one is wrong none of them is measuring what
    /// its name says.
    INTMATH_UNIFORM = 2,
    /// A multiply and an add that both overflow 32 bits, which is the value every mode below
    /// starts from.
    INTMATH_MIX = 3,
    INTMATH_SHIFT_LEFT = 4,
    INTMATH_SHIFT_RIGHT = 5,
    /// The same shift on a signed value, which extends the sign bit instead of shifting in
    /// noughts. A translator that loses the distinction gets every negative input wrong and no
    /// positive one, which is why the sweep over a whole target matters here.
    INTMATH_SHIFT_ARITHMETIC = 6,
    INTMATH_AND = 7,
    INTMATH_OR = 8,
    INTMATH_XOR = 9,
    INTMATH_BIT_COUNT = 10,
    /// The lowest set bit times 32 plus the highest, so one value carries both scans.
    INTMATH_BIT_SCAN = 11,
    /// Division and remainder by a prime, which the hardware has no single instruction for and
    /// has to synthesise.
    INTMATH_DIVIDE = 12,
    INTMATH_REMAINDER = 13,
    INTMATH_MODE_COUNT = 14,
} IntMathMode;

/// The divisor the last two modes use. A prime near a million, so the quotient varies across the
/// whole target rather than being nought or one everywhere.
#define INTMATH_DIVISOR 1000003u

/// The answer for the pixel at @p x, @p y, with @p probe as whatever the uniform buffer carries.
uint32_t IntMathReference(IntMathMode mode, uint32_t x, uint32_t y, uint32_t probe);

/// The mixed value every arithmetic mode starts from, exposed so a self-check can state it.
uint32_t IntMathMix(uint32_t x, uint32_t y);

/// The name that goes on the case's line.
const char *IntMathName(IntMathMode mode);

#ifdef __cplusplus
}
#endif
