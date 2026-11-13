// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multiplies of two full words, the mix and its swap: the low word of the product, and the high
// word of the 64-bit product unsigned and signed. The other statement is source/intref.c. Every
// other multiply in these shaders has a coordinate under two to the 16 as one operand, so the high
// half of that operand never takes part in it.

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
    uint high;
    uint low;
    int signedHigh;
    int signedLow;
    uint result = 0xDEADBEEFu;
    switch (params.mode) {
        case 43u: result = v * w; break;
        case 44u: umulExtended(v, w, high, low); result = high; break;
        case 45u: imulExtended(int(v), int(w), signedHigh, signedLow); result = uint(signedHigh); break;
    }
    outValue = result;
}
