// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One point per vertex of each instance, at the pixel its number names on a 16 by 16 target (the
// vertex's number plus eight per instance, row by row), carrying the attribute as the vertex
// fetched it. The position comes from the numbers alone, so a vertex fetched from the wrong place
// shows as a wrong value at its own pixel, never as a point somewhere else.

#version 460

layout (location = 0) in vec4 inValue;

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

void main()
{
    uint id = uint(gl_VertexID) + uint(gl_InstanceID) * 8u;
    gl_Position = vec4((float(id % 16u) + 0.5) / 8.0 - 1.0, 1.0 - (float(id / 16u) + 0.5) / 8.0,
                       0.0, 1.0);
    outUv = vec2(0.0);
    outColor = inValue;
}
