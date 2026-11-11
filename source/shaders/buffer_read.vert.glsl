// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// common.vert.glsl, but the colour is word 32 of a storage buffer, its bytes red to alpha: the
// vertex stage reading one buffer the geometry stage of the same draw writes elsewhere in.

#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUv;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

layout (std430, binding = 0) readonly buffer Shared
{
    uint words[64];
} b;

void main()
{
    gl_Position = vec4(inPos, 1.0);
    outUv = inUv;
    outColor = unpackUnorm4x8(b.words[32]);
}
