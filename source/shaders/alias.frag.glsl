// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads a texel as one unsigned word and writes it back out as four bytes, least significant
// first, after rotating it right by four bits. The rotation is what makes the byte order
// matter: every output byte takes a nibble from two input bytes, so a word put together in any
// order but the little endian one, or converted rather than reinterpreted, comes out as
// different bytes. The other statement is CohAliasedTexel in source/coherref.c.

#version 460

layout (location = 0) in vec2 inUv;
layout (location = 1) flat in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform usampler2D source;

void main()
{
    uint word = texture(source, inUv).r;
    uint rotated = (word >> 4u) | (word << 28u);
    uvec4 bytes = uvec4(rotated, rotated >> 8u, rotated >> 16u, rotated >> 24u) & 255u;
    outColor = vec4(bytes) / 255.0;
}
