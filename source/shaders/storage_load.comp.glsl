// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads texel p.at of an RGBA8 image through a formatted load and writes the four floats it
// gave, as their bits, into the buffer: what the load converted, to the last bit.

#version 460

layout (local_size_x = 1) in;

layout (binding = 0, rgba8) readonly uniform image2D img;

layout (std140, binding = 0) uniform Params
{
    uvec4 unused;
    ivec4 at;
} p;

layout (std430, binding = 0) buffer Out
{
    uint words[4];
} o;

void main()
{
    vec4 v = imageLoad(img, p.at.xy);
    o.words[0] = floatBitsToUint(v.x);
    o.words[1] = floatBitsToUint(v.y);
    o.words[2] = floatBitsToUint(v.z);
    o.words[3] = floatBitsToUint(v.w);
}
