// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A block of 64 invocations over 48 KiB of shared memory, the most a block of the console's GPU
// is given: each invocation writes every 64th word from its own index on, through the last word,
// then reads the same words back and folds them in order into one sum. No invocation reads
// another's words, so no barrier is needed; a machine that gives the block less memory, or wraps
// an address past what it gives, loses words the sum depends on.

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
    uint sum = 0u;
    for (uint i = 0u; i < 192u; ++i) {
        sum = sum * 31u + words[i * 64u + t];
    }
    o.sums[t] = sum;
}
