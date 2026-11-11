// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Passes each triangle on as it is, and stores words 0 to 15 of the storage buffer the vertex
// stage reads word 32 of: 0xc0de0000 plus the word's index, the same from every invocation.

#version 460

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

layout (location = 0) in vec2 inUv[];
layout (location = 1) flat in vec4 inColor[];

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

layout (std430, binding = 0) buffer Shared
{
    uint words[64];
} b;

void main()
{
    for (uint i = 0u; i < 16u; ++i) {
        b.words[i] = 0xC0DE0000u + i;
    }
    for (int v = 0; v < 3; ++v) {
        gl_Position = gl_in[v].gl_Position;
        outUv = inUv[v];
        outColor = inColor[v];
        EmitVertex();
    }
    EndPrimitive();
}
