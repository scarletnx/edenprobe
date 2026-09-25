// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Samples the texture whose handle sits at the pixel's column in a uniform array of handles
// (ARB_bindless_texture, which uam compiles to the console's bindless texture fetch), so column
// i shows texture i's one texel.

#version 460
#extension GL_ARB_bindless_texture : require

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Handles
{
    uvec4 h[256];
} handles;

void main()
{
    uint i = uint(gl_FragCoord.x);
    outColor = texelFetch(sampler2D(handles.h[i].xy), ivec2(0, 0), 0);
}
