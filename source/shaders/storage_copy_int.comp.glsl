// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// storage_copy.comp.glsl for RGBA8 signed integers, between two volumes.

#version 460

layout (local_size_x = 1) in;

layout (binding = 0, rgba8i) readonly uniform iimage3D src;
layout (binding = 1, rgba8i) writeonly uniform iimage3D dst;

layout (std140, binding = 0) uniform Params
{
    uvec4 unused;
    ivec4 at;
} p;

void main()
{
    imageStore(dst, p.at.xyz, imageLoad(src, p.at.xyz));
}
