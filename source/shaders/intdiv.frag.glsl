// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integer arithmetic into an integer render target, as source/shaders/intmath.frag.glsl, for the
// modes 30, signed division of source/intref.h's IntMathMode. A shader of its own so an instruction
// an emulator cannot translate takes these cases down and no others; the other statement of the
// arithmetic is source/intref.c. The console's compiler has no integer divide to use and builds
// this out of a float reciprocal, which is why the operands are built the way source/intref.c's
// IntMathDividend explains.

#version 460

layout (location = 0) out uint outValue;

layout (std140, binding = 0) uniform Params
{
    uint mode;
    uint probe;
} params;

void main()
{
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    uint v = x * 1664525u + y * 1013904223u;
    uint w = y * 1664525u + x * 1013904223u;
    // The divisor, both signs, never nought or minus one, and a dividend that is x of them and a
    // remainder that is never nought, built without a division.
    int magnitude = int(y * 37u + 3u);
    int divisor = (y & 1u) != 0u ? -magnitude : magnitude;
    int remainder = 1 + int((((x * 29u + y * 13u) & 0x7Fu) * uint(magnitude - 2)) >> 7u);
    int dividend = int(x) * magnitude + remainder;
    dividend = (x & 2u) != 0u ? -dividend : dividend;
    uint result = 0xDEADBEEFu;
    switch (params.mode) {
        case 30u: result = uint(dividend / divisor); break;
    }
    outValue = result;
}
