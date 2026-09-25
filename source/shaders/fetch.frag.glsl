// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pixel (x, y) fetches texel (x, y) of texture 0, clamped to the size in the uniform buffer, so
// a compressed block's texels land one per pixel in the target's corner.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 0) uniform Size
{
    ivec4 size;
} s;

void main()
{
    ivec2 p = min(ivec2(gl_FragCoord.xy), s.size.xy - 1);
    outColor = texelFetch(tex, p, 0);
}
