// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A quad patch spread bilinearly over its four corners, so the tessellated triangles tile the
// corners' rectangle. The colour is the first corner's, with the outer level this stage was
// handed in the alpha, which says whether the control stage's levels arrived.

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
    outColor = vec4(tcColor[0].rgb, gl_TessLevelOuter[0] / 255.0);
}
