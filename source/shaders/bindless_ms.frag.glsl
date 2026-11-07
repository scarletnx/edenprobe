// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pixel (x, y) fetches sample y of texel (0, 0) of the four sample texture handle x of the
// uniform array names, the handle read at a dynamic index.

#version 460
#extension GL_ARB_bindless_texture : require

layout (location = 0) out uvec4 outValue;

layout (std140, binding = 0) uniform Handles
{
    uvec4 h[4];
} handles;

void main()
{
    const ivec2 p = ivec2(gl_FragCoord.xy);
    outValue = texelFetch(usampler2DMS(handles.h[p.x].xy), ivec2(0, 0), p.y);
}
