// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The stencil test and the stencil operations, worked out on the processor.
///
/// The numbering is deko3d's DkCompareOp and DkStencilOp, which the library hands to the 3D class
/// as it is, so a case passes these straight through. The rules are the specification's (OpenGL
/// 4.6, 17.3.5): the test compares the reference against the stored value, both under the
/// compare mask, so LESS passes when the reference is the smaller; INCR and DECR saturate at 255
/// and nought, the WRAP ones wrap; and the write mask decides which bits the result replaces.

typedef enum {
    STENCIL_NEVER = 1,
    STENCIL_LESS = 2,
    STENCIL_EQUAL = 3,
    STENCIL_LEQUAL = 4,
    STENCIL_GREATER = 5,
    STENCIL_NOTEQUAL = 6,
    STENCIL_GEQUAL = 7,
    STENCIL_ALWAYS = 8,
} StencilCompare;

typedef enum {
    STENCIL_KEEP = 1,
    STENCIL_ZERO = 2,
    STENCIL_REPLACE = 3,
    STENCIL_INCR = 4,
    STENCIL_DECR = 5,
    STENCIL_INVERT = 6,
    STENCIL_INCR_WRAP = 7,
    STENCIL_DECR_WRAP = 8,
} StencilOp;

/// The compare cases' three bands, 0x40, 0x80 and 0xc0 under a reference of 0x80: below,
/// equal and above, which gives each of the eight compares its own pattern of passes.
#define STENCIL_COMPARE_REF 0x80u
extern const uint8_t STENCIL_COMPARE_BANDS[3];

/// The op cases: GREATER against 0x80, so the band holding 0xa5 fails the stencil test and the
/// two holding 0x5a pass it, the second of them then failing the depth test. At these values
/// every op leaves a different byte, but for INCR and INCR_WRAP, and DECR and DECR_WRAP, which
/// only part at 255 and nought; the boundary cases part them.
#define STENCIL_OP_REF 0x80u
#define STENCIL_OP_FAILING 0xA5u
#define STENCIL_OP_PASSING 0x5Au

/// Whether the test passes: (@p ref & @p mask) against (@p stored & @p mask) under @p op.
bool StencilPasses(StencilCompare op, uint8_t ref, uint8_t stored, uint8_t mask);

/// The value @p op leaves where @p stored was, with @p ref the reference REPLACE writes and
/// @p write_mask the bits the result is allowed to replace.
uint8_t StencilApply(StencilOp op, uint8_t stored, uint8_t ref, uint8_t write_mask);

/// The names the cases use: "less", "incr_wrap".
const char *StencilCompareName(StencilCompare op);
const char *StencilOpName(StencilOp op);

#ifdef __cplusplus
}
#endif
