// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One colour, read out of a uniform buffer and written unchanged. The coherence suite rewrites
// that buffer between draws, so the only question a wrong pixel can answer is which version of
// the buffer the draw saw.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Colour
{
    vec4 colour;
} params;

void main()
{
    outColor = params.colour;
}
