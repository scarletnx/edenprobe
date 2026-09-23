// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A different four bytes at every pixel, from the pixel's own coordinate and a seed, so an image
// this draws can be told apart from any other this suite draws and from itself shifted. The
// other statement of the same sums is CohPattern in source/coherref.c.
//
// The seed rides in the flat vertex red as seed / 255, which the normalised path hands back to
// the byte. Every sum is a whole number below 256 before the division, so the value written is
// that byte exactly, as it is for every flat colour the suites before this one draw.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main()
{
    uvec2 p = uvec2(gl_FragCoord.xy);
    uint k = uint(inColor.r * 255.0 + 0.5);
    uvec4 v = uvec4(4u * p.x + k, 4u * p.y + 3u * k, 4u * (p.x ^ p.y) + 5u * k,
                    p.x + p.y + 7u * k) & 255u;
    outColor = vec4(v) / 255.0;
}
