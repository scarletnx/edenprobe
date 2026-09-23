// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Float arithmetic into a 32 bit float render target, one operation per draw, chosen by a number
// in a uniform buffer. The other statement of the same arithmetic is source/fmathref.c, in
// double precision, and the suite is the two having to agree within the specification's budget.
//
// Every operand and every argument below is exactly representable, so the processor can
// reproduce the shader's input rather than approximate it. The numbers in the switch are the
// FMathMode enumeration. The result is marked precise, which keeps the compiler from fusing or
// splitting what is written here; the console's binary has every fma as one fused FFMA and every
// float instruction flushing denormals, and the cases hold a machine to that.

#version 460

precise layout (location = 0) out float outValue;

layout (std140, binding = 0) uniform Params
{
    uint mode;
    float probe;
    // A NaN and an infinity, as bits, from the processor: out of a uniform buffer the compiler
    // cannot fold them away, and a literal infinity is not something the language can write.
    uint nanBits;
    uint infinityBits;
} params;

void main()
{
    // Whole numbers out of the fragment's own coordinate; the integer suite is where the half
    // the floor discards is checked. Multiplied by a sixty fourth rather than divided by 64,
    // which matters to the two cases that allow no tolerance at all: only the multiplication is
    // required to be correctly rounded.
    float a = (floor(gl_FragCoord.x) + 1.0) * (1.0 / 64.0);
    float b = (floor(gl_FragCoord.y) + 1.0) * (1.0 / 64.0);
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    uint row = y & 3u;
    float nan = uintBitsToFloat(params.nanBits);
    float inf = uintBitsToFloat(params.infinityBits);
    // The same arguments source/fmathref.c's FMathArgument forms, mode by mode.
    float edges = float(x) * 4.0 - 128.0 + float(y) * (1.0 / 16.0);
    // A full mantissa with its lowest bit set, for both logarithm cases, from shifts and
    // exclusive ors only, so this shader asks nothing of the integer multiplier.
    uint mantissa = ((x << 17u) ^ (y << 11u) ^ (x << 5u) ^ (y << 1u) ^ 0x5A5A5Au) & 0x7FFFFFu;
    mantissa |= 1u;
    float wide = uintBitsToFloat(((x * 4u + 1u) << 23u) | mantissa);
    float small = uintBitsToFloat(((118u + (x & 7u)) << 23u) | mantissa);
    float tiny = uintBitsToFloat(((x >> 1u) << 23u) | (y << 17u) | 1u);
    float sixtyfourths = float(x) - 32.0 + float(y) * (1.0 / 64.0);
    float quarters = float(x) - 32.0 + float(y & 3u) * 0.25;

    float result;
    switch (params.mode) {
        case 0u: result = params.probe; break;
        case 1u: result = a + b; break;
        case 2u: result = a * b; break;
        case 3u: result = fma(a, 1.0 / 3.0, b); break;
        case 4u: result = a / b; break;
        case 5u: result = sqrt(a); break;
        case 6u: result = exp2(a * 8.0 - 4.0); break;
        // Below one half, so one budget covers the whole sweep: the specification bounds the
        // logarithm differently inside the range from a half to two.
        case 7u: result = log2(small); break;
        // Six times a number at most one, less three, so the range is inside minus pi to pi,
        // which is the only range the specification bounds these two over.
        case 8u: result = sin(a * 6.0 - 3.0); break;
        case 9u: result = cos(a * 6.0 - 3.0); break;
        case 10u: {
            // The rounded product with its low twelve bits cleared: fused, the answer keeps the
            // product's rounding error; done apart, it does not.
            float product = a * (1.0 / 3.0);
            float cleared = uintBitsToFloat(floatBitsToUint(product) & 0xFFFFF000u);
            result = fma(a, 1.0 / 3.0, -cleared);
            break;
        }
        case 11u:
            result = row == 0u ? min(a, inf) : row == 1u ? max(a, -inf)
                   : row == 2u ? min(a, -inf) : max(-a, inf);
            break;
        case 12u:
            result = row == 0u ? min(a, nan) : row == 1u ? min(nan, a)
                   : row == 2u ? max(a, nan) : max(nan, -a);
            break;
        case 13u: {
            // The weights come from the row, not from literals, so no compiler can simplify
            // mix(a, y, 0.0) to a before the machine sees it.
            float weight = row == 0u ? 0.0 : row == 1u ? 0.25 : row == 2u ? 1.0 : nan;
            float far = row == 2u ? -inf : row == 3u ? b : inf;
            result = mix(a, far, weight);
            break;
        }
        case 14u:
            if (row == 0u) {
                result = (a * 2.71050543e-20) * 2.71050543e-20;
            } else if (row == 1u) {
                result = ((a * 2.71050543e-20) * 2.71050543e-20) * 1099511627776.0;
            } else if (row == 2u) {
                result = uintBitsToFloat(x + 1u) + 1.17549435e-38;
            } else {
                result = uintBitsToFloat((x + 1u) << 4u) * 8388608.0;
            }
            break;
        case 15u: result = exp2(edges); break;
        case 16u: result = log2(wide); break;
        case 17u: result = sqrt(tiny); break;
        case 18u: result = inversesqrt(tiny); break;
        case 19u: result = floor(sixtyfourths); break;
        case 20u: result = fract(sixtyfourths); break;
        case 21u: result = round(quarters); break;
        default: result = roundEven(quarters); break;
    }
    outValue = result;
}
