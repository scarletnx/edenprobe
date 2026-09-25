// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Pixel (x, y) fetches texel (x, y) of an unsigned integer texture and writes its channels folded
// into one word (each rotated by a byte more than the one before, then exclusive or), so every
// bit of every channel reaches the pixel. Shifts and ors only: no multiply, so the fold itself
// leans on nothing a shader translator might lack.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform usampler2D tex;

layout (std140, binding = 0) uniform Size
{
    ivec4 size;
} s;

void main()
{
    ivec2 p = min(ivec2(gl_FragCoord.xy), s.size.xy - 1);
    uvec4 t = texelFetch(tex, p, 0);
    uint h = t.x ^ ((t.y << 8) | (t.y >> 24)) ^ ((t.z << 16) | (t.z >> 16)) ^
             ((t.w << 24) | (t.w >> 8));
    outColor = unpackUnorm4x8(h);
}
