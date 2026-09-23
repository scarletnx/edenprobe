// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the fixed function blend unit is supposed to produce, worked out on the processor.
///
/// Stated in its own vocabulary rather than the graphics library's, so this file builds for the
/// machine you are sitting at. The blend suite translates each description below into the
/// library's state structures, so the two sides say the same thing in two languages.

typedef enum {
    /// source times its factor plus destination times its factor.
    BLEND_OP_ADD,
    /// source times its factor minus destination times its factor.
    BLEND_OP_SUBTRACT,
    /// the other way round.
    BLEND_OP_REVERSE_SUBTRACT,
    /// The smaller of the two, with no factors applied at all: the specification defines the
    /// minimum and maximum on the source and the destination directly, so an implementation
    /// that weights them first is wrong. Two cases set both factors to zero to say so.
    BLEND_OP_MIN,
    BLEND_OP_MAX,
} BlendOp;

typedef enum {
    BLEND_FACTOR_ZERO,
    BLEND_FACTOR_ONE,
    BLEND_FACTOR_SOURCE_ALPHA,
    BLEND_FACTOR_INVERSE_SOURCE_ALPHA,
    /// The constant colour the pass set, per channel. For the alpha channel that is the
    /// constant's own alpha.
    BLEND_FACTOR_CONSTANT,
} BlendFactor;

typedef struct {
    /// False means the source replaces the destination and nothing below is consulted.
    bool enabled;
    BlendOp color_op;
    BlendFactor color_source;
    BlendFactor color_destination;
    /// The alpha channel has its own equation and its own pair of factors, which is a thing
    /// implementations get wrong by wiring it to the colour one.
    ///
    /// Every case in the table sets @c alpha_destination to whatever @c color_destination holds,
    /// and the reference suite refuses one that does not, because the graphics library sends
    /// only one of the two. The cases reach the separate alpha state another way.
    BlendOp alpha_op;
    BlendFactor alpha_source;
    BlendFactor alpha_destination;
    float constant[4];
} BlendSpec;

/// One case: what to blend, over what, with which state.
typedef struct {
    const char *name;
    BlendSpec spec;
    uint8_t source[4];
    uint8_t destination[4];
    /// The sentence a reader wants next to the numbers: the arithmetic this case is claiming.
    const char *arithmetic;
} BlendCase;

size_t BlendCaseCount(void);
const BlendCase *BlendCaseAt(size_t index);

/// A normalised byte as the float the blend unit works on.
float BlendUnormToFloat(uint8_t value);

/// A float back to a normalised byte: clamped to the range, scaled, rounded to nearest.
uint8_t BlendFloatToUnorm(float value);

/// The four results, before rounding, on the nought to 255 scale.
///
/// Exposed so the reference suite can measure how far each one is from the halfway point between
/// two bytes, which is what makes the exact tier legal: the specification leaves the rounding of
/// a halfway value to the implementation.
void BlendReferenceScaled(const BlendSpec *spec, const uint8_t source[4],
                          const uint8_t destination[4], float out[4]);

/// The four bytes the target should hold afterwards.
void BlendReference(const BlendSpec *spec, const uint8_t source[4], const uint8_t destination[4],
                    uint8_t out[4]);

#ifdef __cplusplus
}
#endif
