// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One lookup at the coordinate the uniform gives, the same for every pixel, at level nought:
// what the sampler and the texture's descriptor make of it (filter, reduction, normalised or
// texel coordinates, wrap), with nothing from the fragment's place in it.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 0) uniform At
{
    vec4 coord;
} at;

void main()
{
    outColor = textureLod(tex, at.coord.xy, 0.0);
}
