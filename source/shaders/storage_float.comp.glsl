// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stores p.value at p.at through an image with no format qualifier: the store converts by the
// format of the image the descriptor names (a formatted store), which is what the storage suite
// asks of every float and normalised format.

#version 460

layout (local_size_x = 1) in;

layout (binding = 0) writeonly uniform image2D img;

layout (std140, binding = 0) uniform Params
{
    vec4 value;
    ivec4 at;
} p;

void main()
{
    imageStore(img, p.at.xy, p.value);
}
