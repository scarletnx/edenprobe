// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// fetch_step.frag at a level the uniform names: pixel (x, y) shows texel (x, y) * step + offset
// of that level, held inside it. The descriptor covers every level, so the shader alone decides
// which one is read.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 0) uniform Step
{
    ivec4 size;   // the level's width and height, then the step across and down
    ivec4 offset; // the first texel fetched, across and down, then the level
} s;

void main()
{
    ivec2 p = min(ivec2(gl_FragCoord.xy) * s.size.zw + s.offset.xy, s.size.xy - 1);
    outColor = texelFetch(tex, p, s.offset.z);
}
