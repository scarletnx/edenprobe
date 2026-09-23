// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The only vertex shader in the suite, so a broken vertex stage fails everything at once rather
// than once per suite.
//
// The colour is flat, not interpolated: a value interpolated from three equal vertices is a sum
// of three weighted copies whose weights only have to add to one within rounding, and an exact
// tier case cannot rest on that.

#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUv;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 outUv;
layout (location = 1) flat out vec4 outColor;

void main()
{
    gl_Position = vec4(inPos, 1.0);
    outUv = inUv;
    outColor = inColor;
}
