// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// position.vert.glsl, plus the one texel of each of 32 textures, all nought, added to it. They
// are there for their descriptors: with ring.frag.glsl's 32 and its uniform buffer a draw holds
// 65, more than a host pushes per draw, so an emulator takes them from its descriptor ring.

#version 460

layout (location = 0) in vec4 inPos;

layout (binding = 0) uniform sampler2D tex[32];

void main()
{
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 32; ++i) {
        sum += texelFetch(tex[i], ivec2(0, 0), 0);
    }
    gl_Position = inPos + sum;
}
