// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: MIT
//
// Float arithmetic into a 32 bit float render target, one operation per draw, chosen by a number
// in a uniform buffer. The other statement of the same arithmetic is source/fmathref.c, in
// double precision, and the suite is the two having to agree within the specification's budget.
//
// Every operand and every argument below is exactly representable, so the processor can
// reproduce the shader's input rather than approximate it. The numbers in the switch are the
// FMathMode enumeration. The result is marked precise, which asks for the multiply and add to be
// fused; its case still allows a unit in the last place, because not fusing is legal.

#version 460

precise layout (location = 0) out float outValue;

layout (std140, binding = 0) uniform Params
{
    uint mode;
    float probe;
} params;

void main()
{
    // Whole numbers out of the fragment's own coordinate; the integer suite is where the half
    // the floor discards is checked. Multiplied by a sixty fourth rather than divided by 64,
    // which matters to the two cases that allow no tolerance at all: only the multiplication is
    // required to be correctly rounded.
    float a = (floor(gl_FragCoord.x) + 1.0) * (1.0 / 64.0);
    float b = (floor(gl_FragCoord.y) + 1.0) * (1.0 / 64.0);

    float result;
    switch (params.mode) {
        case 0u: result = params.probe; break;
        case 1u: result = a + b; break;
        case 2u: result = a * b; break;
        case 3u: result = fma(a, 1.0 / 3.0, b); break;
        case 4u: result = a / b; break;
        case 5u: result = sqrt(a); break;
        case 6u: result = exp2(a * 8.0 - 4.0); break;
        // A quarter, so every input is below one half and one budget covers the whole sweep:
        // the specification bounds the logarithm differently inside the range from a half to two.
        case 7u: result = log2(a * 0.25); break;
        // Six times a number at most one, less three, so the range is inside minus pi to pi,
        // which is the only range the specification bounds these two over.
        case 8u: result = sin(a * 6.0 - 3.0); break;
        default: result = cos(a * 6.0 - 3.0); break;
    }
    outValue = result;
}
