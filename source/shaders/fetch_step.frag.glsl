// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// texelFetch of every step-th texel from an offset: pixel (x, y) shows texel
// (x, y) * step + offset, held inside the texture. With a step of one and no offset it is
// fetch.frag; with a larger one a small target covers a large texture.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 0) uniform Step
{
    ivec4 size;   // the texture's width and height, then the step across and down
    ivec4 offset; // the first texel fetched, across and down
} s;

void main()
{
    ivec2 p = min(ivec2(gl_FragCoord.xy) * s.size.zw + s.offset.xy, s.size.xy - 1);
    outColor = texelFetch(tex, p, 0);
}
