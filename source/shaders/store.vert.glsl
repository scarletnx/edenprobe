// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// common.vert.glsl that also stores 0xb0 plus its vertex index mod 4 into texel (index mod 4, 0)
// of a storage image. A vertex shaded twice stores the same word twice.

#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUv;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

layout (binding = 0, r32ui) uniform writeonly uimage2D img;

void main()
{
    gl_Position = vec4(inPos, 1.0);
    imageStore(img, ivec2(gl_VertexID & 3, 0), uvec4(0xB0u + uint(gl_VertexID & 3)));
    outUv = inUv;
    outColor = inColor;
}
