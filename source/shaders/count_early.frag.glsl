// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Counts its invocations in the second word of a storage buffer, with the depth and stencil
// tests asked to run before it: it runs only for the fragments they let through.

#version 460

layout (early_fragment_tests) in;

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout (std430, binding = 0) buffer Count
{
    uint late;
    uint early;
} count;

void main()
{
    atomicAdd(count.early, 1u);
    outColor = vec4(inColor.rgb, 1.0);
}
