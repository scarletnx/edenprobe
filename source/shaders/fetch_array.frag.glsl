// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// The target is a grid of cells, one column of cells per mip level and one row per layer, each
// 1 << g.x by 1 << g.y pixels. Pixel (x, y) of a cell fetches texel (x, y) of that level and
// layer of the array, clamped to the level's size, so every texel of every level and layer lands
// on a pixel. Pixels outside the grid are transparent black. Shifts and masks rather than
// division, which the console's shader compiler emulates in floating point.

#version 460

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2DArray tex;

layout (std140, binding = 0) uniform Grid
{
    ivec4 g; // log2 of the cell width and height, levels, layers
} s;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    int level = p.x >> s.g.x;
    int layer = p.y >> s.g.y;
    if (level >= s.g.z || layer >= s.g.w) {
        outColor = vec4(0.0);
        return;
    }
    ivec2 size = textureSize(tex, level).xy;
    ivec2 t = min(ivec2(p.x & ((1 << s.g.x) - 1), p.y & ((1 << s.g.y) - 1)), size - 1);
    outColor = texelFetch(tex, ivec3(t, layer), level);
}
