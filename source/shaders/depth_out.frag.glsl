// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The vertex colour, and a depth of its own taken from the vertex colour's alpha, so the depth
// test has to wait for the shader: the stencil suite draws one depth at the vertices and writes
// another here.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main()
{
    gl_FragDepth = inColor.a;
    outColor = vec4(inColor.rgb, 1.0);
}
