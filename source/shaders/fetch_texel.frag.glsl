// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The low byte of texel (n, 0) of an R32_Uint texture as the red channel, n from the vertex
// colour's red (the texel index over 255): the geometry suite's stage image cases read the
// texels a stage before stored, or one it left alone.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform usampler2D tex;

void main()
{
    const int n = int(round(inColor.r * 255.0));
    const uint v = texelFetch(tex, ivec2(n, 0), 0).x;
    outColor = vec4(float(v & 0xFFu) / 255.0, 0.0, 0.0, 1.0);
}
