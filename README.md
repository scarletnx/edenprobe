# edenprobe

A test program that runs on the console and inside an emulator of it, decides for itself whether the
graphics came out right and whether the online services answered as their protocol says, and writes
a report you can diff between the two.

It is not a benchmark and not a demo. Every case computes the answer it expects and checks the
answer it got; nothing needs a person to look at the screen and judge.

It was gfxprobe until 23 September 2026. The name changed when suite families beyond the graphics
unit arrived: one binary with one judge is the reason to keep them together.

## Why not compare screenshots

Because two correct implementations are allowed to disagree, and the specification says so. The
graphics API requires division to land within 2.5 units in the last place, the logarithm within 3
outside the range from a half to two, the power of two within 3 plus twice the magnitude of its
argument, and the sine and cosine only within an absolute 2^-11, and only inside minus pi to pi.
Sub-pixel rasterisation precision is a limit each implementation reports, not a constant. A hash of
a shaded frame therefore breaks between two machines that are both right; the projects that took
that route keep one reference image per backend, driver and operating system, and still chase false
positives.

So the decision is made inside the program, on both machines, by the same code.

## The three tiers

Which tier a case uses is a claim about the hardware, and it belongs in the case.

- **Exact.** Bit for bit. Legal only where the result is defined exactly: integer render targets,
  integer arithmetic, bit patterns moved around. A float shader never qualifies, however tidy the
  numbers look on one machine.
- **Units in the last place.** For float work, with the budget stated per case.
- **Position.** The right answer, possibly a pixel or two away, for rasterisation coverage.

## What it checks today

Thirty seven suites, in the order they run: the judge, then the processor, then the graphics
family, then the net family. A run is 748 cases: 695 for the judge, the processor and the graphics
family, 53 for the net family; the mode word `stress` adds one, and `fatal` runs the cases the
may-crash table names, which are otherwise skipped. The order, and what a failure in each suite
points at, is in "The roadmap" below. A case that ends the program costs that case and not the rest
of the run: see "When the program ends in the middle".

- **selfcheck**, sixteen cases, no hardware involved. That two neighbouring floats measure one unit
  apart and a zero budget rejects them; that an absolute budget is the same on both sides of the
  expected value; that a NaN equals nothing, itself included, whatever the budget; and that three
  units in the last place reach six representable values below a power of two and three above it.
- **support**, one hundred and ten cases, no hardware involved: what the program does around a
  verdict rather than to reach one. The pictures a failing case leaves and the names they get, the
  detailed mode's store, screens and navigation, the start screen, the mode file and its `stress`
  word, the `skip` result, the time a case reports (nineteen cases, see "Time per case"), and the
  journal, the crash list, the may-crash table and what a launch decides from them (eighteen cases,
  see "When the program ends in the middle"). One case carries the whole idea: the diff picture is
  built from what the judge decided pixel by pixel, not from comparing two images, so a pixel that
  differs and was allowed to is not painted as a difference. Every line is the none tier, because
  none of it is a claim about a machine.
