// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads texel p.at.xy of layer p.at.z from one R32F array image and stores it at the same place
// of another, through formatted loads and stores of that format.

#version 460

layout (local_size_x = 1) in;

layout (binding = 0, r32f) readonly uniform image2DArray src;
layout (binding = 1, r32f) writeonly uniform image2DArray dst;

layout (std140, binding = 0) uniform Params
{
    uvec4 unused;
    ivec4 at;
} p;

void main()
{
    imageStore(dst, p.at.xyz, imageLoad(src, p.at.xyz));
}
