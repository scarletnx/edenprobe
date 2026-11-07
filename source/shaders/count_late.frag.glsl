// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Counts its invocations in the first word of a storage buffer, and writes a depth of its own
// (the vertex colour's alpha), so the depth and stencil tests run after it: it runs for every
// fragment, the ones those tests then reject included.

#version 460

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
    atomicAdd(count.late, 1u);
    gl_FragDepth = inColor.a;
    outColor = vec4(inColor.rgb, 1.0);
}
