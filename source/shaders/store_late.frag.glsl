// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// store_ssbo's store made late: every fragment spins p.spin times, then stores marker plus its
// pixel index into storage buffer 0. The spin reaches the word through p.zero, nought at run
// time, so it cannot be folded away and the word is exactly marker plus index. A reader that is
// not ordered after these fragments sees the buffer as it was. The colour output is opaque
// white.

#version 460

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Params
{
    uint marker;
    uint width;
    uint spin;
    uint zero;
} p;

layout (std430, binding = 0) buffer Out
{
    uint words[];
} o;

void main()
{
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    uint index = y * p.width + x;
    uint v = index * 2654435761u + 1u;
    for (uint k = 0u; k < p.spin; ++k) {
        v = v * 1664525u + 1013904223u;
    }
    o.words[index] = p.marker + index + (v & p.zero);
    outColor = vec4(1.0);
}
