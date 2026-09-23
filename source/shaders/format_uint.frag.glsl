// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The format suite's quad: four words out of the uniform buffer, written unaltered to a target
// whose format takes a uvec4. No arithmetic, so what reaches memory is the target's conversion and
// nothing else. The words are the bits of the value, so the processor chose every one of them.

#version 460

layout (location = 0) out uvec4 outColor;

layout (std140, binding = 0) uniform Params
{
    uvec4 bits;
} params;

void main()
{
    outColor = params.bits;
}
