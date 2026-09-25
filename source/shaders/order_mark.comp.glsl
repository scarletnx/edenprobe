// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// One word per workgroup: marker plus the group's index. What an indirect dispatch launched is
// read straight off the buffer, a group that never ran leaving its word as it was.

#version 460

layout (local_size_x = 1) in;

layout (std140, binding = 0) uniform Params
{
    uint marker;
} p;

layout (std430, binding = 0) buffer Marks
{
    uint words[];
} marks;

void main()
{
    marks.words[gl_WorkGroupID.x] = p.marker + gl_WorkGroupID.x;
}
