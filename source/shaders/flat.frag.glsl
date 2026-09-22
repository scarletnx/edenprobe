// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One colour, straight from the vertex, with no arithmetic anywhere in the fragment stage. Used
// by every case that is about where pixels land rather than what is computed in them, so a
// failure in those suites cannot be a shader result.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main()
{
    outColor = inColor;
}
