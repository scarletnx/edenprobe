// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT
//
// Integer arithmetic into an integer render target, one operation per draw, chosen by a number
// in a uniform buffer. The other statement of the same arithmetic is source/intref.c, written
// separately, and the suite is the two having to agree on every pixel.
//
// The operands come from the fragment's own coordinate, so every pixel is a different input. The
// numbers in the switch are the IntMathMode enumeration and mean nothing on their own: they are
// a contract between these two files. Nothing normalises or rounds on the way to an integer
// target, which is what makes the exact tier legal here.

#version 460

layout (location = 0) out uint outValue;

layout (std140, binding = 0) uniform Params
{
    uint mode;
    uint probe;
} params;

void main()
{
    // The coordinate names the centre of the pixel, so truncating gives the pixel's index. The
    // two cases below that compare the coordinate's bit pattern are what check that claim; every
    // line after them relies on it.
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    uint v = x * 1664525u + y * 1013904223u;

    uint result;
    switch (params.mode) {
        case 0u:  result = floatBitsToUint(gl_FragCoord.x); break;
        case 1u:  result = floatBitsToUint(gl_FragCoord.y); break;
        case 2u:  result = params.probe; break;
        case 3u:  result = v; break;
        case 4u:  result = v << 13u; break;
        case 5u:  result = v >> 7u; break;
        case 6u:  result = uint(int(v) >> 7); break;
        case 7u:  result = v & 0xF0F0F0F0u; break;
        case 8u:  result = v | 0x0F0F0F0Fu; break;
        case 9u:  result = v ^ 0xA5A5A5A5u; break;
        case 10u: result = uint(bitCount(v)); break;
        case 11u: result = uint(findLSB(v | 0x80000000u)) * 32u + uint(findMSB(v | 1u)); break;
        case 12u: result = v / 1000003u; break;
        default:  result = v % 1000003u; break;
    }
    outValue = result;
}
