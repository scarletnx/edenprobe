// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes the first word of uniform buffer 0 as the pixel's four bytes (unpackUnorm4x8 into an
// RGBA8 target is exact), so the pixel is the word the draw saw. RGBA8 rather than an integer
// target because integer targets are what the fork's ECFE-2026-0064/0065 fixed upstream.

#version 460

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Value
{
    uvec4 v;
} p;

void main()
{
    outColor = unpackUnorm4x8(p.v.x);
}
