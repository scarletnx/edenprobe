// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// common.vert.glsl with all eight clip distances written: the eighth is minus one and the other
// seven one, so a stage after it that passes the eighth on clips everything and one that passes
// the first clips nothing.

#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUv;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_ClipDistance[8];
};

void main()
{
    gl_Position = vec4(inPos, 1.0);
    for (int i = 0; i < 8; ++i) {
        gl_ClipDistance[i] = i == 7 ? -1.0 : 1.0;
    }
    outUv = inUv;
    outColor = inColor;
}
