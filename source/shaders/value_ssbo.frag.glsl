// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Writes the first word of storage buffer 0 as the pixel's four bytes, as value_ubo does.

#version 460

layout (location = 0) out vec4 outColor;

layout (std430, binding = 0) readonly buffer Value
{
    uint words[];
} b;

void main()
{
    outColor = unpackUnorm4x8(b.words[0]);
}
