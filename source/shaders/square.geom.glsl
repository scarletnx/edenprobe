// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Each point becomes a square of 8 by 8 pixels on a 64 by 64 target, centred on the point, in the
// point's colour; a point whose alpha is nought becomes nothing at all. The half size is a
// binary fraction of clip space, so the square's edges land on pixel edges exactly.

#version 460

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

layout (location = 1) in vec4 inColor[];

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

const float HALF = 0.125;

void main()
{
    if (inColor[0].a == 0.0) {
        return;
    }
    const vec4 centre = gl_in[0].gl_Position;
    const vec2 corners[4] = vec2[4](vec2(-HALF, -HALF), vec2(HALF, -HALF), vec2(-HALF, HALF),
                                    vec2(HALF, HALF));
    for (int i = 0; i < 4; ++i) {
        gl_Position = vec4(centre.xy + corners[i], centre.z, centre.w);
        outUv = vec2(0.0);
        outColor = inColor[0];
        EmitVertex();
    }
    EndPrimitive();
}
