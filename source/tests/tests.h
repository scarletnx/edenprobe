// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Every suite is one function that runs its cases and reports each one. They take no arguments
/// and return nothing: a suite that cannot run says so through a failed case, so a run always
/// produces a line per case whatever happened.
void RunSelfcheck(void);

/// The two things this program does around a verdict rather than to reach one: the picture it
/// leaves beside a failing case, and the screen the detailed mode shows it on. No hardware, so
/// it runs in both builds, and every line is the none tier because nothing here is compared to
/// anything the graphics unit produced.
void RunSupport(void);

/// The detailed mode's screens: the index of the recorded cases, the navigation and the frames.
/// Part of the support suite, which runs it.
void RunSupportScreens(void);

/// The time each drawing case reports and the perf line each drawing suite ends on: the sampling,
/// the arithmetic and the shape of both lines. Support cases, so it runs in both builds.
void RunSupportPerf(void);

/// The expected answers, checked without a graphics device: where clip space lands, which
/// pixels a triangle covers, what a blend or a texel address works out to. Runs in both builds,
/// because a reference that answered differently on the two processors would be blamed on the
/// graphics unit.
void RunReference(void);

/// The cpu suite's expected answers, pinned against numbers written by hand: part of the
/// reference suite, so both builds run it.
void RunReferenceCpu(void);
#ifdef __SWITCH__
/// The processor's vector instructions, compared bit for bit with the reference. Console only,
/// because its cases are AArch64 instructions; no device, so it runs in every mode.
void RunCpu(void);
/// Needs a graphics device, so it exists only in the build that runs on the console. The host
/// makefile names its sources one at a time and this suite is not among them, which is the same
/// gate that keeps gpu.c out of that build.
void RunClear(void);

/// Every colour format a target can have, cleared, drawn over and read back. Right after clear,
/// which proves the one format everything else draws into.
void RunFormat(void);

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

/// The caches between draws, submits and copies: a texture rendered, rewritten by the processor
/// or by the copy engine and sampled again, a uniform buffer rewritten, two formats over one
/// memory, and a render target copied and read back. After texture, because it samples.
void RunCoherence(void);

/// Float arithmetic in a shader, judged in units in the last place. Last, because it is the only
/// suite where two correct machines are allowed to disagree.
void RunFmath(void);

/// The net family: nifm, dns, bsd, ssl, acc and friend, in that order, against whatever
/// sdmc:/edenprobe/net says the backend promises. A case that needs a backend the file does not
/// name, or a service this program cannot open, is skipped with the reason and counted apart.
void RunNet(void);
#endif

/// The net family's parsers and expectations, with no service: part of the reference suite.
void RunReferenceNet(void);

#ifdef __cplusplus
}
#endif
