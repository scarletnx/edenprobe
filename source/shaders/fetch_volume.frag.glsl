// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texel (x, y) of slice nought of a volume, for the pixel at (x, y) of a 4x4 target: the first
// block of a block compressed volume, texel for texel.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler3D tex;

void main()
{
    outColor = texelFetch(tex, ivec3(ivec2(gl_FragCoord.xy) & 3, 0), 0);
}
