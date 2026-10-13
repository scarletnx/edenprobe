# The processor

- **cpu**, nine cases, on the processor and in every mode, since it needs no device. Three count
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
  being judged. The conversions run last of the cases every run has, because a translator that
  cannot compile them without SSE4.1 calls a null pointer there and nothing after it reports. SM4
  is in the may-crash table: it runs only with the mode word `fatal` or when the start screen asks
  for it, and is skipped as `may crash` otherwise, since the console's processor has no SM4 and a
  translator that cannot compile it ends the program. Console build only, since its cases are
  AArch64 instructions.
