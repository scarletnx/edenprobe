// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Column x fetches texel x of an unsigned integer buffer texture and writes its channels folded
// into one word, the fold swizzle.frag.glsl uses.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform usamplerBuffer tex;

void main()
{
    uvec4 t = texelFetch(tex, int(gl_FragCoord.x));
    uint h = t.x ^ ((t.y << 8) | (t.y >> 24)) ^ ((t.z << 16) | (t.z >> 16)) ^
             ((t.w << 24) | (t.w >> 8));
    outColor = unpackUnorm4x8(h);
}
