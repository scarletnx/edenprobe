# The graphics family

The twenty nine drawing suites, clear to blit2d, in the order they run.

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

- **geometry**, eight cases. The two optional stages every other suite leaves out. A geometry shader
  turns each of four points into an 8 by 8 square in the point's colour, or into nothing when the
  point's alpha is nought; a tessellation control and evaluation pair spreads a four point patch
  over its corners' rectangle at level 4 and at level 7, the level taken from the patch and handed
  back in the alpha of every pixel, which says whether the control stage's levels reached the
  evaluation. Every edge is a pixel edge, so every pixel is exact. A host without geometry or
  tessellation shaders has to emulate them to pass (Ryujinx runs them as compute there); an emulator
  that assumes them leaves it to the driver, which gets a pipeline it cannot run
  (`vulkan_device.cpp` lists `geometryShader` as mandatory and carries on without it). The two
  geometry shader cases are in the may-crash table: the shader compiles to a PRET, which ends Eden,
  and the fork before ECFE-2026-1163, inside the pipeline cache. Two more run twins of the shaders
  that need neither: `square_branch.geom` emits its square inside the test instead of after an early
  return, which uam 1.1.0 compiles with SSY and SYNC rather than PRET and RET, over the layout with
  two points hidden; `quad_color.tese` takes the alpha from the first corner's colour, which
  `quad.tesc` passes on, instead of reading the level back, at level 4. A translator that lacks PRET
  fails the first two and passes the geometry twin; one that cannot read the levels in the
  evaluation stage fails the patches at 4 and 7 and passes the tessellation twin; one without the
  stages fails all six that draw. The twins are not in the may-crash table.

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
  RGBA_ETC2), one 4x4 block each written by the processor and fetched texel by texel: an individual,
  a flipped differential, a T mode, two H mode blocks (the first base the larger and the smaller,
  which sets the distance index's last bit), a planar block, three punch-through blocks (index 2
  transparent in differential and T mode, and planar always opaque), and an RGBA block whose 8-bit
  alpha reaches both clamps. Every block is built from its fields by `source/etc2ref.c`, whose
  decoder says what each texel must be; the reference suite holds it to Mesa's. ETC2 decodes to
  exact bytes, so the tier is exact. A host without ETC2 textures has to decode them itself; in an
  emulator a failure points at whether it does (`renderer_vulkan/maxwell_to_vk.cpp`,
  `vk_texture_cache.cpp`, `texture_cache/util.cpp`, `ConvertImage`): an upload of the raw blocks
  shows bytes of block data. A failing case whose texels 0 and 1 are the block's own bytes (an RGB
  format's alpha may read 255, from its swizzle) says so: the block was uploaded as it is and never
  decoded, the way the order and coherence suites name their signatures. The reference suite checks
  that every block's bytes read that way and that no block's colours do.

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
  masquerade layer (see [Making the driver look older](../OlderDriver.md)).
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
- **query**, twenty five cases and three notes. The samples passed counter over quads of whole
  pixels, exact: 9600 for a 32 by 300 quad, 11200 with a second quad of 1600, nought after a reset.
  The target is 320 rows tall, past the 288 an emulator that scales render targets starts at, so a
  run with scaling at 2x says whether a game is handed the console's count or four times it (Ryujinx
  divides by the scale squared; a failing case names the factor). Then the render enable, which the
  library never writes, as raw methods around one draw: FALSE and TRUE, and IF_EQUAL and
  IF_NOT_EQUAL over two 64-bit values 16 bytes apart, written by the processor (equal with
  timestamps that differ, apart in the low word, apart only in the high word) and reported by the
  counter (the same count twice, a count and nought), in the same list after a wait for idle and in
  a list that finished before, the same-list pair also over a full target clear, and a full target
  clear under IF_EQUAL over the processor's values, apart and equal, which tells a clear that
  ignores the render enable from reports an emulator loses track of. The processor's values are
  plain memory to an emulator and the reports are queries it may be resolving at the wait or may
  have left from the earlier submission, so the sets can disagree; in an emulator a failure points
  at `query_cache/query_cache.h`, `AccelerateHostConditionalRendering`, and at `vk_query_cache.cpp`,
  which renders unconditionally on some drivers and accuracies. CONDITIONAL, which reads one
  address, gets three notes rather than cases (the value one, the value two to the 32, a report of
  9600), since which of its bytes decide is not settled: a console run is what says.
