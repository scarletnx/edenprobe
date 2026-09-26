// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// value_ubo.frag.glsl, plus the one texel of each of 32 textures, all nought, added to the
// pixel: the draw's word is the pixel, and the textures are there for their descriptors (see
// ring.vert.glsl). Adding nought changes no value unpackUnorm4x8 gives.

#version 460

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Value
{
    uvec4 v;
} p;

layout (binding = 0) uniform sampler2D tex[32];

void main()
{
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 32; ++i) {
        sum += texelFetch(tex[i], ivec2(0, 0), 0);
    }
    outColor = unpackUnorm4x8(p.v.x) + sum;
}
