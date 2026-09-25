// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copies p.count words from src to dst, but only after every invocation has spun p.spin times,
// and only from the last p.count invocations of the dispatch, which the hardware schedules last.
// So the copy lands as late as the dispatch allows, and a reader that is not ordered after the
// whole dispatch sees dst as it was before. The spin result reaches the output through p.zero,
// which is nought at run time, so the loop cannot be folded away and the words are exactly src.
// Follows the shape of deko3d's compute example (switch-examples, deko_examples, sinewave.glsl).

#version 460

layout (local_size_x = 64) in;

layout (std140, binding = 0) uniform Params
{
    uint count;
    uint spin;
    uint zero;
    uint pad;
} p;

layout (std430, binding = 0) readonly buffer Source
{
    uint words[];
} src;

layout (std430, binding = 1) buffer Destination
{
    uint words[];
} dst;

void main()
{
    uint id = gl_GlobalInvocationID.x;
    uint total = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    uint v = id * 2654435761u + 1u;
    for (uint k = 0u; k < p.spin; ++k) {
        v = v * 1664525u + 1013904223u;
    }
    if (id >= total - p.count) {
        uint i = id - (total - p.count);
        dst.words[i] = src.words[i] + (v & p.zero);
    }
}
