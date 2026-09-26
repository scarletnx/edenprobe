// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Writes word (y * width + x) of storage buffer 0 as the pixel's four bytes, so each pixel is
// the word the draw read for it (unpackUnorm4x8 into an RGBA8 target is exact).

#version 460

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Params
{
    uint marker;
    uint width;
    uint spin;
    uint zero;
} p;

layout (std430, binding = 0) readonly buffer In
{
    uint words[];
} b;

void main()
{
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    outColor = unpackUnorm4x8(b.words[y * p.width + x]);
}
