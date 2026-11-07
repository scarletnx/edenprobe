// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// quad_color.tese.glsl that also passes on the clip distance the vertex stage wrote in slot 7
// as its own first: clip.vert.glsl makes that minus one, which clips the whole patch.

#version 460

layout (quads, equal_spacing, ccw) in;

in gl_PerVertex
{
    vec4 gl_Position;
    float gl_ClipDistance[8];
} gl_in[gl_MaxPatchVertices];

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_ClipDistance[1];
};

layout (location = 1) in vec4 tcColor[];

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

void main()
{
    const vec2 uv = gl_TessCoord.xy;
    const vec4 bottom = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, uv.x);
    const vec4 top = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, uv.x);
    gl_Position = mix(bottom, top, uv.y);
    gl_ClipDistance[0] = gl_in[0].gl_ClipDistance[7];
    outUv = uv;
    outColor = tcColor[0];
}
