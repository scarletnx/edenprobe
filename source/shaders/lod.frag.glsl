// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One lookup per pixel of a 16 by 1 target, all at (13/32, 13/32), told by the uniform arrays
// what to do: textureGrad or textureGradOffset with the gradients in a, textureLod or
// textureLodOffset with the level in b.x; b.w is the mode. The offset is a constant, as the
// language wants it.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 0) uniform Lookups
{
    vec4 a[16];
    vec4 b[16];
} l;

void main()
{
    const uint i = uint(gl_FragCoord.x);
    const vec2 uv = vec2(13.0 / 32.0);
    const vec4 a = l.a[i];
    const vec4 b = l.b[i];
    const uint mode = uint(b.w);
    float r;
    if (mode == 0u) {
        r = textureGrad(tex, uv, a.xy, a.zw).x;
    } else if (mode == 1u) {
        r = textureGradOffset(tex, uv, a.xy, a.zw, ivec2(1, -1)).x;
    } else if (mode == 2u) {
        r = textureLod(tex, uv, b.x).x;
    } else {
        r = textureLodOffset(tex, uv, b.x, ivec2(1, -1)).x;
    }
    outColor = vec4(r, 0.0, 0.0, 1.0);
}
