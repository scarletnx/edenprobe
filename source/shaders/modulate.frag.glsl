// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two colours into two normalised targets, for coverage modulation to scale.

#version 460

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outColor2;

layout (std140, binding = 0) uniform Params
{
    vec4 colour;
    vec4 colour2;
    uvec4 sentinel;
} p;

void main()
{
    outColor = p.colour;
    outColor2 = p.colour2;
}