- **dma**, five cases. The copy engine copies a 64 KiB buffer 32 times in one list, first from
  words the processor wrote and then from words a dispatch wrote just before; every copy is
  judged word by word. The difference between the two times is what copying data the graphics
  unit wrote costs: an emulator that stops the processor until the graphics unit is idle on each
  such copy (Eden's buffer cache, `DMACopy`, with a fence setting that asks for it) pays it 32
  times in the second case. A single judged copy goes first and takes the first use.
- **blit2d**, five cases. The 2D engine, by raw methods, copies a block linear source at half and
  at two thirds size, from each sample origin, into a 21 by 9 window of a block linear destination
  whose rows start and end inside a gob's 16 byte runs. Every pixel is judged twice, in the raw
  bytes the processor reads and in a copy to buffer, against the image each answer to two open
  questions gives. See [The blit2d suite](Blit2d.md).

## What a console run settles

Questions only a console answers, asked about the cases both emulators fail in this family; each
case's expectation already follows the specification or the library, and the console says
whether the machine does.

- **Q1, etc2.** Do the ten blocks give exactly the specification's texels? Expected yes.
- **Q2, zeta.** Does a Z24S8 word 0x00ff005a written by the processor sample as 1 (depth in the
  top 24 bits, as the case expects) or 254 (the low 24)? And 0x80808080 as 128?
- **Q3, query.** In one list, with a full barrier between the reports and the render enable, does
  IF_EQUAL over 9600 and nought skip (the reports visible) or draw (the memset's nought read)? And
  does a clear obey the render enable?
- **Q4, geometry.** Does the patch cover exactly 8..56 by 8..56, with alpha 4 and 7?
- **Q5, geometry.** Does the geometry shader make the four squares, and only two in the second case?
- **Q6, query notes.** What CONDITIONAL does over the value one, over two to the 32 and over a
  report of 9600 in the same list, which says which of its bytes decide.

The blit2d suite asks two questions of its own, which texel each origin samples when the copy
scales and how a fraction in the scale is stepped; they are on [its page](Blit2d.md), as its Q1
and Q2.

## Where reden falls short

Read in reden at 75c12299a6; each gap is a case here that fails, and the new cases tell them apart.

- **ETC2 colour formats are not decoded.** Without `textureCompressionETC2`
  (`vulkan_common/vulkan_device.h:397-399`) the six ETC2 colour formats go to A8B8G8R8
  (`renderer_vulkan/maxwell_to_vk.cpp:298`) with no decoder: `vk_texture_cache.cpp:1891` marks
  only ASTC, BCn and EAC as converted, and `ConvertImage` (`texture_cache/util.cpp:926`) has a
  branch for EAC (986) and none for ETC2. The etc2 cases fail with the block's own bytes, which
  their detail now names.
- **Z24S8 is uploaded as it is.** reden's own layout agrees with the zeta cases
  (`texture_cache/format_lookup_table.cpp:154-157`, `vk_texture_cache.cpp:313-315`, R stencil and
  G depth), but `maxwell_to_vk.cpp:241` maps the format to D24_UNORM_S8_UINT, a host without it
  falls back to D32_SFLOAT_S8_UINT (`vulkan_device.cpp:51-55`, chosen at 122-123), and the upload
  (`vk_texture_cache.cpp:2048`, `TransformBufferImageCopies` at 474) copies the words unconverted.
- **The render enable over reports in the same list is ignored.** `maxwell_3d.cpp:515-518` turns
  execution on whenever the rasterizer accelerates; with both operands still queries in the cache,
  `vk_query_cache.cpp:1552-1560` ends host conditional rendering and draws and clears
  unconditionally. Values from the processor, and reports from a list that finished, take the
  software path and are right; the clear over the processor's values tells the two apart.
- **PRET in the flow analysis.** `shader_recompiler/frontend/maxwell/control_flow.cpp:329-330`
  throws on PRET, and the pipeline is dropped; `square.geom`'s early return compiles to one, and
  `square_branch.geom`, which has none, separates this from the geometry stage.
- **Reading the tessellation levels in the evaluation stage.**
  `shader_recompiler/ir_opt/collect_shader_info_pass.cpp:69-71` throws on a non-generic patch, and
  `quad.tese` reads `gl_TessLevelOuter[0]` (`TessellationLodLeft`, `frontend/ir/patch.h:11`);
  `quad_color.tese`, which does not, separates this from the tessellation stages.
- **The 2D engine's scaled copy stays in the texture cache.** `engines/fermi_2d.cpp:84-88` and
  `:112-115` send any copy with a scale to `AccelerateSurfaceCopy`, which draws it into the cache's
  image of the destination, and nothing writes that image back to memory before the processor
  reads it (`texture_cache/texture_cache.h:712-713`, `engines/maxwell_3d.cpp:588-592`): the
  blit2d cases fail on the raw read and pass on the copy to buffer. See [The blit2d
  suite](Blit2d.md).
