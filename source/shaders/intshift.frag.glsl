// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integer arithmetic into an integer render target, as source/shaders/intmath.frag.glsl, for the
// modes 24 to 29, the shifts by a count in a register of source/intref.h's IntMathMode. A shader of
// its own so an instruction an emulator cannot translate takes these cases down and no others; the
// other statement of the arithmetic is source/intref.c. The language leaves a count of 32 or more
// undefined. The console's compiler emits these as shift instructions without the wrap flag, which
// clamp the count to 32; source/intref.c says so.

#version 460

layout (location = 0) out uint outValue;

layout (std140, binding = 0) uniform Params
{
    uint mode;
    uint probe;
} params;

void main()
{
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    uint v = x * 1664525u + y * 1013904223u;
    uint w = y * 1664525u + x * 1013904223u;
    // Shift counts in registers: 31, 32 and 33 across the row, and every count down the column.
    uint wide = 31u + x % 3u;
    uint result = 0xDEADBEEFu;
    switch (params.mode) {
        case 24u: result = v << wide; break;
        case 25u: result = v >> wide; break;
        case 26u: result = uint(int(v) >> wide); break;
        case 27u: result = v << y; break;
        case 28u: result = v >> y; break;
        case 29u: result = uint(int(v) >> y); break;
    }
    outValue = result;
}
