# The processor

- **cpu**, thirty eight cases, on the processor and in every mode, since it needs no device. Three count
  leading zeros over the sixteen 8-bit, eight 16-bit and four 32-bit lanes of one vector register,
  loaded from memory so a translator cannot fold the answer away, and compared exactly with the
  reference. The 16-bit case is there because a translator that widened the lanes to count them
  lost the upper half on hosts with AVX-512: lanes four to seven came back as 0xfff0.

  Six more are defects a fuzzer found in Eden's translator, run against Unicorn (reden's dynarmic
  tests), each checked against what the architecture defines: SDIV and UDIV by zero give zero, on
  32 and 64 bits; FRINTX rounds to nearest with ties to even and raises Inexact; FRINTA rounds a
  halfway value away from zero; SQSHLU saturates 8, 16 and 64-bit lanes and sets QC, FPSR cleared
  and read around each shift; FCVTZS and FCVTZU with 64 fraction bits scale by 2^64 and saturate;
  and SM4EKEY and SM4E give the SM4 standard's worked example. The expected values come from
  references in integer arithmetic, never from the processor's floating point, which is what is
  being judged.

  One more calls 48 functions the translator has never seen, each a single straight-line block of
  160 operations between loading and storing seventeen general and sixteen vector registers: more
  live values than an x86-64 host has registers, so a translator's register allocator spills, and
  a mistake in its bookkeeping shows as a wrong word. The operations are drawn by a fixed
  pseudo-random sequence from add, subtract, the logical operations, shifts and rotates, multiply,
  multiply-add and high multiply, ADDS followed by CSEL on each of fourteen conditions, CLZ, and
  vector add, multiply, shifts and logic with moves between the two register files.
  `tools/jit-blocks.py` writes the functions and the state each must leave
  (`source/jitblocks.c`), from the architecture's rules restated in Python, sharing no code with
  the program; Unicorn agreed with all 48 states when they were written. Each block is called
  twice: the first call is where an emulator compiles it, the second only runs it, and a note
  says what both took and what compiling cost a block, here under Eden's x86-64 build:

  ```json
  {"type":"note","text":"cpu compile: 48 blocks of 160 operations, first calls 14993 us, second calls 2 us, 312316 ns a block to compile"}
  ```

  The note is for comparing two builds of one emulator on one machine; the verdict is the state.
  On the console the two calls differ by a cold instruction cache and nothing more.

  Three cover base AdvSIMD instructions dynarmic has no translation for, found by sampling the
  instruction groups recompiled games execute and keeping what a Cortex-A57 runs (Unicorn's model)
  and dynarmic refuses: SQRSHL and UQRSHL on every lane width and as scalars, sixteen instructions;
  SQDMLAL, SQDMLSL and SQDMULL as vectors, by element, in their upper-half forms and as scalars,
  twenty six; SQRSHRN, UQRSHRN and SQRSHRUN as scalars, eighteen. Each stores its register and
  FPSR, checked against references in integer arithmetic (`source/cpuref.c`), with inputs that
  saturate in 35 of the 60 and round in the rest. Eden logs the exception and skips the
  instruction, so the program goes on with the register never written: the cases fail there
  rather than end the program. The instructions sit in `source/cpugaps.c`, apart from the case,
  so their machine code can be run on Unicorn against the same references before a console sees
  them.

  Seven hold the floating point rules a translator emulates in software or hands to a host whose
  own instructions differ, each lane against the Arm ARM's pseudocode worked out in integer
  arithmetic (`source/cpufpref.c`, Armv8.0 as the Cortex-A57 has it), and FPSR's five cumulative
  flags read after every register, so a lane that raised the wrong one is told apart:
  FRECPE and FRSQRTE on four registers of singles, normal, at the edges of the exponent (2^127
  and 2^126, whose estimates are denormal; denormals under and over 2^-128, where FRECPE overflows
  to an infinity), noughts, infinities, negative values and NaNs quiet and signalling; URECPE
  and URSQRTE, all ones below their ranges; FRECPX, which has only a scalar form, the largest
  exponent for noughts and denormals; FCVTN and FCVTL with FPCR.AHP set, where exponent 31 is a
  number (0x7c00 is 65536), a value past 131008 or an infinity saturates to 0x7fff with Invalid and
  no Inexact, and a NaN becomes nought; FCVTN of four NaNs with DN clear, keeping each payload's
  top nine bits, and set, giving 0x7e00; and FMAX, FMIN, FMAXNM and FMINNM on eight pairs: a quiet
  NaN against a number either way round, a signalling NaN against a quiet one either way (the
  signalling one wins, quieted, even second), the two noughts both ways (+0 the maximum, -0 the
  minimum), denormals of either sign and an infinity. The references agreed with Unicorn's
  AArch64 on 122,510 inputs, flags included, before a console saw them.
  ScarletNX fa431b5 passes all seven. Under Eden 6a9235c22d five pass: FRSQRTE raises Inexact on
  the normal register, which the pseudocode never does (`common/fp/op/FPRSqrtEstimate.cpp`, or the
  host's own estimate behind it), and FCVTN of a signalling NaN to half leaves Invalid clear
  (`common/fp/op/FPConvert.cpp`).

  Sixteen more are what ScarletNX's translator tests hold its code to, asked of the processor. SSHL
  and USHL, SRSHL and URSHL, SQSHL and UQSHL by a register, every lane width as vectors and the
  scalars each has, sixty forms in three cases, on the gap cases' inputs and on two more rows of
  64-bit lanes shifted by 64 and -64, 127 and -128, bits above the low byte that count for nothing;
  against `CpuRefShiftLane` in `source/cpuref.c`, QC included. CRC32B to CRC32X and CRC32CB to
  CRC32CX on four accumulators and data words, against the bit by bit definition (`CpuRefCrc32`),
  which the judge holds to the standard check values. FRINTN, FRINTP, FRINTM, FRINTZ and FRINTA on
  sixteen singles (halves either side of an even and an odd integer both ways, noughts under a half
  of both signs, which keep their sign, 2^23 - 0.5 and 2^23 + 1, the least denormal, infinity, NaNs
  quiet and signalling with payloads), then FRINTI and FRINTX under each of FPCR's four rounding
  modes, FRINTX alone raising Inexact (`CpuRefRoundSingle` in `source/cpufpref.c`). FMADD and FMSUB
  on rows worked out by hand from FPMulAdd: a product exact only when fused ((1 + 2^-23)(1 - 2^-24)
  - 1 is 2^-24 - 2^-47, where rounding the product first gives nought), an inexact one, 2 max - max
  with no overflow midway, and the signs of nought; and the NaN rules, where nought times infinity
  plus a quiet NaN is the default NaN with Invalid, not the quiet NaN, and a signalling addend wins
  over a quiet multiplicand. FCMEQ, FCMGE and FCMGT with NaN lanes false and Invalid raised by a
  signalling NaN alone for equality and by any NaN for the ordered compares. FADD, FSUB, FMUL and
  FDIV on four lanes at once, one a tie that rounds to even with Inexact, one a quiet NaN that keeps
  its payload, one a signalling NaN quieted with Invalid, one -0 against -0 (-0, +0, +0 and the
  default NaN). A signalling NaN stored from b, h, s, d and q registers keeps its bits and touches
  no byte past it. LDXP and STXP swap a 128-bit pair, and STXP after CLREX fails and leaves the
  pair. And CNTFRQ_EL0 says 19.2 MHz while 100 ms asleep counts at that rate on CNTPCT_EL0, the
  count libnx reads, a wake up to a second late allowed. The shift, CRC and rounding references, the
  fused rows, the compares and the lanes agreed with Unicorn's AArch64 on 98,161 checks, every input
  the cases use among them.
  ScarletNX 9bef605 passes all sixteen. Under Eden 6a9235c22d fourteen pass: FRINTN, FRINTP, FRINTM,
  FRINTZ, FRINTA and FRINTI raise Inexact where they round, which only FRINTX does (dynarmic's
  `backend/x64/emit_x64_floating_point.cpp`, `EmitFPRound`, takes ROUNDSS or ROUNDSD for every
  rounding but FRINTX's, read at reden 44395ac49c, and the host's precision flag reaches FPSR).

  The last two read system registers a game may reach from EL0: CNTVCT_EL0, the virtual count,
  which has to move forward with the physical one, and DAIF, which holds nothing outside its four
  mask bits. dynarmic has no translation for either and reaches UNREACHABLE, which ends the
  program, and the console's kernel may refuse EL0 either of them, which a console run settles.

  The cases run in this page's order, and every one runs in every mode. Some end the program on a
  machine that gets them wrong: the two registers under dynarmic, SM4 on the console, whose
  processor lacks it, and on a translator that cannot compile it, and the conversions on a
  translator that cannot compile them without SSE4.1, which calls a null pointer there. Each says
  it started before its instruction, so such a machine loses that case alone: the next launch
  skips it as crashed and goes on with the one after ([When the program ends in the
  middle](../Crashes.md)). Console build only, since its cases are AArch64 instructions.
