// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every fragment stores marker plus its pixel index into storage buffer 0, for the cases where
// a later stage has to see what the fragments wrote. The colour output is
// opaque white.

#version 460

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Params
{
    uint marker;
    uint width;
} p;

layout (std430, binding = 0) buffer Out
{
    uint words[];
} o;

void main()
{
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    uint index = y * p.width + x;
    o.words[index] = p.marker + index;
    outColor = vec4(1.0);
}
