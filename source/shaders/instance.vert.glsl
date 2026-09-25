// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each instance lands a quarter of the clip space width to the right of the one before, so the
// instances a draw really ran are read off the target region by region.

#version 460

layout (location = 0) in vec3 inPos;

void main()
{
    gl_Position = vec4(inPos.x + 0.5 * float(gl_InstanceID), inPos.y, inPos.z, 1.0);
}
