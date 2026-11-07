// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sample s of pixel (x, y) of a four sample integer texture onto pixel (4 x + s, y), so a one
// sample target as wide as the texture holds every sample of its left quarter.

#version 460

layout (location = 0) out uvec4 outValue;

layout (binding = 0) uniform usampler2DMS tex;

void main()
{
    const ivec2 p = ivec2(gl_FragCoord.xy);
    outValue = texelFetch(tex, ivec2(p.x >> 2, p.y), p.x & 3);
}
