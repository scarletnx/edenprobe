// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// value_ubo.frag.glsl, but the word comes from an entry of uniform buffer 0 picked by the
// pixel's position, among the first `count` (uniform buffer 1): the draw can read any of them,
// so an emulator has to hand the shader the whole buffer rather than the first sixteen bytes.
// Every entry of a buffer holds the same word, so the pixel is that word whichever is read.

#version 460

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Values
{
    uvec4 v[4096];
} p;

layout (std140, binding = 1) uniform Count
{
    uint count;
} n;

void main()
{
    uint i = (uint(gl_FragCoord.x) * 7u + uint(gl_FragCoord.y) * 13u) & (n.count - 1u);
    outColor = unpackUnorm4x8(p.v[i].x);
}
