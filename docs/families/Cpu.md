# The processor

- **cpu**, ten cases, on the processor and in every mode, since it needs no device. Three count
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

  The conversions run last of the cases every run has, because a translator that
  cannot compile them without SSE4.1 calls a null pointer there and nothing after it reports. SM4
  is in the may-crash table: it runs only with the mode word `fatal` or when the start screen asks
  for it, and is skipped as `may crash` otherwise, since the console's processor has no SM4 and a
  translator that cannot compile it ends the program. Console build only, since its cases are
  AArch64 instructions.
