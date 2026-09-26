// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

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

/// The journal, the crash list and what a launch decides from them: part of the support suite,
/// which hands it a directory of its own to write a file into and take it away again.
void RunSupportJournal(const char *scratch);

/// The time each drawing case reports and the perf line each drawing suite ends on: the sampling,
/// the arithmetic and the shape of both lines. Support cases, so it runs in both builds.
void RunSupportPerf(void);

/// The wait a device close makes for a copy delivered after the queue went idle: when a slot
/// counts as arrived, and the note a run carries when any close waited. Runs in both builds.
void RunSupportSettle(void);

/// The expected answers, checked without a graphics device: where clip space lands, which
/// pixels a triangle covers, what a blend or a texel address works out to. Runs in both builds,
/// because a reference that answered differently on the two processors would be blamed on the
/// graphics unit.
void RunReference(void);

/// The cpu suite's expected answers, pinned against numbers written by hand: part of the
/// reference suite, so both builds run it.
void RunReferenceCpu(void);

/// The raw method words the index and instance suites send, pinned against words worked out by
/// hand: part of the reference suite.
void RunReferenceRaw(void);

/// The ASTC reference decoder held to ARM's decoder block by block, and its weight table to
/// values worked out by hand: part of the reference suite.
void RunReferenceAstc(void);

/// The EAC reference decoder against values worked out by hand: part of the reference suite.
void RunReferenceEac(void);

/// The ETC2 colour reference decoder and its block builders held to Mesa's decoder block by
/// block, and to values worked out by hand: part of the reference suite.
void RunReferenceEtc2(void);

/// The bcn suite's array layout held to offsets worked out by hand from deko3d's layout rules,
/// and its solid BC4 block: part of the reference suite.
void RunReferenceBcn(void);
#ifdef __SWITCH__
/// The processor's own instructions, compared bit for bit with the reference. Console only,
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

/// Compute before a wait for idle, then a reader: vertices, indices, indirect arguments,
/// uniform and storage buffers, another dispatch. Each reader must see the written words.
void RunOrder(void);

/// Indexed draws whose index limit register holds the last byte of the indices, written with raw
/// methods: the last index must still be used.
void RunIndex(void);

/// The 3D class's instanced vertex array draws, sent as raw methods: each draws when it arrives,
/// with the state it arrived under, as the instance it is.
void RunInstance(void);

/// The optional stages: a geometry shader that turns points into squares or into nothing, and a
/// tessellated quad patch whose level reaches the evaluation stage. Every pixel exact.
void RunGeometry(void);

/// 256 textures reached through a uniform array of handles at a dynamic index: every index must
/// sample its own texture.
void RunBindless(void);

/// ASTC blocks, one per feature of the format, sampled texel by texel against the reference
/// decoder's answer.
void RunAstc(void);

/// One- and two-channel EAC blocks sampled texel by texel.
void RunEac(void);

/// ETC2 colour blocks, one per mode and punch-through rule, sampled texel by texel.
void RunEtc2(void);

/// BC4 arrays whose layers and small levels do not end on a block boundary, every texel of
/// every level and layer fetched.
void RunBcn(void);

/// Depth textures the processor writes, Z16, ZF32 and Z24S8, sampled texel by texel.
void RunZeta(void);

/// Block linear textures of every texel size and block height, written by the processor and
/// sampled back texel for texel; and a 4 MiB one for the time an upload takes.
void RunSwizzle(void);

/// Buffer textures a few bytes past an aligned address: each column must fetch its own texel.
void RunTexel(void);

/// 4096 draws in one list, each reading a uniform buffer of its own: 16 MiB, then 256 MiB of
/// uploads; every draw must see its own buffer.
void RunStream(void);

/// Turns on the stream suite's 256 MiB case, which the mode word `stress` asks for.
void StreamSetStress(bool on);

/// Fill rate for time: 128 quads over a 512 by 512 target, with and without a depth buffer
/// tested and written by each, the pixels and the depth judged too.
void RunFill(void);

/// 4096 indexed draws in one list, each with 65 descriptors and a uniform buffer of its own, so
/// an emulator's descriptor ring runs out several times in the list: every draw must land.
void RunRing(void);

/// The samples counter reported after each of 320 quads and each of 320 cleared cells: every
/// count must be the quad's pixels, and every quad and cell must be there.
void RunOcclusion(void);

/// DRAW_TEXTURE, sent as raw words between two draws: the draw after it must use its own
/// pipeline, viewport and scissor.
void RunDrawtex(void);
/// The samples passed counter, exact on quads of whole pixels, and the render enable reading
/// counters back: FALSE, TRUE, IF_EQUAL and IF_NOT_EQUAL over values from the processor and from
/// reports in the same list, over a draw and over a clear.
void RunQuery(void);

/// The copy engine copying a buffer 32 times in one list, from words the processor wrote and
/// then from words a dispatch wrote; every copy judged, the difference in time is the point.
void RunDma(void);

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
