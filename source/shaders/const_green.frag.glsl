// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opaque green, from the code alone: no input, no uniform. Its twin, const_red.frag.glsl,
// compiles to the same control section and code of the same size, so writing one's code over
// the other's makes the program the other.

#version 460

layout (location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(0.0, 1.0, 0.0, 1.0);
}
