// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A point's own coordinate as its colour, red the x and green the y, for the fixed suite's note
// on which way up it runs: the console's PointCoordReplace register sets the origin, and deko3d
// leaves it at lower left.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(gl_PointCoord, 0.0, 1.0);
}
