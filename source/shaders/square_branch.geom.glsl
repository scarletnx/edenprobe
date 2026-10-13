// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// square.geom with the hidden point's test turned round: the square is emitted inside the branch
// instead of after an early return. uam then compiles the branch with SSY and SYNC rather than
// PRET and RET, so a translator without PRET can still run it, and a failure here is about the
// geometry stage itself.

#version 460

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

layout (location = 1) in vec4 inColor[];

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

const float HALF = 0.125;

void main()
{
    if (inColor[0].a != 0.0) {
        const vec4 centre = gl_in[0].gl_Position;
        const vec2 corners[4] = vec2[4](vec2(-HALF, -HALF), vec2(HALF, -HALF),
                                        vec2(-HALF, HALF), vec2(HALF, HALF));
        for (int i = 0; i < 4; ++i) {
            gl_Position = vec4(centre.xy + corners[i], centre.z, centre.w);
            outUv = vec2(0.0);
            outColor = inColor[0];
            EmitVertex();
        }
        EndPrimitive();
    }
}
