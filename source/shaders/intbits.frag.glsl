// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Integer arithmetic into an integer render target, as source/shaders/intmath.frag.glsl, for the
// modes 32 to 42, the bit scans, the bitfields and the integer minimum, maximum and clamp of
// source/intref.h's IntMathMode. A shader of its own so an instruction an emulator cannot translate
// takes these cases down and no others; the other statement of the arithmetic is source/intref.c.

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
    // The minimum, maximum and clamp operands, as IntMathMinMaxOperands: on the last row the
    // ends of the signed range, nought and minus one, every pairing across the row.
    uint ends[4] = uint[4](0x80000000u, 0x7FFFFFFFu, 0u, 0xFFFFFFFFu);
    uint mv = y == 63u ? ends[x & 3u] : v;
    uint mw = y == 63u ? ends[(x >> 2u) & 3u] : w;
    int offset = int(x & 31u);
    int bits = int(min(y % 33u, 32u - (x & 31u)));
    uint result = 0xDEADBEEFu;
    switch (params.mode) {
        case 32u: result = uint(findMSB(v >> (x & 31u))); break;
        case 33u: result = uint(findMSB(int(v) >> (x & 31u))); break;
        case 34u: result = uint(findLSB(v << (x & 31u))); break;
        case 35u: result = bitfieldExtract(v, offset, bits); break;
        case 36u: result = uint(bitfieldExtract(int(v), offset, bits)); break;
        case 37u: result = bitfieldInsert(v, w, offset, bits); break;
        case 38u: result = bitfieldReverse(v); break;
        case 39u: result = uint(min(int(mv), int(mw))); break;
        case 40u: result = max(mv, mw); break;
        case 41u: result = uint(clamp(int(mv), -0x10000000, 0x20000000)); break;
        case 42u: result = clamp(mv, 0x40000000u, 0xC0000000u); break;
    }
    outValue = result;
}
