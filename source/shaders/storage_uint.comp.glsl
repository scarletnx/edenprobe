// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// storage_float.comp.glsl for the unsigned integer formats.

#version 460

layout (local_size_x = 1) in;

layout (binding = 0) writeonly uniform uimage2D img;

layout (std140, binding = 0) uniform Params
{
    uvec4 value;
    ivec4 at;
} p;

void main()
{
    imageStore(img, p.at.xy, p.value);
}
