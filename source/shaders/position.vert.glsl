// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The position is the whole vertex, four floats read as they are, so vertex data another stage
// wrote is used without any interpretation of this shader's in between.

#version 460

layout (location = 0) in vec4 inPos;

void main()
{
    gl_Position = inPos;
}
