// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// One texture read and nothing else. Every case in the texture suite differs only in the
// sampler bound to this shader and in the coordinates the quad carries, so a failure is in the
// descriptors or in the texture's storage, and cannot be in the shader.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D source;

void main()
{
    outColor = texture(source, inUv);
}
