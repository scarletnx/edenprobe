// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Integer arithmetic into an integer render target, as source/shaders/intmath.frag.glsl, for the
// modes 14 to 23, the conversions and the packing functions of source/intref.h's IntMathMode. A
// shader of its own so an instruction an emulator cannot translate takes these cases down and no
// others; the other statement of the arithmetic is source/intref.c.

#version 460

layout (location = 0) out uint outValue;

layout (std140, binding = 0) uniform Params
{
    uint mode;
    uint probe;
} params;

// The half precision pattern the pack and unpack modes use, as source/intref.c's
// IntMathHalfPatterns: an exponent of nought raised to one, a NaN made an infinity.
uint halfPatterns(uint v)
{
    uint result = 0u;
    for (uint part = 0u; part < 2u; ++part) {
        uint h = (v >> (part * 16u)) & 0xFFFFu;
        uint e = (h >> 10u) & 0x1Fu;
        if (e == 0u) {
            h |= 1u << 10u;
        } else if (e == 31u) {
            h &= ~0x3FFu;
        }
        result |= h << (part * 16u);
    }
    return result;
}

// Such a half, built into a float bit by bit so no conversion rounds it.
float floatFromHalf(uint h)
{
    uint sign = (h & 0x8000u) << 16u;
    uint e = (h >> 10u) & 0x1Fu;
    uint m = h & 0x3FFu;
    uint exponent = e == 31u ? 0x7F800000u : ((e + 112u) << 23u);
    return uintBitsToFloat(sign | exponent | (m << 13u));
}

void main()
{
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    uint v = x * 1664525u + y * 1013904223u;
    uint w = y * 1664525u + x * 1013904223u;
    uint result = 0xDEADBEEFu;
    switch (params.mode) {
        case 14u: result = floatBitsToUint(float(int(v))); break;
        case 15u: result = floatBitsToUint(float(v)); break;
        case 16u: result = uint(int((float(x) - 31.5) * 3.25 + float(y) * 0.125)); break;
        case 17u: result = uint(float(v >> 8u) * 256.0); break;
        case 18u: {
            vec4 c;
            for (uint i = 0u; i < 4u; ++i) {
                uint odd = ((x * 4u + y * 3u + i * 37u) % 200u) * 2u + 1u;
                c[i] = (float(odd) - 64.0) * (1.0 / 256.0);
            }
            result = packUnorm4x8(c);
            break;
        }
        case 19u: result = packUnorm4x8(unpackUnorm4x8(v)); break;
        case 20u: {
            vec2 c;
            for (uint i = 0u; i < 2u; ++i) {
                int odd = int((x * 5u + y * 7u + i * 101u) % 334u) * 2 - 333;
                c[i] = float(odd) * (1.0 / 256.0);
            }
            result = packSnorm2x16(c);
            break;
        }
        case 21u: result = packSnorm2x16(unpackSnorm2x16(v)); break;
        case 22u: {
            uint h = halfPatterns(v);
            result = packHalf2x16(vec2(floatFromHalf(h & 0xFFFFu), floatFromHalf(h >> 16u)));
            break;
        }
        case 23u: {
            vec2 u = unpackHalf2x16(halfPatterns(v));
            result = floatBitsToUint(u.x) ^ floatBitsToUint(u.y);
            break;
        }
    }
    outValue = result;
}
