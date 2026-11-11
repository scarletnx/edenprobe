// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stores p.value at p.at.xy of slice p.at.z of a volume.

#version 460

layout (local_size_x = 1) in;

layout (binding = 0) writeonly uniform uimage3D img;

layout (std140, binding = 0) uniform Params
{
    uvec4 value;
    ivec4 at;
} p;

void main()
{
    imageStore(img, p.at.xyz, p.value);
}
