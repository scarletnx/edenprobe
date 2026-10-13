// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// quad.tese without reading the levels: the alpha is the first corner's own, which quad.tesc
// passes on and which holds the level the patch was drawn at. A translator that cannot read
// the tessellation levels in this stage can still run it, so a failure here is about the
// tessellation stages themselves.

#version 460

layout (quads, equal_spacing, ccw) in;

layout (location = 1) in vec4 tcColor[];

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

void main()
{
    const vec2 uv = gl_TessCoord.xy;
    const vec4 bottom = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, uv.x);
    const vec4 top = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, uv.x);
    gl_Position = mix(bottom, top, uv.y);
    outUv = uv;
    outColor = tcColor[0];
}
