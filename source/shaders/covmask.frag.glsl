// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sentinel word into an integer target. With coverage to color on, the coverage takes the
// red component's place, so a pixel holding the sentinel drew without it.

#version 460

layout (location = 0) out uvec4 outMask;

layout (std140, binding = 0) uniform Params
{
    vec4 colour;
    vec4 colour2;
    uvec4 sentinel;
} p;

void main()
{
    outMask = p.sentinel;
}
