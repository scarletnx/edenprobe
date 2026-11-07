// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A colour whose alpha alpha to coverage reads, into target 0, and the sentinel into the integer
// target 1, which coverage to color replaces with the coverage.

#version 460

layout (location = 0) out vec4 outColor;
layout (location = 1) out uvec4 outMask;

layout (std140, binding = 0) uniform Params
{
    vec4 colour;
    vec4 colour2;
    uvec4 sentinel;
} p;

void main()
{
    outColor = p.colour;
    outMask = p.sentinel;
}
