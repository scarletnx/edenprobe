// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The value at the fragment's column of the buffer, as a float, clamped to nought and one: the
// clamp the compiler folds into the hardware's saturate (.SAT), which turns a NaN into +0 where
// GLSL's clamp of a NaN says nothing. Columns past the buffer's 32 words read its last.

#version 460

layout (location = 0) out vec4 outColor;

layout (std430, binding = 0) buffer In
{
    uint words[32];
} i;

void main()
{
    uint x = min(uint(gl_FragCoord.x), 31u);
    outColor = vec4(clamp(uintBitsToFloat(i.words[x]), 0.0, 1.0), 0.0, 0.0, 1.0);
}