- **reference**, one hundred and seventy five cases, no hardware involved either: the other half of
  the judge, where the rules the cases judge by are pinned. Where clip space lands on the target,
  which pixels a triangle covers and which of two triangles owns the centres on their shared edge,
  projected from the very numbers the rasterisation cases hand the hardware, so it proves the
  geometry they draw rather than restating it. Fifteen transcendental answers as bit patterns worked
  out to fifty digits away from any C library, so a runtime that drifts moves an expected value on
  its own machine and says so there, not inside a float case. It runs in both builds, because a
  reference that answered differently on the two processors would be blamed on the graphics unit.
  Among the rest:
  - three cases on the blend table (the blend suite says why);
  - seven on the format rules: the clear and drawn pixel of all sixty formats, worked out from each
    format's name by a separate script, `tools/format-pixels.py` (it prints the table the test
    holds, and shares no code with `formatref.c`); every format's fields against its pixel; the
    half, small float and sRGB conversions inverted over every code; the quad covering exactly its
    rectangle. One of these caught the emulator's processor rather than its graphics unit: under
    Eden, `rint`, a single `frintx`, rounds 1024.5 to 1025 with the rounding mode at nearest, where
    the architecture gives 1024, so the format reference rounds its halves by hand (the cpu suite
    now checks that instruction itself);
  - eleven on intmath: 58 answers at two pixels worked out with exact rationals away from this
    program's code, and each rule against a second algorithm over the whole sweep;
  - five on fmath: 54 answers worked out with exact rationals and sixty digit decimals, the float
    judge's decisions one at a time, the answer the console never gives refused at every pixel where
    it differs, where the edge operands sit, and a budget written down for every mode;
  - twenty two on the net family's judge, `source/netref.c`: the net file, what each service is
    expected to answer from it, and every format a service answers in, each fixture written out by
    hand from the format rather than captured (the resolver's two serialisations, a certificate in
    PEM and DER and whether it is self-signed, against bytes openssl printed, a handshake's two
    certificate forms, an HTTP response head, a status document, a friend profile at the offsets the
    guest reads, a bearer token's alphabet, a result code as the platform prints one), and what nsd,
    sfdnsres, the sign-in wait, a picture url's `{size}` and a profile slot should be;
  - eight on the cpu suite's answers, against numbers written by hand: the leading zero counts,
    the two roundings, the saturating shifts, the conversions with 64 fraction bits, and the SM4
    inputs from the standard's key and constants;
  - eight on the raw 3D method words the index, instance, drawtex and query suites send, against
    words worked out by hand from deko3d's command header layout;
  - twenty one on the astc suite's answers: the ASTC reference decoder, `source/astcref.c`, held
    block by block to hashes of what ARM's decoder (astcenc) makes of the same sixteen blocks, and
    its weight table to values worked out by hand from the bit replication rule;
  - four on the eac suite's answers, against values worked out by hand from the specification's
    formula;
  - sixteen on the etc2 suite's answers: the block builders held to the bytes the suite samples,
    the ETC2 reference decoder, `source/etc2ref.c`, held block by block to hashes of what Mesa's
    decoder (26.1.6) makes of the same ten blocks, and five texels worked out by hand;
  - four on the bcn suite's layout, `source/bcnref.c`: where deko3d puts each level and layer of
    a block linear array, against offsets worked out by hand from its layout rules, and the solid
    BC4 block.
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
- **clear**, five cases, on the graphics unit: whether the device came up (no tier, it compares
  nothing), and four that clear a 64 by 64 RGBA8 target and copy all 4096 pixels back. Three cover
  the whole target in three colours in a row, so a stale readback cannot pass by accident. The
  fourth clears a rectangle of another colour inside it, which makes the answer depend on where a
  pixel is: only this one catches a transposed, flipped or shuffled readback. All four are Exact:
  the format is integer and every clear value a constant, so each channel has one right byte.
- **format**, sixty four cases: every colour format the graphics library can render to, sixty of
  them (one to four channels, eight to thirty two bits, normalised, signed normalised, integer,
  signed integer, float, the two small floats of RG11B10, sRGB, the packed RGB10A2, the blue first
  sixteen bit ones, and the X formats whose fourth channel is stored and not defined), plus four for
  the bring-up and a check that the reference's table and the library's formats pair by name. Each
  format gets a 32 by 32 target cleared to one value, with a quad over x 4 to 20, y 12 to 28
  (aligned to nothing, not centred) in a second value from a uniform buffer; every pixel is read
  back at the format's own size and compared with what `source/formatref.c` says the format stores
  there, the drawn value inside and the clear outside.

  A format's name lists its fields from the least significant bit up, little endian: RGBA8 is red in
  the first byte, BGR565 is blue in bits 0 to 4 and red in 11 to 15. The conversions are the
  specification's: clamp and round to the nearest code for normalised channels, minus one as minus
  the largest code (never the one below), the low bits of an integer, binary16 and the small floats
  rounded to nearest even, and the sRGB curve before the normalised rule. The values are chosen so
  each rule has one answer. The specification's conversion must return the integer its product lands
  on exactly and may otherwise pick either neighbour, so every normalised value is the nearest float
  to a code over the largest code, whose product with the largest code in single precision is
  exactly that code, for every code used at every width from 1 to 16 bits (the reference suite
  checks it). The sRGB values decode from a code and encode back within three millionths of it. The
  drawn value carries one channel above the range and one below, so both clamps are judged; the
  integer ones are the golden ratio's word and its complement, so every stored bit changes; the
  float ones are exactly representable, including the largest half, the smallest normal and an
  infinity, and no denormal, which a target may flush. Integer and normalised formats are Exact; the
  float ones are the units in the last place tier at a budget of nought. sRGB clears use nought and
  one, which encode to themselves, so whether a clear goes through the curve decides nothing.

  The formats the library refuses as a target are named in a note with the reason: the twelve byte
  RGB32 ones, the sampling only packed ones, the shared exponent one, the compressed ones and the
  depth ones. A failing case gives the number of wrong pixels, the first one's bytes against the
  expected under the mask of defined bits, and how many inside the quad still hold the clear and
  outside hold the drawn value, which tells a conversion fault from a coverage fault. Against the
  emulator build from before the integer target fix (`eden-cli-dacd0e59f2`), all 25 integer formats
  fail, only outside the quad: the clear wrote the wrong bits, the defect that fix addressed, now
  seen for every integer format rather than R32_Uint alone.
- **raster**, ten cases. A triangle or a quad in one flat colour through a vertex and a fragment
  shader compiled into the binary, every pixel compared to the coverage rule: exact for pixels at
  least half a pixel from every edge, position for the band an edge runs through, and exact
  everywhere for the two cases that put 32 pixel centres exactly on a shared edge. Those are decided
  by the fill rule, not by sub pixel precision, so an implementation that draws a shared edge twice
  or not at all shows it there.
- **intmath**, forty six cases. Fragment shaders doing integer arithmetic on their own coordinate
  into an R32_Uint target: add, multiply, the three shifts, and, or, exclusive or, bit counting, bit
  scanning, division and remainder; conversions between integers and floats both ways, above two to
  the 31 included; packUnorm4x8, packSnorm2x16, packHalf2x16 and their round trips, 0x8000 coming
  back as 0x8001; shifts by 31, 32, 33 and every count up to 63; signed division and remainder with
  every combination of signs; findMSB and findLSB of nought and minus one; bitfieldExtract and
  bitfieldInsert at every offset and width, nought and 32 included; bitfieldReverse; and signed and
  unsigned minimum, maximum and clamp, with the most negative and most positive values, nought and
  minus one against each other. Exact tier, the strongest claim here: an integer target normalises
  nothing, so a wrong answer is a wrong bit pattern.

  Two of those needed a rule the language does not give, taken from the console's compiled shader.
  The language leaves a shift by 32 or more undefined; `uam` emits every register shift here without
  the wrap flag (bit 39 of the instruction clear, read with a decoder of the opcode table in the
  emulator's `frontend/maxwell/maxwell.inc`), and such a shift clamps its count to 32, as PTX
  defines the same instructions: 32 and up shift everything out, to nought or to the sign. And the
  console has no integer divide: `uam` says so as it builds (`program uses non-constant integer
  division/modulo ... floating point emulation with resulting loss of precision has been applied`),
  and the division compiles to two conversions, a reciprocal, a multiply and a truncation, which
  leaves a quotient near a whole number to the reciprocal. So the operands stay under two to the 18
  and never a multiple of the divisor, which keeps the true quotient at least a part in 2334 from a
  whole number, where a float error of a few units cannot move the truncation. The reference suite
  measures both.

  The modes are spread over six shaders by family (the original fourteen, conversions and packing,
  shifts, division, remainder, bit operations), so an instruction an emulator cannot translate fails
  only its own family's shader, and a case whose every pixel still holds the sentinel says so. The
  first three cases are the fragment coordinate and the uniform buffer, which every later case reads
  its operands and its instructions out of.
- **blend**, thirteen cases. The fixed function blend unit: adding, subtracting both ways round, the
  minimum and the maximum, the source alpha, the blend constant, and the clamp at each end of the
  range. Exact on a normalised target, which takes an argument: the specification requires the
  conversion back to eight bits to round to nearest but leaves a halfway value to the
  implementation, so every case lands at least 0.49 of a step away from one, which the reference
  suite measures. Two cases set both factors to zero under the minimum and the maximum, because
  those two equations ignore the factors.

  The alpha channel has its own equation and factors, and wiring it to the colour ones is the
  classic way to get this unit wrong, so the table keeps them apart: by the equation and the source
  factor, never by the destination factor, which is the graphics library's limit.
  `dkCmdBufBindBlendStates` writes six registers per target and puts the colour destination factor
  in the last, the alpha destination one, so a state whose two destination factors differ reaches
  the hardware with both the same (deko3d 0.5.0, `source/maxwell/gpu_3d_state.cpp`; the other five
  words carry the fields they are named after). A case built on that difference would fail a machine
  that did nothing wrong, so the reference suite refuses one, and checks that some case still
  separates each part that does reach the registers. Whether the hardware then acts on them is left
  open too: separate alpha blending is switched on by the register just before those six, which no
  library call writes (nor its global twin), so a console and an emulator can disagree. Two notes on
  every run say whether the alpha channel runs an equation of its own and whether an alpha
  destination factor of its own has started arriving; the first is what makes the second readable.
  They are notes, not cases, because they are properties of the way to the hardware, not of the
  blend arithmetic.
- **depth**, thirty one cases. One quad at a known depth and a second one challenging it: each of
  the eight compare operations at each of three depths, twenty four cases. Three depths give eight
  patterns of pass and fail, one per operation, so leaving any out would let two operations share
  their answers and an emulator that wired them together go unnoticed; one more case works the
  twenty four answers out on the processor and checks that the eight really differ. Plus the depth
  buffer read back as a float, bit for bit. Exact: the depths are 0.25, 0.5 and 0.75, which survive
  the transform into depth space untouched, so a fragment at the buffer's own depth really is an
  equality. Three cases are about the write rather than the test, the half a colour cannot show.
- **texture**, twelve cases. A 4 by 4 texture over the whole target, so sixteen by sixteen pixels
  land on each texel. Nearest filtering with repeating, clamping to the edge and mirroring, each on
  a quad three copies wide, and linear filtering laid out so the only weight the filter can use is
  one half and every pair of texels averages to a whole byte. Exact, earned by the layout: no pixel
  centre comes within a thirty second of a texel boundary (the reference suite measures it), and no
  two texels the linear case blends differ by more than 48, so the filter's weight would have to be
  wrong by one part in 96 before any byte moved.
- **coherence**, twenty one cases, five of them the none tier (the device, the shaders, the images
  and their starting texels coming up, and the block linear texture having the size the gob rule
  assumes) and sixteen exact. Every suite above draws once into memory nobody has read, so the
  caches an emulator keeps between draws, submits and copies never have an old answer to give. Here
  each sampling and uniform case, but the first alias case, makes the reader hold the old contents,
  then writes new ones by one path and reads them by another: a texture rendered and sampled in one
  list, whole and in part; a texture the processor rewrites between two submits, pitch linear, block
  linear from the middle of a gob, and one row through a cached mapping flushed with
  `dkMemBlockFlushCpuCache`; a uniform buffer the processor rewrites between submits, whole and then
  one word at a time over four submits, and one the list pushes between two draws; a render target
  sampled through an R32_Uint view of the same memory, judged as the little endian word rotated
  right by four bits, so every output byte mixes two input bytes; a texture filled by the copy
  engine from a buffer and from part of another image; and a render target copied, whole and in
  part, to another image, which tests that rendered pixels are flushed before the copy engine reads
  them. Every pattern's four bytes are unique to each texel and step, so a shift, a transposition or
  a stale texel is always a wrong byte (the reference suite checks every seed). Region one of the
  target shows the old contents, region two the new, and a failing case counts how many of its wrong
  pixels hold exactly the old contents, which is what a stale cache looks like. The synchronisation
  is what `deko3d.h` documents for each hand over and no more, named in the suite's header: the
  fragments barrier between a draw that samples an image and one that renders it, the full barrier
  around the copy engine, the image or shader invalidation before reading what was rewritten, and a
  wait on the queue only where the processor touches the memory, since a wait anywhere else would
  hide the fault the suite is there to find. Every expectation comes from one image model,
  `source/coherref.c`, pinned by the reference suite.
- **fmath**, twenty six cases. Addition, multiplication, a fused multiply and add, division, the
  square root, powers and logarithms of two, the sine and the cosine, into an R32_Float target,
  against the same arithmetic in double precision; and the edges: a fused multiply and add that
  nearly cancels its own product, the minimum and the maximum against infinities and NaNs, mix
  carrying them through, denormal products, sums and operands, the power of two from -128 to nearly
  128, the logarithm from the smallest normal to nearly the largest float, square roots and inverse
  square roots of denormals and tiny normals, and floor, fract, round and roundEven on negatives and
  halves. Units in the last place, with a budget per case, because the tables give each operation
  its own figure and two of them are not even the same shape. They all come from one place, named in
  `source/fmathref.h`: the Vulkan precision tables, core for core instructions and GLSL.std.450 for
  the rest. The shading language's own specification leaves the trigonometric functions undefined,
  so it would give no figure for two of these nine operations; the Vulkan tables cover all nine:

  | operation | the table | what the case allows |
  |---|---|---|
  | addition, multiplication | correctly rounded | nought |
  | a multiply and an add | inherited from a multiply and an add; the console's FFMA fuses | nought |
  | division | 2.5 | 3 |
  | the square root | inherited from one over the inverse square root, which is 2 and 2.5 | 5 |
  | two to the power of x | 3 plus twice the magnitude of x | 3.5 plus 2 per unit of it |
  | the logarithm to base two | 3 outside the range from a half to two | 3.5 |
  | the sine and the cosine | an absolute 2^-11 inside minus pi to pi | the same |
  | the edges of the power of two and the logarithm | as above, over the wider ranges | as above |
  | the inverse square root of tiny operands | 2 | 2.5 |
  | the square root of tiny operands | inherited from one over the inverse square root, 2 and 2.5 | 5 |
  | fma nearly cancelling its product, floor, fract, round, roundEven, min and max against infinities, denormals | exact once the console's rules are applied | nought |
  | min and max against a NaN | the console hands back the other operand, as NMin and NMax do | nought |
  | mix through infinities and NaNs | the formula in IEEE arithmetic | nought |

  Where the tables allow more than one answer and the console's compiled shader gives exactly one,
  the case holds a machine to the console's. Read out of `fmath.frag.dksh`: every float instruction
  flushes denormals (the FMUL, FFMA, FADD and FMNMX flush bits are set, and MUFU has none to clear),
  every fma is one fused FFMA, and round compiles to the same round to nearest even as roundEven.
  The fma case subtracts the rounded product with its low twelve bits cleared, so a fused machine
  keeps the product's rounding error and one that multiplies and adds apart loses it at nearly every
  pixel. A failing case counts the wrong pixels that hold what the tables would allow and the
  console does not give, and names it: denormals kept, the multiply and the add done apart, halves
  rounded away from nought, plus nought where the console keeps minus nought. The judge is one
  function, `FMathJudge`, pinned case by case by the reference suite: a NaN expected is met by any
  NaN, a nought by the same nought with its sign, anything else by the budget. The NaN and infinity
  cases hold an emulator to the console, not to the Vulkan tables, which let a host assume neither
  arrives, and the budget line says so.

  Most budgets carry half a unit on top of the table's figure, for the reference being a double
  precision answer rounded to single. Three do not, and each says why on its own line: addition and
  multiplication are exact in double precision before the rounding, the multiply and add is measured
  against the processor's own fused one, which is exact, and the sine and cosine's budget is two
  thousand times larger than anything the reference could contribute. The power of two's budget is
  the only one that is not a constant, because the table's figure is not: over the range swept it
  runs from three and a half to eleven and a half units, and a flat number would either fail a
  correct machine at the ends or wave the middle through.

  A unit in the last place is a distance, not a count of representable values: the values just below
  a power of two are half as far apart as the ones above, and the suite's answers land on powers of
  two often. Each case reports its worst error next to its allowance, so a pass still says how much
  room was left. Every case that computes something is the units in the last place tier, the budgets
  of nought included, since a tolerance of nought is still a tolerance; the one Exact case is the
  guard, which copies a float through a uniform buffer and computes nothing.

- **order**, nineteen cases, on the graphics unit with a compute queue. A dispatch writes a buffer,
  a wait for idle follows (`DkBarrier_Primitives`, the library's "completes the processing of all
  previous primitives and compute jobs", emitted as the 3D engine's WaitForIdle, the pattern
  deko3d's own compute example uses between generating vertices and drawing them), and the next
  command reads the buffer: as vertices, as indices, as indirect draw arguments, as a uniform
  buffer, as a storage buffer in the fragments, as the grid of an indirect dispatch, and as the
  input of another dispatch; and the reverse, words the fragments stored read by the next
  dispatch. Every reader must see the written words. The race is made as wide as a dispatch
  allows: every invocation spins first and only the last ones copy, and what the buffer held
  before is chosen so a reader that ran early draws nothing or launches nothing, which the
  failing case counts. Four more cases are for time: 128 dispatch and draw pairs, and 256 draws
  into one target, each with and without a wait after every one; the differences are what a wait
  for idle costs on the machine under test, with and without compute around it. Exact
  tier. In an emulator a failure points at the host side of the guest's wait for idle
  (`renderer_vulkan/vk_rasterizer.cpp`, `WaitForIdle`).

  Five cases ask the same question of the library's other barriers. Vertices, indirect draw
  arguments and a storage buffer a dispatch wrote are read after `DkBarrier_Full` ("completes the
  processing of all previous commands"), which the library emits as the channel's SetReference and
  nothing in the 3D class; in an emulator a failure points at what that method records
  (`engines/puller.cpp`, `RefCnt`). And a draw whose fragments store a word each, late, is read
  back pixel by pixel by the next draw's fragments after `DkBarrier_Fragments` ("similar to Vulkan
  renderpasses") and after `DkBarrier_Tiles` ("similar to Vulkan subpasses"), each pixel reading
  only its own word, with the shader cache invalidation and no image cache one; in an emulator a
  failure points at `FragmentBarrier` and `TiledCacheBarrier` (`renderer_vulkan/vk_rasterizer.cpp`).
  The tiles case holds the library to its documentation and has not been run on a console yet.
- **index**, eight cases. Indexed draws whose index limit register holds the last byte of the
  indices, the way a driver that bounds its draws exactly programs it (the register is a limit,
  the last valid byte; nouveau writes the address plus the size minus one). The library never
  writes that register after start-up, so each case writes it with raw methods before the draw
  and restores the library's value after. The draw's last index closes the second of two
  triangles over the target, so reading the limit as one past the end loses half the target.
  8, 16 and 32 bit indices, a long draw, and one that starts four indices in; one control with
  the library's open limit. In an emulator a failure points at the size the index buffer is bound
  with (`buffer_cache/buffer_cache.h`, `UpdateIndexBuffer`).
- **instance**, five cases. The 3D class's instanced vertex array draws, VERTEX_ARRAY_INSTANCE_FIRST
  and _SUBSEQUENT, sent as raw methods between the library's own commands. FIRST draws instance
  nought when it arrives, with the state it arrived under: a uniform value pushed after it must
  not reach it, and the library draw that follows must add no instances of its own. Each
  SUBSEQUENT draws the same range as the next instance, and the vertex shader moves each instance
  one region along, so the target says which instances ran. In an emulator a failure points at
  `engines/draw_manager.cpp`, `DrawArrayInstanced`, `DrawDeferred`.

- **geometry**, six cases. The two optional stages every other suite leaves out. A geometry shader
  turns each of four points into an 8 by 8 square in the point's colour, or into nothing when the
  point's alpha is nought; a tessellation control and evaluation pair spreads a four point patch
  over its corners' rectangle at level 4 and at level 7, the level taken from the patch and
  handed back in the alpha of every pixel, which says whether the control stage's levels reached
  the evaluation. Every edge is a pixel edge, so every pixel is exact. A host without geometry or
  tessellation shaders has to emulate them to pass (Ryujinx runs them as compute there); an
  emulator that assumes them leaves it to the driver, which gets a pipeline it cannot run
  (`vulkan_device.cpp` lists `geometryShader` as mandatory and carries on without it). The two
  geometry shader cases are in the may-crash table: the shader compiles to a PRET, which ends
  Eden, and the fork before ECFE-2026-1163, inside the pipeline cache.

- **bindless**, five cases. A fragment shader picks one of 256 one-texel textures through a handle
  read from a uniform array at a dynamic index (ARB_bindless_texture, which uam compiles to the
  console's bindless fetch), so column i of the target must show texture i. Split at 128 so a
  host array cut short at 128 descriptors says so on its own line. The same shader with a
  constant index is how the setup was checked: the handles and the index reach the shader. In an
  emulator a failure points at the dynamic descriptor arrays the shader translator builds
  (`shader_recompiler/ir_opt/texture_pass.cpp`, `DESCRIPTOR_MAX_COUNT`,
  `DynamicDescriptorCount`).

- **astc**, eighteen cases. ASTC textures of one block each, the block written by the processor
  into image memory and every texel fetched onto its own pixel: a void extent, 4-bit weights 0 to
  15 and every 5-bit weight between endpoints 0 and 255 (built by hand, `tools/astc-blocks.py`),
  trit and quint weights, trit and quint endpoints, two, three and four partitions, a dual plane,
  weight infill on 8x8 and 12x12 blocks, an HDR partition before an LDR one, and an illegal block.
  What each texel must be is `source/astcref.c`, an LDR profile decoder written from the Khronos
  Data Format Specification's ASTC chapter, which the reference suite holds block by block to ARM's
  reference decoder (astcenc 9fbec680) and which agreed with it on 458,752 random blocks over
  every 2D footprint when it was written. A texel may be either byte its 16-bit value gives,
  divided by 65535 and rounded or its top byte, since the texture unit may decode to eight bits;
  nothing else. HDR endpoint modes are the error colour, opaque magenta, on their own partition's
  texels in this profile, as astcenc has it. In an emulator a failure points at the ASTC decoder
  it uses (`video_core/host_shaders/astc_decoder.comp`, `video_core/textures/astc.cpp`): the 4-bit
  and 5-bit cases at the weight unquantization, the partition cases at the partition hash.

- **eac**, four cases. The one- and two-channel 11-bit EAC formats of ETC2 (the library's
  R_ETC2 and RG_ETC2), one 4x4 block each written by the processor and fetched texel by texel:
  every index, both clamps, and the multiplier of nought, which adds the bare modifier. What each
  texel must be is `source/eacref.c`, the specification's formula, which agreed with Mesa's
  `texcompress_etc.c` on a million random blocks; the 11-bit value may reach the target as either
  of two bytes (divided by 2047, or extended to 16 bits first). A host without EAC textures has
  to decode them itself; in an emulator a failure points at the EAC format mapping
  (`texture_cache/format_lookup_table.cpp`, `surface.h`) and at whether it decodes EAC for such a
  host.

- **etc2**, twelve cases. The ETC2 colour formats (the library's RGB_ETC2, RGB_PTA_ETC2 and
  RGBA_ETC2), one 4x4 block each written by the processor and fetched texel by texel: an
  individual, a flipped differential, a T mode, two H mode blocks (the first base the larger and
  the smaller, which sets the distance index's last bit), a planar block, three punch-through
  blocks (index 2 transparent in differential and T mode, and planar always opaque), and an RGBA
  block whose 8-bit alpha reaches both clamps. Every block is built from its fields by
  `source/etc2ref.c`, whose decoder says what each texel must be; the reference suite holds it to
  Mesa's. ETC2 decodes to exact bytes, so the tier is exact. A host without ETC2 textures has to
  decode them itself; in an emulator a failure points at whether it does
  (`renderer_vulkan/maxwell_to_vk.cpp`, `vk_texture_cache.cpp`, `texture_cache/util.cpp`,
  `ConvertImage`): an upload of the raw blocks shows bytes of block data.

- **bcn**, five cases. BC4 arrays whose levels or layers do not end on a block boundary: six
  layers with levels down to 1x1, the shape of every mipmapped cube map; two layers six texels
  tall; and a 4x16 texture whose second level is two texels wide and two blocks tall. Every block
  is one value unique to its layer, level and position, written by the processor where
  `source/bcnref.c` says the library put it, and every texel of every level and layer is fetched.
  Exact tier. A host with BCn textures samples the blocks as they are and passes; one without
  decodes them on upload, and in an emulator a failure points at that decode's walk over the
  layers of a level (`texture_cache/decode_bc.cpp`, `DecompressBlocks`). The three array cases are
  in the may-crash table: the array fetching shader compiles to a PRET, which ends Eden, and the
  fork before ECFE-2026-1163, inside the pipeline cache.

- **zeta**, six cases. Depth textures the processor writes, sampled texel by texel: Z16 and ZF32
  as controls, then Z24S8 twice, once with a word that reads the same whichever way its depth and
  stencil are split and once with depth 0x00ff00 over stencil 0x5a, whose low 24 bits would give
  254 where the depth gives 1. Exact tier: each depth is a whole number of 255ths or a quarter
  away from a half. In an emulator a failure points at the upload of a depth texture: a host
  without D24_UNORM_S8_UINT that copies the words into a 32-bit float depth image, or a copy that
  takes the depth from the low 24 bits (`renderer_vulkan/vk_texture_cache.cpp`,
  `TransformBufferImageCopies`).

- **swizzle**, fourteen cases. Block linear textures the processor writes texel by texel at the
  addresses the block linear rule gives (`source/coherref.c`, `CohBlockLinearOffset`) and samples
  back one texel per pixel: 1, 2, 4, 8 and 16 bytes per texel, blocks 1 to 32 gobs tall, and a
  37 by 23 texture that ends inside a gob. The texels are unsigned integers folded into one word
  by `swizzle.frag.glsl` with shifts and ors, so every bit is compared exactly; each case first
  checks the library laid the texture out in the bytes the rule says. The last case writes a
  1024 by 1024 texture, 4 MiB, and its time is the whole upload an emulator makes on first use.
  In an emulator a failure points at the unswizzle, on the processor (`textures/decoders.cpp`) or
  in a compute pass (`host_shaders/block_linear_unswizzle_2d.comp`, `vk_compute_pass.cpp`).

- **texel**, seven cases. RGBA8_Uint buffer textures 0, 4, 20, 36 and 68 bytes past a 256 byte
  aligned address, all of them the four bytes the library asks of this format; column x must
  fetch texel x, and a wrong one says how many bytes away the texel it showed sits. A host that
  aligns texel buffers more coarsely (64 bytes is common on phones) has to make up the difference
  somewhere other than the texels a shader reads; in an emulator a failure points at how a
  buffer view is made (`renderer_vulkan/vk_buffer_cache.cpp`, `Buffer::View`).

- **stream**, four cases, five with `stress`. One list of draws, each covering one pixel with the
  first word of a uniform buffer of its own, every buffer different memory the processor filled
  before the list: 4096 of 4 KiB (16 MiB) and 1024 of 64 KiB (64 MiB). Pixel k must hold buffer
  k's word; the time is what the uploads cost. With the mode word `stress`, 4096 of 64 KiB,
  256 MiB in one list, more than the ring an emulator stages uploads through, which then has to
  wrap inside the list without handing a later draw's bytes to an earlier one. It is off by
  default because an emulator that runs out of memory on it takes the rest of the run down with
  it. In an emulator a failure points at the staging ring (`renderer_vulkan/vk_staging_buffer_pool.cpp`,
  `GetStreamBuffer`) and the uniform buffer upload path (`buffer_cache/buffer_cache.h`).

- **fill**, five cases. 128 quads over a 512 by 512 target in one list, first with colour alone
  and then with a 32-bit float depth buffer tested and written by every quad, each nearer than the
  last. The last quad's colour and depth must cover the target, so a time is never the time of a
  wrong answer; the difference between the two times is what depth costs the machine, which is
  where an emulator that loses depth compression (a depth image created with usage it cannot
  compress, `renderer_vulkan/vk_texture_cache.cpp`) pays. A single depth tested quad goes first
  and takes the target's first use in each pass: without it, whichever case came first measured
  about 11 ms against 2 on RADV, in either order. There, colour alone takes about 2.0 ms and depth
  2.7.

- **ring**, four cases. One list of 4096 indexed draws, each covering one pixel with the word of
  a uniform buffer of its own, as stream does, through shaders that also read 32 textures each
  (`ring.vert.glsl`, `ring.frag.glsl`, one texel of nought behind every binding): 65 descriptors
  a draw, more than a host pushes per draw (32 on RADV), so an emulator writes every draw's set
  into a descriptor ring, a new one each draw since the uniform buffer moves. The ring runs out
  every few hundred draws and waits for the GPU to take more room; an emulator that takes that
  wait after it bound a draw's index and vertex buffers submits them with the command buffer
  before the draw's own, and the draw is lost, its pixel left cleared. The failing case counts
  the lost draws and names the first. In Eden the ring is on only where the device reports a
  Vulkan version below 1.2, so on a newer driver this suite shows the defect only under the
  masquerade layer (see "Making the driver look older").
- **occlusion**, six cases. The samples counter, which the library leaves enabled, reported
  after each of 320 quads (reset before each, so each must count exactly its 64 pixels) and
  after each of 320 cleared cells, in one list each: more than 256 reports, so an emulator that
  keeps its queries in banks of 256 takes a new bank in the middle of a list, at a draw or at a
  clear. Every quad, every count and every cleared cell is judged. An emulator that resets a
  new bank's queries by ending the render pass the draw or clear has just begun records that
  draw or clear outside any render pass; in Eden only on a driver without host query reset,
  which the masquerade layer gives on any driver.
- **drawtex**, six cases. DRAW_TEXTURE, the call the console's graphics API names DrawTexture:
  a texture's rectangle copied onto the target by one method, between two draws. The library
  has no call for it, so it goes in as raw words (`rawref.h`, `RawDrawTexture`), in the layout
  the emulators that implement it read (the public class header leaves the range out). A list
  draws the left half with a flat pipeline, copies a 16 by 16 texture onto x 40 to 56, y 24 to
  40, and draws the bottom quarter with the same pipeline, or with another one; the copy, the
  draw after it with the pipeline of the draw before, and the draw after it with another
  pipeline are each a case. An emulator that blits with a pipeline, a viewport and a scissor of
  its own behind its cache of what is bound draws the next quad with the blit's pipeline, or
  into the blit's rectangle. The layout is the emulators' and has not been checked on a console.
- **query**, twenty three cases and three notes. The samples passed counter over quads of whole
  pixels, exact: 9600 for a 32 by 300 quad, 11200 with a second quad of 1600, nought after a reset.
  The target is 320 rows tall, past the 288 an emulator that scales render targets starts at, so a
  run with scaling at 2x says whether a game is handed the console's count or four times it (Ryujinx
  divides by the scale squared; a failing case names the factor). Then the render enable, which the
  library never writes, as raw methods around one draw: FALSE and TRUE, and IF_EQUAL and
  IF_NOT_EQUAL over two 64-bit values 16 bytes apart, written by the processor (equal with
  timestamps that differ, apart in the low word, apart only in the high word) and reported by the
  counter (the same count twice, a count and nought), in the same list after a wait for idle and in
  a list that finished before, the same-list pair also over a full target clear. The processor's
  values are plain memory to an emulator and the reports are queries it may be resolving at the wait
  or may have left from the earlier submission, so the sets can disagree; in an emulator a failure
  points at `query_cache/query_cache.h`, `AccelerateHostConditionalRendering`, and at
  `vk_query_cache.cpp`, which renders unconditionally on some drivers and accuracies. CONDITIONAL,
  which reads one address, gets three notes rather than cases (the value one, the value two to the
  32, a report of 9600), since which of its bytes decide is not settled: a console run is what says.

The net family asks the services a program reaches the network through, the way a game asks them,
and judges each answer against its protocol or against what `sdmc:/edenprobe/net` says the backend
publishes (see "The net family" below). Every line says what was expected and what came back. A case
that needs a backend the file does not name, or a service this program cannot open, is `skip`, with
the reason. The tier is exact wherever a value is compared and none where a line only says a service
opened. Nothing waits without a budget: every connect is non-blocking and polled for three seconds,
every read is polled first, and every handshake, write and read over ssl runs non-blocking against
five or three seconds, so a server that drops the attempt or accepts and never speaks costs a failed
case, never the run.

- **nifm**, seven cases. The service opens; a request nobody submitted says so; and, when the file
  names a backend, the connection is reported connected, the machine has an address of its own, a
  submitted request is accepted without being asked for its result and again when it is, and while
  one stands the service says an internet request is accepted.
- **dns**, eleven cases, sfdnsres and nsd. A dotted address resolves to itself through both of
  sfdnsres's serialisations, read by this program's own parsers; a name under `.invalid` does not
  resolve (RFC 6761); nsd names its environment, replaces the `%` placeholder with it, and leaves a
  plain name alone. With a session, once it answers: nsd hands back the destination the backend
  publishes for a name, sfdnsres resolves that name to the destination's address as a hostent, as
  addrinfo and through the C library, and a name with the placeholder, and the name nsd expanded,
  both go to loopback (the host rewriting rules in reden's `docs/OnlineService.md`). The fork signs
  in in the background when the program loads, so the suite asks nsd until it gives the session's
  answer, for at most the fork's fifteen second sign-in budget, and a note says how long it took.
- **bsd**, eight cases. Two datagram sockets on loopback send a datagram one way and back, whole
  both ways and each time from the socket that sent it; an empty socket has nothing to read; a
  stream to a listener the suite opens carries bytes both ways; a port nothing listens on refuses; a
  receive timeout set with `SO_RCVTIMEO` gives up with `EAGAIN` in its time (bounded by a datagram
  another thread sends two seconds in, so a machine that ignores the option fails the case rather
  than hanging). With a server: a stream connects to it, and a connect that does not block
  completes, polls writable and reports no pending error, as a game does.
- **ssl**, sixteen cases. A context; a new connection's default verification (the peer and the name,
  as libnx documents); the name and an option set on a connection reading back (SetOption carries
  the flag before the option, as the console lays raw input out by alignment); a connection taking a
  socket and blocking mode, and saying so; a descriptor handed back by default and none with
  `DoNotCloseSocket` (libnx's `ssl.h` on `sslConnectionSetSocketDescriptor`). With a server whose
  certificate the file names: a verifying handshake refuses a self-signed certificate, succeeds once
  it is imported, and hands back exactly its bytes; a handshake that does not verify succeeds,
  carries a request out whole and reads back a 200 with a status document that says nothing is
  wrong; and the connection keeps its socket after the program closes its own descriptor, since the
  service works on a duplicate. With a session: a verifying handshake under the name the backend
  rewrites, and under the address the service sends a game to, succeeds with nothing imported,
  because the backend publishes the certificate that server presents (`game_server` in reden's
  `docs/OnlineService.md`; the fixture publishes its own).
- **acc**, six cases. acc:u0 and a user, the network account manager, and with a session: the
  account id is the one the backend publishes, the id token refresh completes, succeeds and leaves a
  token in the cache, the token is a bearer token (RFC 6750's alphabet) terminated inside its
  buffer, and the cache hands back the same token again.
- **friend**, five cases. The service opens, and with a session: the blocked user list is the
  backend's, a profile the backend knows comes back valid with its name and picture, one it does not
  comes back invalid in its own slot with the id that was asked, and a picture url comes back with
  every `{size}` replaced by the size asked for.

When a session is named and never answers the sign-in wait, every session case in dns, acc and
friend says so at the end of its detail, so a failure there reads as a sign-in that did not happen.

Some expectations rest on documentation and reasoning, not yet on a console run, which would settle
them: that an unknown profile's slot carries the id that was asked (the contract says only that it
comes back invalid; the id is the fork's `profile_query.cpp`); that the ssl connection keeps its
socket when the program closes its own descriptor (POSIX `dup` semantics); that a submitted nifm
request is accepted by itself within three seconds (the three seconds are this program's); the
descriptor `sslConnectionSetSocketDescriptor` hands back (libnx's reading of it, which switchbrew
shares: the one passed in); and nsd expanding the placeholder with no session (the contract's
statement about the real service). Each case's detail says the same.

Nothing is put on a screen during the run. The target is offscreen and the program reads it back
itself, which keeps the compositor, the swapchain and vertical sync out of a test about none of
them, and keeps the first problem on a new machine from being "did the screen work".

## The roadmap

The suites are ordered by how much one case tells you when two machines disagree, each leaning on
the ones above it. That order is the whole point: when a run comes back with forty failures, the
first suite that failed is the only one worth reading, because everything below it was measured with
a broken instrument. It is the order they run in and were built in; anything added later goes where
its dependencies put it, not at the end.

| suite | tier | what a failure points at |
|---|---|---|
| selfcheck | none, no hardware | the comparators themselves. Nothing below can be believed. |
| support | none, no hardware | what is written and drawn around a verdict, never a verdict. |
| reference | none, no hardware | the expected answers: the coverage rule, the blend arithmetic, the texel address. The judge, not the machine. |
| cpu | exact | the emulator's translation of one processor instruction, the one the case names. In Eden: dynarmic's x64 backend (`src/dynarmic/src/dynarmic/backend/x64/`), the emitter or the soft-float fallback of that instruction. |
| clear | exact | the render target's memory layout and the path that copies it back. Everything below draws into that same target. |
| format | exact, ulp for floats | the render target format mapping for one format: its host format, its clear and its fragment output type. In an emulator: `video_core/surface.cpp` and `video_core/renderer_vulkan/maxwell_to_vk.cpp`, and the clear of an integer target. Inside or outside the quad tells a conversion from a coverage fault. |
| raster | position for edges, exact for interiors | vertex fetch, the clip space to screen space transform, and the coverage rule. In an emulator: the viewport registers, the y direction, the half pixel offset, and the fill rule the host rasteriser is asked for. |
| intmath | exact | the shader translator, bit for bit, and the delivery of the fragment's own coordinate. In an emulator: one wrongly decoded or untranslated instruction (`shader_recompiler/frontend/maxwell/translate/impl/`; the log names the instruction, the failing family names the shader), or an integer render target mapped to a normalised host format. |
| blend | exact | the fixed function blend unit: the equation and factor registers, and the rounding on the way back to eight bits. |
| depth | exact | the depth test: the depth target's format and layout, the depth range set up by the viewport, and the compare operation. |
| texture | exact | the texture and sampler descriptors: the wrap mode and filter fields, and the block linear layout a sampled image is stored in. |
| coherence | exact | a cache between two uses of the same memory. In an emulator: the texture cache not marking an image the graphics unit wrote as modified, or missing a processor write to one (`texture_cache/texture_cache.h`, `MarkModification`, `WriteMemory`); the buffer cache serving a uniform buffer the processor rewrote, or dropping an update pushed inside the list (`buffer_cache/buffer_cache.h`, `Maxwell3D::ProcessCBData`); the copy engine's writes not reaching the images it overlaps (`engines/maxwell_dma.cpp`), or a render target not flushed before the copy engine reads it; or two formats over one memory joined as a conversion rather than a view (`texture_cache/texture_cache.h`, `JoinImages`). The region two pixels that still hold region one's contents say which. |
| fmath | units in the last place | the float side of the shader translator, and the transcendental unit behind it; for the edge cases, the minimum and maximum's NaN rule, denormal handling and the rounding modes. The only suite where two correct machines are allowed to differ, which is why it is last among the graphics suites. |
| order | exact | the host side of the guest's wait for idle: a compute write not ordered before the vertex, index, indirect, uniform or storage read after it (`renderer_vulkan/vk_rasterizer.cpp`, `WaitForIdle`), or indirect arguments read from memory before the dispatch that wrote them finished; and the same after the full, fragments and tiles barriers (`engines/puller.cpp`, `RefCnt`; `FragmentBarrier`, `TiledCacheBarrier`). |
| index | exact | the size an index buffer is bound with: a limit register read as one past the end drops the last index (`buffer_cache/buffer_cache.h`, `UpdateIndexBuffer`). |
| instance | exact | when an instanced array draw happens and as which instance (`engines/draw_manager.cpp`): deferred past a state change, extra instances added to the next draw, or SUBSEQUENT not counted as the next instance. |
| geometry | exact | the geometry and tessellation stages: points expanded or dropped, a patch's coverage and the levels reaching the evaluation stage; on a host without those stages, whatever the emulator does instead. |
| bindless | exact | a texture reached through a handle at a dynamic index: the host descriptor array the translator builds for it, its size and how it is filled (`shader_recompiler/ir_opt/texture_pass.cpp`). |
| astc | exact, either of two bytes | the emulator's ASTC decoder: weight and endpoint unquantization, the integer sequence, the partition hash, infill, dual plane, and the error colour for HDR modes and illegal blocks (`host_shaders/astc_decoder.comp`, `textures/astc.cpp`). |
| eac | exact, either of two bytes | the EAC formats: their mapping to a host format and the decode on a host that has none (`texture_cache/format_lookup_table.cpp`, `surface.h`, `maxwell_to_vk.cpp`). |
| etc2 | exact | the ETC2 colour formats: the decode on a host without ETC2 textures, mode by mode, and the punch-through rules (`maxwell_to_vk.cpp`, `texture_cache/util.cpp`, `ConvertImage`). |
| bcn | exact | the BCn decode on a host without BCn textures, across the layers and the small levels of an array (`texture_cache/decode_bc.cpp`). |
| zeta | exact | a depth texture's upload: 24-bit depth converted for a host without it and taken from the right bits (`vk_texture_cache.cpp`, `TransformBufferImageCopies`). |
| swizzle | exact | the block linear unswizzle an emulator runs when a texture is first used, by texel size and block height (`textures/decoders.cpp`, `host_shaders/block_linear_unswizzle_2d.comp`). |
| texel | exact | the offset a buffer texture's host view starts at, when the host's texel buffer alignment is coarser than the guest's (`vk_buffer_cache.cpp`, `Buffer::View`). |
| stream | exact | uploads staged through a ring inside one list: a later draw's bytes handed to an earlier one when the ring wraps (`vk_staging_buffer_pool.cpp`, `GetStreamBuffer`), or an upload path that asks the host for far more memory than the guest data it moves. |
| fill | exact | fill rate with and without depth; the time, not the verdict, is the point: a depth buffer the host cannot compress shows as the second case slowing against the first. |
| ring | exact | a draw's descriptors taken from a ring after its geometry is bound: the ring's wait for the GPU submits the binds apart from the draw, which is lost (`renderer_vulkan/vk_descriptor_buffer.cpp`, the order of `vk_graphics_pipeline.cpp`, `ConfigureImpl`). |
| occlusion | exact | a query bank reset inside a draw or clear's render pass: the pass ends and the draw or clear lands outside it (`renderer_vulkan/vk_query_cache.cpp`, `SamplesQueryBank::Reset`); or counts that are not the pixels drawn. |
| drawtex | exact | the state DRAW_TEXTURE leaves bound: the next draw keeps the blit's pipeline or its viewport and scissor (`renderer_vulkan/blit_image.cpp`, `BlitColor`, and `vk_rasterizer.cpp`, `DrawTexture`). |
| query | exact | the samples counter (a count scaled with the render target, `vk_query_cache.cpp`) and the render enable over values and reports (`query_cache.h`, `AccelerateHostConditionalRendering`; `maxwell_3d.cpp`, `ProcessQueryCondition`), draws and clears alike. |
| nifm | exact | the network state a program is told: the connection status, the address, and a request's life from submitted to accepted (`hle/service/nifm/nifm.cpp`, `IRequest`, `HasConnection`). |
| dns | exact | name resolution: the two serialisations sfdnsres writes, nsd's placeholder, and, signed in, the rewrite table and the loopback redirect (`hle/service/sockets/{sfdnsres,nsd}.cpp`, `core/online_initiator.cpp`, `ResolveUrl`). |
| bsd | exact | sockets on the host: bind, datagrams, streams, a refused connect, a receive timeout (`SetSockOptImpl`, and `internal_network/network.cpp`), a connect that does not block, and the duplicate an ssl connection is handed, which shares the program's socket and dies with it (`hle/service/sockets/bsd.cpp`, `DuplicateSocketImpl`, `CloseImpl`). |
| ssl | exact | the secure connection: its defaults and getters, the descriptor it hands back, verification, an imported certificate, the handshake, and a request and its answer (`hle/service/ssl/ssl.cpp`, `ssl_backend_openssl.cpp`). |
| acc | exact | the account a signed in session reports and the id token it caches (`hle/service/acc/acc.cpp`, `IManagerForApplication`, `EnsureIdTokenCacheAsyncInterface`). |
| friend | exact | the blocked list and the profiles a session fetches (`hle/service/friend/{friend,profile_query}.cpp`). |

What comes next, in this order, each its own change with its own critic:

- **the net family, sharper**: a rewrite whose destination has an address of its own, so the two
  sfdnsres rewrite cases can tell a published rewrite from the session's loopback redirect (today
  the fixture's destination is `localhost`, which the redirect also gives); and a verifying
  handshake with a name the certificate does not carry, which must be refused.
- **another emulator**: the same binary under Ryujinx, with its own keys, and the two reports diffed
  against Eden's and a console's. Nothing in this program depends on the emulator it runs under, and
  that run is what proves it.

Two rules hold across all of them. Every case computes what it expects on the processor and checks
every pixel, so there is no stored image anywhere and no machine's answer is the reference for
another's. And every rule a case judges by, wherever it is more than one line of arithmetic, is
implemented once in `source/` and pinned by the `reference` suite, which runs with no graphics
device: `make -f host.mk host-run` proves the judge before a console or an emulator is involved.

The picture a failing case leaves and the detailed mode that shows it are support, not suites.
Neither decides anything, neither is compared to anything, and no run's answer changes if either is
turned off; they sit in the `support` row only because that suite checks them.

## What it reports

One JSON object per line, to two channels at once, because the two machines read different ones:

- the debug string service, which an emulator logs with no setup and hardware ignores unless a
  debugger is attached;
- a file on the memory card, which is what a console run leaves behind.

A case line carries its tier next to the result:

```json
{"type":"case","suite":"clear","name":"clear to opaque red","tier":"exact","result":"pass","detail":""}
```

The tier is on the line because a pass means nothing on its own: two machines agreeing is worth
something only if both were held to the same tolerance, and a reader diffing two reports cannot
otherwise know which one a case used. The values are `exact`, `ulp` and `position`, and one that is
not a tier at all, `none`: any line, in any suite, that reports no comparison of something the
hardware produced. That covers the two suites that judge this program's own arithmetic, every case
that reports whether a precondition held (from the device coming up to a texture reaching the
graphics unit), and any drawing case stopped before it could compare anything, which still gives the
reason in its detail. Calling any of those `exact` would read, in a diff, as a passing claim about a
machine that was never asked anything.

A case that could not ask its question at all says so with the result `skip`, the none tier, a
`reason` of one word and a sentence in `detail`, and is counted apart from passes and failures,
since a machine that was never asked has agreed with nothing:

```json
{"type":"case","suite":"acc","name":"the id token refresh completes and succeeds","tier":"none","result":"skip","reason":"not configured","detail":"needs an online session: ..."}
{"type":"summary","passed":424,"failed":9,"skipped":27,"crashed":2}
```

| reason | why the case did not ask |
|---|---|
| `crashed` | the program ended inside it on this machine: in this run, which the journal says, or in an earlier one, which the crash list says |
| `may crash` | it is in the may-crash table, and neither the word `fatal` nor the start screen asked for it: today the SM4 case, the two geometry shader cases and the three bcn array cases |
| `hangs` | it is in the hang table, and the mode word `hang` did not ask: today the bsd receive that asks for a timeout and does not come back. `retry` does not run it |
| `not configured` | the net file does not name what it needs: a session, a server, a rewrite, an account, a certificate |
| `unavailable` | a service or a resource this program cannot have here |
| `depends` | a case before it did not leave what it needs: it did not run, failed, or gave nothing to ask about |

The summary counts crashes on their own: `skipped` is every skip but the ones whose reason is
`crashed`, which `crashed` counts, so the four numbers add up to the case lines. The runner's
`EDENPROBE DONE` line on the debug channel carries the same four.

A run that dies halfway still leaves everything up to its last finished case, and the summary
carries the counts so a runner does not have to parse the rest. The summary is the last line of
every run but one: a detailed run nobody attended adds one note after it (see "Looking at it on the
console").

## Time per case

Every line a drawing suite reports ends with how long the case kept the graphics unit busy, in
microseconds, and every drawing suite ends with a line that sums its cases up:

```json
{"type":"case","suite":"clear","name":"clear to transparent cyan","tier":"exact","result":"pass","detail":"","us":231}
{"type":"perf","suite":"clear","timed":4,"total_us":7161,"min_us":192,"max_us":6530,"median_us":219,"submits":9,"samples":9}
```

**What is measured.** Every piece of work a case puts on the graphics unit goes through one function
in `source/gpu.c`, which reads the system tick when it closes a command list and again when the
queue stands idle after it. A case's time is the sum of those intervals since the line before it:
its clears, uploads, draw and readback. A line with no work since the previous one has no `us` (the
device opening, a shader loading, a case judged from another's readback). Recording the commands,
judging the pixels and writing the line or a picture happen outside those intervals, so a number is
the machine's work, and a slower judge cannot move it.

**How it is repeated.** A drawing suite runs ten times: a warm-up whose numbers are dropped, eight
rehearsals with the report muted (no line, no count, no picture), and the verdict pass, the only one
a reader sees. Each line carries the smallest of the case's nine samples, because everything else on
the machine only ever adds time. Five runs of a build that kept every sample settled both choices:
over the ten pairs, the minimum of nine put 92 percent of cases within a tenth of the other run, the
median of nine 79, and past nine bought almost nothing (93 at thirteen). The perf line's median is
over the suite's cases (the middle one, or between the two middle ones rounded down); `samples` is
how many runs each number is the smallest of, and `submits` how many submits the timed cases made in
the verdict pass.

**Why no verdict moves.** Each suite opens its own graphics device and closes it before it returns,
so a rehearsal leaves the verdict pass nothing but the emulator's shader cache, which a second
`make run` inherits anyway. Checked on the build with the `format` suite, the grown `intmath` and
`fmath` and the `net` family: all 460 case lines the same once `"us"` was taken off, failures and
skips included, in six quick runs and a `notime` run, and the failures' pictures identical. The one
allowance is the net family's `detail`, which carries the ports and buffers a run was given.

**When a rehearsal hangs**, the run says where three times. The report has one note before each
drawing suite's rehearsals, and a report that ends on it stopped while that suite was being timed:

```json
{"type":"note","text":"timing depth: 1 warm-up and 8 rehearsals before the verdict pass"}
```

The debug channel, and only it, gets a line before every rehearsed case, so the emulator's log ends
on the case that was about to run:

```
EDENPROBE TIMING depth pass 4 case a fragment nearer than the buffer passes
```

Passes count from one, the warm-up first. The warm-up has no earlier pass to learn names from, so
its lines number the case, `pass 1 case number 3 after a depth clear writes the value it was given`,
meaning the case after the one named; one past the suite's last case is closing the device. The log
line counts only when the report ends on the timing note: a hang in the verdict pass leaves the
report ending on the case line before it, and a stale timing line in the log. The third is the
journal on the card, whose last line names the case and the pass by name, the warm-up's included;
on a console with no debugger it is the one that says which case.

`notime` in the mode file (`make run MODE="quick notime"`) skips the warm-up and the rehearsals:
every drawing suite runs once, no line carries `us`, no perf line or timing note is written, and the
mode note says `no timing passes`. That is the old drawing run case for case, plus the nineteen
support cases that check the timing, and the way to get the case line a hang stops at.

**What a number can see.** Every submit costs a round trip, whatever it carries. Under eden-cli,
over the 374 case lines under two milliseconds in two runs at a load near eight, a case's time is
about 145 microseconds plus 31 per submit, or 80 per submit taken alone (the middle half between 73
and 102). The targets are 64 by 64, so for most cases the round trip is the whole number: a case of
three submits takes about 235 microseconds, its floor and not its draw. The critic proved it with
one case changed at a time, 40 comparisons of three runs a side:

| one case changed to | its time | listed |
|---|---|---|
| read its target back twice | 190 to 270 microseconds to 320 to 370 | 40 of 40 |
| clear twice | about 40 more, inside the noise | 0 of 40 |
| draw its triangle twice, or sixteen times | no change | 0 of 40 |
| upload its 4 by 4 texture once more | 45 to 70 more, one round trip | 0 of 40, unsteady in 20 |

So under an emulator this watches the readbacks and the first case of each suite. A clear, a draw or
an upload of this size costs less than one round trip, about a hundred microseconds, and sits under
the floor: `submits` times the per-submit cost is the floor under a suite's total, and what is above
it is the work. A console's floor is unknown until one has run it.

Two kinds of case are dominated by something other than their draw, and the warm-up fixes neither,
since every pass opens a fresh device and the emulator pays again:

- the first case of every suite, the device's first work: 6 to 10 milliseconds where that is a
  clear, a draw and a readback. It is not a shader compiling: `clear` has no shader and pays it too.
  In `texture` and `coherence` the first work is an upload, and that case's 2 to 2.9 milliseconds is
  the device's first work too, not the upload;
- the first draw that samples a texture, 14 to 18.5 milliseconds in `texture` and `coherence`.

Those two first uploads ("the texels reach the textures", "the starting texels reach the textures")
are not one number at all: 2 to 2.9 milliseconds in most emulator processes, anything from 60
microseconds to 1.6 milliseconds in about one in five. A side of three holding such a process
spreads past the unsteady limit, about half the sides, and perf.py names the case as unsteady
instead of seeing a move (the `texture` one in 7140 of the 9240 splits below, the `coherence` one in
4620).

**The method.** Inside one process the smallest of nine samples is steady. Between processes it is
not: now and then one emulator process runs nearly every case about 13 percent slower than the next,
which no number of samples inside a process can see. So one report is one sample of the process, and
a comparison takes three runs a side, both sides run in the same sitting:

```sh
tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
```

A case's time on a side is its median over the three, which needs two processes to agree; the
smallest was tried and lost, since the one fast process then decides the side (it listed a move in 9
of 840 splits of nine runs and 16206 of 185640 of eighteen). A move is listed when it is more than
20 percent **and** more than 100 microseconds, and a case whose three runs on either side are more
than 75 percent apart is named as unsteady and not compared. On twelve runs of one build in one
sitting (six at a load of 4 to 5.5, six at 1 to 14), split every way into two sides of three, 9240
comparisons of a build with itself:

| limits | comparisons listing a move |
|---|---|
| 20 percent alone | 840 |
| 20 percent and 100 microseconds | 840 |
| 20 percent and unsteady at 100 percent, with or without the floor | 91 |
| 20 percent, 100 microseconds, unsteady at 75: the defaults | 0 |
| 30 percent, 100 microseconds, unsteady at 100 | 0 |

The unsteady rule is what keeps noise out, not the 20 percent: everything the lesser limits listed
was one of the two first uploads, and at 75 the rule sets aside those two and nothing else, while at
50 steady `blend` cases start being set aside. The floor keeps the quarter-millisecond cases quiet
on a noisier day: on the critic's nine runs of an earlier build, twenty percent with no floor listed
a move in 76 of 1680 ordered splits, and none with it. And 30 percent would have hidden a 20 to 30
percent move in exactly the cases this can watch.

On those twelve runs the sides agree on 85.4 percent of cases within a tenth at worst, 98.0 typical,
99.0 within a fifth at worst, and suite medians within 10.1 percent; on the six at a load of 4 to
5.5 alone, every case within a tenth in all ten splits and suite medians within 4.7 percent. Six
more runs of the build with the `net` family, at a load near nine, listed no move, with cases within
a tenth 99.0 percent at worst, suite medians within 7.5 percent, and the `texture` upload set aside
in all ten. One report a side is accepted for a quick look, but 32 of the 66 pairs of the twelve,
and 8 of the 15 of the six, list a case that did not change. Both sides in one sitting is part of
the method, not advice: eighteen runs from two sittings about an hour apart, at different loads,
listed a move in 24135 of 185640 splits, nearly all the two first sampling draws (about 18.3
milliseconds typical in the first sitting, 14.8 in the second): the device's first work moves with
the machine more than anything else. Run the two builds alternately if the machine is shared. The
perf line and a case's `us` stay one process's figure; the method lives in the comparison.

**Comparing two runs.**

```sh
tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
tools/perf.py before.jsonl after.jsonl      # one run a side
tools/perf.py report.jsonl                  # the per-suite table of one run
tools/perf.py r1.jsonl r2.jsonl r3.jsonl -- # the table of one side of three
```

It lists the cases that moved, largest first, then the unsteady ones, then the cases only one side
timed, then a table per suite: cases timed, the two totals, the two medians and the change in each.
`--threshold`, `--floor` and `--unsteady` change the three limits, each a finite number of nought or
more. It reads the case lines, not the perf lines, and skips a line it cannot parse, or that is not
UTF-8, with a count on standard error, so a report cut off by a killed run still compares. A report
with nothing timed (an empty file, a `notime` run, a report from before cases were timed) is refused
on standard error with exit status 1, alone or as one run of a side, rather than compared as a
hundred percent faster; a suite only one side timed says so instead of a percent. Its tests run with
the host suite.

## What a failing case leaves behind

A verdict says how many pixels were wrong and what the first one held, not that the wrong ones were
the right shape in the wrong place, or one block in sixteen, or every other column. So a drawing
case that fails writes what it saw next to the report and names the files on its own line (one line,
wrapped here to fit):

```json
{"type":"case","suite":"intmath","name":"bitwise and","tier":"exact","result":"fail",
 "detail":"4095 of 4096 pixels wrong, first at (1,0) is 0x00000000, expected 0x00106000",
 "image":"images/intmath/bitwise-and.pam","expected":"images/intmath/bitwise-and-expected.pam",
 "diff":"images/intmath/bitwise-and-diff.pam","raw":"images/intmath/bitwise-and.bin"}
```

Each field is a path relative to the directory of `report.jsonl`, present only when that file was
written. The files land under `images/<suite>/`, named after the case with letters lower cased and
every run of other characters turned into one dash: "the fragment coordinate's x is the pixel
centre, bit for bit" becomes `the-fragment-coordinate-s-x-is-the-pixel-centre-bit-for-bit.pam`. Two
case names that spell one file name get a file each: the second gets `-2` before the extension,
giving up characters off the end of the name if it must. A name is held together with the two its
pictures are spelled from, so a case called "bitwise and expected" cannot land on the expectation of
"bitwise and". After ninety eight numbers the case gets no picture and its line none of the four
fields, because one failure quietly overwriting another's evidence is worse than a missing picture;
a directory too long to hold whole is likewise refused rather than cut short.

These are not stored reference images and must never become one (see "Why not compare screenshots").
Nothing compares them to anything and no case reads one: the readback is judged, then the very bytes
the judge looked at are written. Deleting every image changes no verdict.

**The formats.** All three pictures are binary PAM, the netpbm format that carries four channels, so
a colour target's alpha is in the picture, not in a second file that can go missing. `magick`,
`identify` and the netpbm tools read them; a PNG is one `magick a.pam a.png` away.

| file | what it is |
|---|---|
| `<case>.pam` | what the machine drew, four channels, MAXVAL 255, labelled `drawn` on screen |
| `<case>-expected.pam` | what the processor worked out for that same pixel |
| `<case>-diff.pam` | three channels: the judge's verdict per pixel, labelled `judged` on screen |
| `<case>.bin` | the target's bytes as they were read back, for the formats an image cannot hold |

A pixel's four bytes go into the image in the order the readback handed them over. For a colour
target that is red, green, blue and alpha, and the picture is the picture. For 32 bit integers or
floats it is the little end first, a picture of bit patterns rather than of magnitudes: equal values
look equal and one changed bit changes a colour, while a float range has no honest mapping onto a
byte. Those formats also get their bytes verbatim in a `.bin`, for `cmp` between two machines.

**The diff is the judge's verdict, not a comparison of the two images**, and that is what makes it
safe. Dark grey means the judge accepted the pixel, red that it refused it, blue that it declined to
judge it (the band an edge runs through). Magenta means no compare loop ever looked at the pixel:
every pixel starts magenta, so a loop that skipped one cannot pass for one that meant to leave it
alone, and a run that shows magenta has a bug in this program, not in the machine. A pixel within a
budget of units in the last place differs in its bytes and is not a difference here; an image
comparison would paint the whole float suite red on a machine that did nothing wrong.

By default only a failing case writes anything; the word `images` in the mode file (next section)
asks for every drawing case. The `support` suite writes a few eight by four fixtures into
`support-check/` beside the report while it runs, parses them back and takes them away again, and a
run that leaves that directory behind has failed that case and says so.

## Looking at it on the console

One binary, two ways to run it, and a file on the memory card that chooses.

**Detailed** is the default, because whoever starts this on a console is standing in front of the
screen. Every suite runs, both report channels get every line, the summary is counted and the report
is closed, and only then does the run go on the screen, a group at a time. A group is a suite,
gathered under its family: the judge (selfcheck, support, reference), cpu, graphics (clear to fmath)
and net (nifm to friend). Three screens, one inside the other:

- **The overview** comes first, on the first group that failed. Its heading carries the run's counts
  and names the groups that failed, as many as fit. Below it, every group under its family with its
  passes, failures and skips in columns and a bar of them in proportion, a failed group's name in
  red; under the list, the selected group's failures by name, or its skips when nothing failed. A
  case the program ended inside is counted apart, as the summary counts it: in violet, in a fourth
  column that only a run with such a case has, and listed under the list when its group failed
  nothing.
- **A group** lists its cases, each marked `PASS`, `FAIL` or `SKIP` in that result's colour, with
  `pic` at the end of the row when the case kept pictures. It opens on its first failure. Under the
  list: the selected case's whole name, as much of its detail as fits, wrapped, and what opening it
  will show.
- **A case** that compared pixels opens on its three pictures side by side, labelled `drawn`,
  `expected` and `judged`, each 64 by 64 target scaled up with nearest neighbour, a key naming the
  four colours of `judged`, and the detail under them. Any other case, and a drawing case that
  failed before it read anything back, opens on its whole name and its whole detail.

The pictures answer what a verdict cannot, whether a failure looks like a rendering fault or like
the judge being wrong; the rest is for finding the case that failed in a run of five hundred without
a computer beside the console.

**Quick** is what a script wants: every suite, both channels, and the program exits, in about six to
seven seconds under eden-cli (6.4 to 7.3 in most runs at a load of 4 to 9, emulator start included),
several times that when the machine is busy. It is the same run up to the report closing.

The screens hold every case the report recorded, kept in memory as its line went out, exactly as a
reader of the line gets it back, plus the pictures the judge looked at. Nothing is read back from
the memory card, so a green run and a run that wrote nothing are both worth looking at, and `images`
changes what is on the card, not on the screen. A quick run keeps and allocates nothing for any of
it. The store holds 4096 cases, eight times a full run; a run past that, or one that ran out of
memory keeping them, says on the overview how many of the report's cases the screens hold.

The order is the safeguard: the viewer refuses to start while a report is open, so both modes leave
the same verdicts and the same images, line for line. The report differs only in the note near the
top that says which mode ran and why, and in the case below. The screen is a second opinion for a
person, never an input to one.

**The start screen.** A detailed run with something that could end it, a case the may-crash table
names in the families it runs or a case on the crash list, first shows both lists and two choices,
each off unless the mode file's word turned it on: Y runs the cases that may crash (the word
`fatal`), X the crash list's again (the word `retry`). Y and X in one edge change neither choice.
While either is on, a warning says what it
means: the program may end, and started again it goes on after the case that ended it. A begins
the run; nothing pressed for ten seconds begins it as the file had it, and any press keeps the
screen. What was chosen is in the mode line (`chosen on the start screen`) and in the journal, so a
launch that goes on with the run keeps it. The screen decides the mode, as holding X does, and reads
no case and writes no line.

**Nobody there.** A detailed run whose screen sees no button pressed for ten seconds after the
report closed leaves by itself, and appends one line after the summary:

```json
{"type":"note","text":"detailed mode: nothing pressed for 10 seconds after the report closed, left by itself"}
```

That is for a script that forgot the mode file, which must not leave an emulator running until
something kills it; ten seconds because on a console the person is reading the overview when the
count starts. A press counts, not a connected controller, because an emulator presents a controller
as connected whether or not anyone holds it. The countdown shows on the bottom line, and any button
stops it for the rest of the run. The first eight frames are not a press, nor navigation: a
controller arriving with the window reports its whole mask as newly pressed. An edge that holds
both Y and X changes neither choice, because a pad that connects after those frames reports the
same whole mask once; A in that edge still begins the run. Two presses, one button each, still
choose both.

**The mode file** is `sdmc:/edenprobe/mode`: a few words in any order and any case, anything that is
not a letter between them.

| word | what it does |
|---|---|
| no file, or no `quick` in it | detailed |
| `quick` | the automatic run: every suite, the report, exit |
| `detailed` | detailed even beside `quick`, the same as holding X |
| `images` | a picture for every drawing case, passing ones too, on either mode |
| `notime` | no timing passes: each drawing suite runs once, and no line carries a time; see "Time per case" |
| `stress` | the heavy variants too, today the stream suite's 256 MiB list; off by default, since an emulator that runs out of memory on one takes the rest of the run with it |
| `fatal` | the cases the may-crash table names, known to end the program on a machine that gets them wrong: today SM4EKEY and SM4E, which the console's processor lacks; off by default. On a console it costs a relaunch per case its processor lacks |
| `hang` | the cases the hang table names, known to never return on a machine that gets them wrong: today the bsd receive-timeout case; off by default. `retry` does not run one, and the start screen cannot turn this on |
| `retry` | the cases on the crash list run again; one that runs to its end leaves the list (see "When the program ends in the middle"). A case the hang table names stays skipped until `hang` asks too |
| `nojournal` | nothing is written between cases: no journal, so a crash costs the rest of the run as it did before the journal; for measuring what the journal costs |
| `cpu` | the processor family: the cpu suite runs in every mode, so naming it alone keeps the graphics and net families out, a run of a few seconds with nothing drawn |
| `graphics` | the graphics family: the twenty seven drawing suites, clear to query; selfcheck, support, reference and cpu always run, whatever family is named |
| `net` | the net family: nifm, dns, bsd, ssl, acc and friend, against what `sdmc:/edenprobe/net` names |

Families choose what runs beyond the judge, which always runs because nothing a family reports can
be believed without it, and beyond the cpu suite, which needs no device and runs in every mode. None named means every family this build has; a named family this build
lacks is listed in the mode line and skipped, so a file written for a later build is reported, not
refused. `net` alone runs the judge and the six net suites, which is what the fixture below writes.
Any other word is reported in the mode line, quoted and counted, and ignored: a typo costs a line in
the report, never the run.

The first 4096 bytes of the file are read. A longer file is read that far and the mode line says
"mode file longer than 4096 bytes, the rest ignored"; a word the limit cuts in two is dropped rather
than reported as an unknown word nobody wrote. A directory called `mode` reads as no file, and the
mode line says so.

```json
{"type":"note","text":"mode: quick, from the mode file; pictures of failures only; families graphics; 2 unknown words ignored: grpahics fast"}
```

**Holding X** as the program starts is the other way into the detailed mode, for a card that says
`quick` and no way to change it. The pad is asked only then, once it turns up: a frame or two with a
controller attached, twelve frames at most. The report is opened before any of this, so a machine
that cannot read the file or reach the pad still leaves a report saying how far it got.

On the screens, each of which names its buttons on its bottom line:

| button | overview | group | case |
|---|---|---|---|
| up, down | the group before or after | the case before or after | the case before or after |
| left, right | the first group of the family before or after | the group before or after, on its first failure | the case before or after |
| A | opens the group | opens the case | |
| B | | back to the overview | back to the group |
| R or ZR, L or ZL | the next or previous group that failed | the next or previous failure in the group | the next or previous failure in the group |
| X | | | the pictures: all three, then each alone |
| plus or minus | quit | quit | quit |

And on the start screen: A begins the run, Y turns the cases that may crash on or off, X the crash
list's cases again. Y and X in the same edge change neither.

Every list comes round at either end. A direction held down acts again after twenty frames and then
every four, so the reference suite's hundred and eighteen cases do not take a press each; what
repeats is the direction pressed, whatever else is held, and only after a press counted past the
first eight frames. Going back lands on the group or case that was open, and the view chosen with X
stays from one drawing case to the next. A frame is composed on the processor only when something
moved or the countdown ticked, so a still screen costs an emulated machine nothing.

## When the program ends in the middle

A case can end the program that runs it: a shader an emulator cannot translate, an instruction its
processor lacks, a list asking for more memory than it has, a service it has not written. Once,
such a case cost the rest of the run. Now it costs the case: the run goes on over as many launches
as it takes, and one report covers it all.

**The journal.** `sdmc:/edenprobe/journal` says what the run has done, a line at a time, each on
the card before the program goes on: before a case starts, its name and the pass; after it ends,
its result. A launch that finds a run whose last line is a case starting knows the program ended
inside that case, and goes on with the run instead of starting another. It skips the steps the
journal says are done, cuts the report back to where the step under way began, reports the case as
a skip whose reason is `crashed`, puts it on the crash list, and runs the step again without it;
everything after it runs as it would have. The launch says so in a note where the report goes on,
and the note before the summary says how many launches the run took, how many cases it skipped as
crashed, how many of those only because the crash list named them, and which cases the program
ended inside in this run:

```json
{"type":"note","text":"journal: launch 2 resumes the run; the program ended inside index case \"an indexed draw with the library's open limit uses every index\" in a timed pass: that case is skipped as crashed and goes on the crash list, and step index runs again from its start"}
{"type":"case","suite":"index","name":"an indexed draw with the library's open limit uses every index","tier":"none","result":"skip","reason":"crashed","detail":"the program ended inside it earlier in this run, in a timed pass; ..."}
{"type":"note","text":"journal: this run took 2 launches; 1 case skipped as crashed; the program ended inside index \"an indexed draw with the library's open limit uses every index\""}
```

And in the next run, which skips it from the crash list without ending:

```json
{"type":"note","text":"journal: this run took 1 launch; 1 case skipped as crashed, 1 of them from the crash list"}
```

A step is what main runs as one: a suite, or the whole net family. A launch goes on with a run only
when the journal is this build's, was begun with the same mode words (quick and detailed aside,
since they change nothing a report holds), and the report it describes is still there and at
least as long as the journal says it was; otherwise it starts the run again, and says why in a
note. A case the journal shows the program ended inside goes on the crash list whichever way the
launch goes. To start over rather than go on, delete the journal or the report.

| journal line | written |
|---|---|
| `run <version> <build> <mode words>` | first, when a run begins |
| `chosen <words>` | after it, when the start screen added `fatal` or `retry` |
| `launch <n>` | each time the program starts on the run, 1 the first |
| `step <step> <offset> <passed> <failed> <skipped> <crashed>` | before a step: the report's length then, and its counts |
| `pass <step> <n>` | before each pass of a drawing suite, 1 the warm-up and 10 the verdict pass |
| `start <suite> <pass> <case>` | before a case's work; pass 0 outside the drawing suites |
| `end <suite> <pass> <result> <case>` | after its line: `pass`, `fail`, `skip`, `crashed`, or `muted` in a rehearsal |
| `crashed <suite> <pass> <case>` | by the launch after, for the case the program ended inside |
| `done <step>` | after a step |
| `finished <passed> <failed> <skipped> <crashed>` | after the summary |

A last line with no newline is a write the program did not live to finish, and is never read as a
record; nor is a line that is not a whole one.

**The crash list.** `sdmc:/edenprobe/crashes` names the cases that ended this machine's program,
`<suite> <case>` a line under a comment that says what it is. They are skipped in every later run
as `crashed`, until the mode word `retry` runs them again; one that then runs to its end, through
every pass, leaves the list, and a note says so. A case that ended the program in this very run is
skipped even under `retry`, since running it is what ended the launch before. A case the hang
table names stays skipped as `hangs` whether or not `retry` asks, until `hang` does too. The file
can be edited by hand, one line deleted to try one case again; the program writes it whole when
it changes, so a comment typed into it does not stay.

**The may-crash table.** `source/maycrash.c` names the cases known to end the program on a machine
that gets them wrong, each with its suite, its family and why, in one table. They are skipped with
the reason `may crash` and the table's sentence unless the mode word `fatal` or the start screen
asks for them (see "Looking at it on the console"); a machine that then ends inside one goes on
with the run after it like any other. A case on the crash list stays skipped as `crashed` whether
or not `fatal` asks, until `retry` does too; a case that ended the program in this run is skipped
whatever either says. A launch whose suites never reached a case the table names says so in a note
before the summary, since an entry naming a case this build lacks protects nothing.

**The hang table.** `source/hang.c` names the cases known to never return on a machine that gets
them wrong. They are skipped with the reason `hangs` and the table's sentence unless the mode word
`hang` asks for them. `retry` does not run one: a hang does not end the program, so there is no
next launch to mark it until something else kills the process, and running it again hangs that
launch too. Today the table has one case, the bsd receive that asks for a timeout. The start
screen cannot turn `hang` on. A case that ended the program in this run is still skipped as
`crashed` before the hang table is consulted, whatever `hang` says.

**Where a crash can be pinned.** Every case of the cpu, graphics and net families says it starts
before any of its work, which is what lets a launch skip it: the start and the skip come before the
first instruction, list or service call the case makes. A case judged from another's work (two
halves of one readback, a request asked about again, a token read twice) is skipped as `depends`
when that one does not run. The judge's cases ask the machine nothing but ordinary arithmetic and
are journaled only as they end. What runs between two cases belongs to none: a suite reopening its
device after a fault, blend's two probe notes, the net family opening a service or waiting for
the sign-in. A crash there costs the rest of that step, which keeps what it reported and is not
run again, and the launch says so in a note. A case that reports a verdict without having said it
started, where it had to, is a bug in this program, and a `bookkeeping:` note says which.

**What it costs.** Two lines for each case in each pass, and a drawing suite runs ten passes: a run
of 670 cases writes 7228 lines, 474 kilobytes, none of them inside a timed interval, which is only
the submits and the waits after them. Under Eden, three runs a side, that run took 7.00 s against
6.81 s with `nojournal` (3 percent, some 27 microseconds a line), and `tools/perf.py` moved no
timed case past its threshold; the machine was not idle, so the two times are indicative.
`nojournal` in the mode file writes nothing between cases, and a crash then costs the rest of the
run.

**On an emulator that buffers.** Eden writes the card through the host's C library and seeks before
every write, so a file's last write sits in a buffer until the next one comes, and dies with the
process. Each line is therefore written without its newline, flushed, and then the newline is
written at the end: the flush puts the line on the host's disk, and what a crash can still hold
is the newline. Seeking back onto the line to rewrite a byte already there drops, on the Android
host library, every buffered byte past the seek. A finished journal then keeps the offsets while
the report loses whole suites and stays a dense jsonl, shorter by exactly those suites. While the
journal is on, the report is written the same way, which makes true on Eden what a killed run
always promised, everything up to its last completed case.

**Under Ryujinx**, which a person starts by hand: open `edenprobe.nro`, and when Ryujinx stops or
closes, open it again, and again, until the report ends on its summary; each launch goes on with
the run. Its card is Ryujinx's own, `~/.config/Ryujinx/sdcard/edenprobe/` on Linux (or
`$XDG_CONFIG_HOME/Ryujinx/...`, or `portable/sdcard/` beside a portable build): the report, the
journal and the crash list are there. A console is the same: start the program again from the
menu after it ends.

## Versions

The version is the one line of `VERSION`, semantic (`0.1.0` was the first), and a tag `v<version>`
on `main` marks each. Every report opens with a note naming it and the build, as git describes the
tree: `edenprobe 0.1.0 (9632b63) starting`, or `-dirty` when uncommitted changes were built. Two
reports are worth diffing only when that line agrees, or when the difference between the builds is
what is being measured. The version moves when what a case asks or how it judges changes: a new
suite or case is a minor step, a changed verdict for an existing case at least that, and the
README's counts move with it.

The cover a loader shows is `icon.jpg`, drawn from `static/icon.svg` (the reden mark, the Eden
sunset without the palm, on the site's dark ground) by `make icon`; the JPEG is committed so a build
needs neither drawing tool.

## Building

For the console, with the toolchain installed:

```sh
export DEVKITPRO=$HOME/devkitpro
make
```

That produces `edenprobe.nro`. The same file runs on hardware and in an emulator, unchanged: two
reports are only comparable if the binary is identical.

The host build compiles the parts that touch no console and runs them on the machine you are sitting
at:

```sh
make -f host.mk host-run
```

It runs the three judge suites, so a broken comparator shows in a second instead of after a boot,
along with what is written and drawn around a verdict: the picture writer against a fixture it
parses back, the screens and the navigation between them against frames composed in ordinary memory
from a fixture shaped like a real run, the store the screens read, the mode file's words, and the
time a case reports. Last it runs the tests of the tools, `tools/perf.py`, `tools/run.py` (with a
script standing in for the emulator, crashing, hanging and finishing as each test asks) and
`tools/validate.py`, which need Python 3 and nothing else.

To look at the screens without a console, name an existing directory in `EDENPROBE_FRAMES`:

```sh
EDENPROBE_FRAMES=/tmp/frames make -f host.mk host-run
```

The screen cases then also write each frame of their walk there as a PAM file: the overview, a
failing graphics group, one of its cases on its pictures and alone on `judged`, the group again, a
net case's detail page and a net group; `magick 1-overview.pam 1-overview.png` makes a PNG of one.
The frames are never read, so they change no verdict.

Commit messages have a convention too: `edenprobe: what changed`, one to three lines of intent, one
Signed-off-by that is also the author and the committer, ASCII, no merges.
`git config core.hooksPath hooks` once per clone turns the check on; `sh hooks/test-commit-msg.sh`
shows what it accepts and `--history <range>` runs it over existing commits.

## Running it under an emulator

```sh
make run
```

`EMULATOR` points at the binary to test: `make run EMULATOR=/path/to/emulator`. With nothing given
it is an eden-cli build kept in `~/WorkDir/accord/bin`.

The run leaves `report.jsonl`, and `images/` beside it, in `sdmc:/edenprobe`: the memory card on a
console, and on a desktop a directory the emulator keeps under `$XDG_DATA_HOME`, beside its settings
and caches under `$XDG_CONFIG_HOME` and `$XDG_CACHE_HOME`. `make run` points all three at
`build/xdg/` inside this tree, so the report is at `build/xdg/data/eden/sdmc/edenprobe/report.jsonl`
(the target's last line prints it) and `make clean` takes the lot away. That is not tidiness: a
frontend saves its settings when it exits, so a run pointed at your own directories reads your
settings and may rewrite them, while one pointed here starts from the emulator's defaults every
time, which also makes two runs comparable. An emulator with a different layout puts the same names
under whatever it calls its memory card.

The target runs `tools/run.py`, which first deletes the previous report, its pictures and the
support suite's scratch directory, so a run that never started cannot leave an old report looking
like its own, and a green run cannot sit beside the pictures of an earlier failing one. The journal
stays: the program reads it, starts a new run because the report is gone, and lists a case the last
launch of an earlier run ended inside.

**It starts the emulator again after a crash.** A launch that ends with no summary in the report,
crashed or stopped at its time limit (`TIMEOUT`, 60 seconds, then killed ten after that), is
followed by another, which goes on with the run after the case the last one ended inside (see
"When the program ends in the middle"), up to `RELAUNCHES` more (16). Each launch gets a line:

```
make run: launch 1 ended by signal 6 (SIGABRT) inside bcn case "every layer of a six-layer BC4 array keeps its own 2x2 and 1x1 levels" (the warm-up); launching again (2 of at most 17)
```

A launch that leaves the journal as it was, or adds nothing to it but its own start, ends the
relaunching, since the next would do the same; so does a run with no journal (`nojournal`). At the
end the target prints the counts, then, on a line of its own, the cases skipped as crashed and the
ones this run ended inside, and the number of launches. A missing or non-executable emulator, or a
run that never gets its summary, stops the target with a line saying which and a non-zero exit;
stopping it by hand stops the emulator and writes `stopped` into the journal, so the case under
way is not taken for a crash.

**The crash list is kept per emulator binary**, under `build/xdg/crash-lists/`, and put on the card
before the first launch and taken back after the last, so a case one build crashes on is not
skipped on another's run. A rebuild, a binary of another size or time, starts a list of its own.
To try one case again under `make run`, delete its line in the list kept there, named after the
binary: the card's copy is replaced from it before the first launch.

Before the emulator starts, the target writes `quick` into the mode file and says so, because the
detailed screen is for a person. `MODE` is a variable too: `make run MODE="quick images"` keeps
every picture, `make run MODE="quick fatal"` runs the cases that may crash, and `make run MODE=`
writes no file, so the start screen and the results come up, each counting down ten seconds, and
the program leaves by itself, which is the way to see them from here. `XDG` moves the whole tree:
`make run XDG=/elsewhere` starts from an empty card there.

The run needs a real rendering backend. On a null or headless backend the selfcheck cases still
report and the device still opens, then every drawing case fails with every pixel reading zero,
because a backend that draws nothing is doing exactly as asked: a property of the run, not a defect
found. Such a run is still worth doing. The graphics library runs on the emulated processor, so
everything it checks about the commands it is given is checked for real: a shader module it will not
load, a copy it will not make or a rectangle it will not clear ends the program rather than failing
a case. A headless run that reaches its summary has shown none of that happened, which is most of
what can go wrong before a pixel is involved.

## Running it on Android

The same `.nro` runs under the Android build of an emulator; the report is the same file under
the app's own memory card. For Eden, whose package is `dev.eden.eden_emulator` (a suffix such as
`.relWithDebInfo` on other build types; `adb shell pm list packages | grep eden` names it), one
run is:

```sh
adb install -r Eden.apk && adb push edenprobe.nro /sdcard/edenprobe.nro && \
adb shell 'd=/sdcard/Android/data/dev.eden.eden_emulator/files/sdmc/edenprobe; mkdir -p $d && rm -f $d/report.jsonl && echo quick > $d/mode'
# open /sdcard/edenprobe.nro from the app, wait for it to leave by itself, then:
adb pull /sdcard/Android/data/dev.eden.eden_emulator/files/sdmc/edenprobe/report.jsonl report-android.jsonl
```

Two builds of one package share that directory, so run them one after the other and pull the
report in between. When the app stops in the middle of a run, open the program again: it goes on
with the run (see "When the program ends in the middle"), and the report is whole once it ends on
its summary. The device and driver are in the report's first lines only as far as the
emulator logs them; say which phone and which driver next to the file.

## The validation layer's view

`make validate EMULATOR=...` runs the graphics family once under the Khronos validation layer,
through `tools/run.py` like `make run` (a launch that crashes is followed by one that goes on),
synchronisation validation on (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`,
`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`), with the same XDG override as `make run`, and prints
one row per message id: the VUID or the SYNC-HAZARD, how many times, and the case it came
before, since the layer speaks while a case works and the case reports when it is done.
`tools/validate.py diff a.log b.log` lists the ids whose counts moved between two emulators. It
is the host half of what the suites check: a wrong index buffer size or an image created with
view formats it cannot have is a VUID here long before it is a wrong pixel on a forgiving
driver. It needs the layer installed on the host, and it is slow: several times a plain run.

## Making the driver look older

Some emulator paths run only on an older driver: a device that reports Vulkan 1.1 enables later
features through their extensions, and one without host query reset resets queries in command
buffers. `tools/vklayer` builds `VK_LAYER_EDENPROBE_masquerade`, a Vulkan layer for the host that
runs the emulator, which makes this machine's driver look like such a one to the emulator above
it: with `EDENPROBE_MASQUERADE_API=1.1` the physical device reports Vulkan 1.1, and with
`EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1` it has no `VK_EXT_host_query_reset` and no
`hostQueryReset` feature. Nothing else changes, and the driver underneath is the same.

```sh
make -C tools/vklayer
VK_ADD_LAYER_PATH=$PWD/tools/vklayer VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade \
EDENPROBE_MASQUERADE_API=1.1 EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1 eden-cli -g edenprobe.nro
```

`make -C tools/vklayer check` asks the first device, through the loader, what the layer lets
through, and submits nothing: `api 1.1 hostQueryReset 0 VK_EXT_host_query_reset 0` with both
masks on, and the driver's own answer with neither. The ring and occlusion suites were written
for it: on RADV, which is Vulkan 1.4 with host query reset, Eden takes neither path without it.

## The net family

The net family needs to know what the machine's backend promises, and a file on the memory card says
it: `sdmc:/edenprobe/net`, one `key value` per line, `#` to the end of a line a comment, and a line
it does not understand counted and named in the report and otherwise ignored.

| line | what it promises |
|---|---|
| `session <origin>` | an online session in the sense of reden's `docs/OnlineService.md` is configured against this backend, so the cases that ask one run |
| `account <16 hex digits>` | the account id its `/api/v1/config` publishes |
| `rewrite <source> <destination> <a.b.c.d>` | a rewrite it publishes, and the address the destination has |
| `blocked <16 hex digits>` | one entry of its `/api/v1/blocked` |
| `profile <16 hex digits> <url or -> <name>` | one entry of its `/api/v1/profiles`, the name running to the end of the line |
| `server <a.b.c.d> <port>` | a game server reachable over TCP and TLS; an address, not a name, so a server known by name is resolved by hand first |
| `server-name <name>` | the name to present to it, which its certificate has to carry as a subject alternative name for the verifying cases to pass |
| `server-cert <path>` | the certificate it presents, PEM, relative to `sdmc:/edenprobe` |

With no file, every case that needs a backend is skipped and the rest still run: loopback sockets, a
dotted address resolving to itself, a connection's defaults. Without a `session` line nothing asks
acc or friend for anything that reaches a server, which on a console would be the platform's own. A
console run against a deployed game server names `server`, `server-name` and, when no authority
signed the certificate, `server-cert`; the same file runs there and here. By hand, for a server at
203.0.113.7 whose certificate names `game.example.org`, copied to the card as
`sdmc:/edenprobe/game.pem`:

```
# sdmc:/edenprobe/net
server 203.0.113.7 443
server-name game.example.org
server-cert game.pem
```

And every key, as the fixture below writes it for a backend here (its ports are picked free at run
time; the session lines only make sense against an emulator configured for that backend):

```
session http://127.0.0.1:41234
account bb00000100000002
rewrite game.edenprobe.invalid localhost 127.0.0.1
blocked bb00000100000009
blocked bb0000010000000a
profile bb00000100000002 http://127.0.0.1:41234/avatar/{size}.png edenprobe
server 127.0.0.1 8443
server-name redenex
server-cert net-server.pem
```

**Here**, `tools/net-fixture.sh` does all of it and takes it down again:

```sh
EMULATOR=/path/to/eden-cli tools/net-fixture.sh
```

It builds redenex from `REDENEX` (default `~/WorkDir/accord/redenex`) and starts it on a free port
with a certificate made for the name `redenex` (as a subject alternative name). It starts reden's
reference online backend from `REDEN` through `tools/net-backend.py` on another free port, with one
rewrite, a profile and two blocked ids to publish, and asks it over its own protocol (discovery, a
token per scope, the three reads) what it publishes, writing the answers into the net file: an
expectation is only ever what the backend said. It puts the three `reden_online_*` settings in the
scratch `XDG_CONFIG_HOME` that `make run` uses (eden-cli's `eden/sdl2-config.ini`, group
`[Services]`), runs `make run MODE="net quick"` (which starts the emulator again after a crash),
prints passes, failures and skips per suite, and
stops both servers. The settings file is copied aside first and put back however the run ends, and
the net file and the certificate come off the scratch memory card, so a later plain `make run` skips
rather than fails against servers that are gone. redenex keeps its database on port 45432 (under
`~/.local/share/redenex`) and adopts one already running there; one that loses the race with another
redenex bringing that database up is started again, three tries in all, each with its own log
(`build/net/redenex-1.log` and on). Building it writes Go's caches under the home directory, as any
`go build` does.

It fails loudly rather than print a table: a server that does not come up, `make run` exiting
non-zero (no emulator there, a build error, a run with no summary), or output with no
`EDENPROBE DONE` line ends it with one line saying which and a non-zero exit, and the previous
report is deleted before the run, so nothing from an earlier run can pass for this one's.
`NET_CONFIG_DELAY=6` makes the backend hold `/api/v1/config` for six seconds, which is how the
sign-in wait is tested.

## Credits

- **switch-examples**, https://github.com/switchbrew/switch-examples: `source/viewer.c` reads the
  pad the way `hid/read-controls` does (`padConfigureInput`, `padInitializeDefault`, `padUpdate`,
  `padGetButtonsDown`, `padGetButtons`) and drives a linear, double buffered framebuffer the way
  `graphics/simplegfx` does. Its README marks the examples with the Creative Commons Public Domain
  Mark 1.0; the repository has no licence file of its own.
- **libnx**, https://github.com/switchbrew/libnx: the library every call to the console's services
  goes through. ISC licence, in its `LICENSE.md` (copyright 2017-2018 libnx Authors).
- **deko3d**, https://github.com/devkitPro/deko3d: the graphics library the drawing suites use. zlib
  licence, in its `LICENSE` (copyright 2018-2020 fincs).

- **deko3d's internals and its compute example**: `source/gpu.c` `GpuRawMethods` builds a one-entry
  control list in the shape of deko3d's `source/dk_ctrlcmd.h` (`CtrlCmdHeader`,
  `CtrlCmdGpfifoEntry`), and `source/rawref.c` encodes method headers the way its
  `source/maxwell/command.h` does, with the method numbers of `source/maxwell/engine_3d.def`
  (deko3d 350f2b0, zlib licence). The order suite's pattern, a dispatch, a primitives barrier and
  a draw of what it wrote, is switch-examples' `deko_examples/source/Example09_SimpleCompute.cpp`
  and `sinewave.glsl`, which `order_copy.comp.glsl` follows in shape.

- **astcenc**, https://github.com/ARM-software/astc-encoder (Apache 2.0): ARM's reference ASTC
  codec. None of its code is in this repository; `source/astcblocks.c` carries hashes of what its
  decoder (commit 9fbec680) makes of each block the astc suite samples, and `source/astcref.c`
  follows its reading of two points the specification leaves to the decoder: bits past the end of
  an integer sequence read as nought, and an HDR endpoint mode in the LDR profile is the error
  colour on its own partition only.

## Licence

GPL-3.0-or-later; the full text is in `LICENSE`. That lets code from other GPL-3 projects come
in, and everything the program links or follows from elsewhere is under a licence compatible
with it.
