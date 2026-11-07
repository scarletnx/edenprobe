// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// quad_color.tese.glsl that also stores 0xa0, 0xa1 and 0xa2 into texels 0, 1 and 2 of a storage
// image while the fragment stage samples texel 3 of the same image. Every invocation stores the
// same three words, so which of them lands last decides nothing.

#version 460

layout (quads, equal_spacing, ccw) in;

layout (location = 1) in vec4 tcColor[];

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

layout (binding = 0, r32ui) uniform writeonly uimage2D img;

void main()
{
    const vec2 uv = gl_TessCoord.xy;
    const vec4 bottom = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, uv.x);
    const vec4 top = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, uv.x);
    gl_Position = mix(bottom, top, uv.y);
    for (int i = 0; i < 3; ++i) {
        imageStore(img, ivec2(i, 0), uvec4(0xA0u + uint(i)));
    }
    outUv = uv;
    outColor = tcColor[0];
}
