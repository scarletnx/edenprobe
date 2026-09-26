// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Four control points, passed through, and every tessellation level taken from the first
// point's alpha: the level is the alpha's byte, rounded so it is a whole number exactly.

#version 460

layout (vertices = 4) out;

layout (location = 1) in vec4 inColor[];
layout (location = 1) out vec4 tcColor[];

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tcColor[gl_InvocationID] = inColor[gl_InvocationID];
    if (gl_InvocationID == 0) {
        const float level = round(inColor[0].a * 255.0);
        gl_TessLevelOuter[0] = level;
        gl_TessLevelOuter[1] = level;
        gl_TessLevelOuter[2] = level;
        gl_TessLevelOuter[3] = level;
        gl_TessLevelInner[0] = level;
        gl_TessLevelInner[1] = level;
    }
}
