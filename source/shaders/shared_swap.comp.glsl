// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// shared_reach.comp.glsl with a barrier between the writes and the reads, which then fold the
// words the invocation at the other end wrote: the barrier as uam compiles one (BAR.SYNC with RZ
// for its barrier and its count), which has to hold every invocation until all have written.

#version 460

layout (local_size_x = 64) in;

shared uint words[12288];

layout (std430, binding = 0) buffer Out
{
    uint sums[64];
} o;

void main()
{
    uint t = gl_LocalInvocationID.x;
    for (uint i = 0u; i < 192u; ++i) {
        uint at = i * 64u + t;
        words[at] = at * 2654435761u + 12345u;
    }
    memoryBarrierShared();
    barrier();
    uint sum = 0u;
    for (uint i = 0u; i < 192u; ++i) {
        sum = sum * 31u + words[i * 64u + (63u - t)];
    }
    o.sums[t] = sum;
}
