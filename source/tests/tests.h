// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Every suite is one function that runs its cases and reports each one. They take no arguments
/// and return nothing: a suite that cannot run says so through a failed case, so a run always
/// produces a line per case whatever happened.
void RunSelfcheck(void);

/// The picture a failing case leaves beside the report. No hardware, so it runs in both builds,
/// and every line is the none tier because nothing here is compared to anything the graphics
/// unit produced.
void RunSupport(void);

/// The expected answers, checked without a graphics device: where clip space lands, which
/// pixels a triangle covers, what a blend or a texel address works out to. Runs in both builds,
/// because a reference that answered differently on the two processors would be blamed on the
/// graphics unit.
void RunReference(void);

#ifdef __SWITCH__
/// Needs a graphics device, so it exists only in the build that runs on the console. The host
/// makefile names its sources one at a time and this suite is not among them, which is the same
/// gate that keeps gpu.c out of that build.
void RunClear(void);

/// Where geometry lands: coverage, the fill rule, the viewport and the scissor. Everything
/// below it in the order draws, so it runs first among the suites that do.
void RunRaster(void);

/// Integer arithmetic in a fragment shader, into an integer target, compared bit for bit. The
/// suite that judges a shader translator.
void RunIntmath(void);

/// The fixed function blend unit: the equations, the factors, and the rounding back to eight
/// bits.
void RunBlend(void);

/// The depth test, the depth write and the depth buffer's own contents.
void RunDepth(void);

/// Texture sampling: the texel a coordinate names, the wrap modes, and a filter weight of
/// exactly one half.
void RunTexture(void);

/// Float arithmetic in a shader, judged in units in the last place. Last, because it is the only
/// suite where two correct machines are allowed to disagree.
void RunFmath(void);
#endif

#ifdef __cplusplus
}
#endif
