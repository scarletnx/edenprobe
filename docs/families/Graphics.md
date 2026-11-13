# The graphics family

## Index

* [clear](#clear)
* [format](#format)
* [raster](#raster)
* [intmath](#intmath)
* [blend](#blend)
* [depth](#depth)
* [fixed](#fixed)
* [stencil](#stencil)
* [texture](#texture)
* [lod](#lod)
* [coverage](#coverage)
* [msaa](#msaa)
* [sampling](#sampling)
* [coherence](#coherence)
* [fmath](#fmath)
* [order](#order)
* [queue](#queue)
* [storage](#storage)
* [index](#index-1)
* [topology](#topology)
* [vertex](#vertex)
* [instance](#instance)
* [geometry](#geometry)
* [xfb](#xfb)
* [bindless](#bindless)
* [astc](#astc)
* [astcimage](#astcimage)
* [eac](#eac)
* [etc2](#etc2)
* [bcn](#bcn)
* [zeta](#zeta)
* [swizzle](#swizzle)
* [texel](#texel)
* [stream](#stream)
* [fill](#fill)
* [ring](#ring)
* [occlusion](#occlusion)
* [drawtex](#drawtex)
* [query](#query)
* [dma](#dma)
* [blit2d](#blit2d)
* [copy](#copy)
* [tex8k](#tex8k)
* [What a console run settles](#what-a-console-run-settles)
* [Where reden falls short](#where-reden-falls-short)

The forty two drawing suites, clear to copy, in the order they run, and tex8k after them.

## clear

Seven cases, on the graphics unit: whether the device came up (no tier, it compares
nothing), and four that clear a 64 by 64 RGBA8 target and copy all 4096 pixels back. Three cover
the whole target in three colours in a row, so a stale readback cannot pass by accident. The
fourth clears a rectangle of another colour inside it, which makes the answer depend on where a
pixel is: only this one catches a transposed, flipped or shuffled readback. All four are Exact:
the format is integer and every clear value a constant, so each channel has one right byte.

The last two ask what cuts a clear to the scissor, which the bounded rectangle leans on: the
library sets CLEAR_FLAGS' scissor bit (bit 8, deko3d's ClearBufferFlags) once at start-up. With
the bit cleared by a raw write, and then with scissor 0's own enable off, the same clear over the
rectangle's scissor covers the whole target, as ScarletNX's clear flags test holds its 3D engine
to; each puts the register back after its clear. In an emulator a failure points at a clear cut
to the scissor whatever the flags say, or at the enable left unread.

## format

Sixty four cases: every colour format the graphics library can render to, sixty of
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

## raster

Ten cases. A triangle or a quad in one flat colour through a vertex and a fragment
shader compiled into the binary, every pixel compared to the coverage rule: exact for pixels at
least half a pixel from every edge, position for the band an edge runs through, and exact
everywhere for the two cases that put 32 pixel centres exactly on a shared edge. Those are decided
by the fill rule, not by sub pixel precision, so an implementation that draws a shared edge twice
or not at all shows it there.

## intmath

Forty six cases. Fragment shaders doing integer arithmetic on their own coordinate
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

## blend

Thirteen cases. The fixed function blend unit: adding, subtracting both ways round, the
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

## depth

Thirty one cases. One quad at a known depth and a second one challenging it: each of
the eight compare operations at each of three depths, twenty four cases. Three depths give eight
patterns of pass and fail, one per operation, so leaving any out would let two operations share
their answers and an emulator that wired them together go unnoticed; one more case works the
twenty four answers out on the processor and checks that the eight really differ. Plus the depth
buffer read back as a float, bit for bit. Exact: the depths are 0.25, 0.5 and 0.75, which survive
the transform into depth space untouched, so a fragment at the buffer's own depth really is an
equality. Three cases are about the write rather than the test, the half a colour cannot show.

## fixed

Thirty one cases and three notes. The fixed function state a game's driver sets and Vulkan lacks
or keeps elsewhere, so an emulator carries it over itself. Every case draws flat colours over whole
pixels of a 64 by 64 RGBA8 target and reads all 4096 back: exact. The rules past a comparison are
`source/fixedref.c`'s, pinned by the reference suite. In the order they run:

- **The point size.** Four points in one list, the size set between draws (`dkCmdBufSetPointSize`,
  the 3D class's PointSpriteSize, with the vertex program's own size off): 1 and 3 on a pixel
  centre, 4 and 8 on a pixel corner, so no pixel centre sits on a square's edge. A case each,
  judged over its quarter of the target: the size by size pixels around the centre and nothing
  else. A host that takes a point's size only from the shader has to write the register's into it.
- **The alpha test.** Three columns of alpha 0.5 under one compare operation, the reference set
  between draws to 0.25, 0.5 and 0.75: eight cases, one per operation, the three references giving
  each operation a pattern of its own (the reference suite checks the eight differ). The fragment's
  alpha is on the left, as OpenGL's compatibility profile and NVN have it. Red, green and blue are
  written and alpha masked off, so how a half rounds into eight bits decides nothing. Vulkan has no
  alpha test: an emulator compiles it into the fragment shader.
- **The depth bounds.** A ZF32 buffer cleared to 0.5 and a quad over it at 0.25, the depth test on
  with ALWAYS and no write: the bounds test reads the value in the buffer, never the fragment's,
  with both ends included (EXT_depth_bounds_test). Bounds 0.125 to 0.375 reject though the fragment
  is inside them; 0.375 to 0.625 keep; 0.5 to 0.75 and 0.25 to 0.5 keep at either end; 0.625 to
  0.875 reject; and the test switched off keeps. Six cases.
- **The rasteriser off.** A quad over the target with RasterizerEnable off writes nothing.
- **Faces and the viewport.** Two rectangles, one counter clockwise and one clockwise in clip
  space, back faces culled: the counter clockwise one is front (deko3d's upper left device flips
  the viewport's y scale and not the winding, and keeps OpenGL's counter clockwise front), and with
  the front face clockwise the other one is. Then the viewport's y scale written positive by a raw
  method (`RawViewportScaleY`), the swizzle NegativeY (`dkCmdBufSetViewportSwizzles`), and both:
  each of the first two mirrors the picture and with it the winding, so the counter clockwise
  rectangle is culled and the clockwise one drawn where the mirror puts it; together they cancel.
  The rasteriser decides the face from the triangle as it lands in the target, which is Vulkan's
  rule for a negative viewport height too; an emulator that tracks the face through the scale's
  sign and the swizzle apart gets one of them inside out. Last, the swizzle that swaps x and y,
  with no culling, transposes the picture. Six cases.
- **A device with the lower left origin** (`DkDeviceFlags_OriginLowerLeft`): the viewport's y scale
  stays positive, so clip space y of one is the target's last row, and the window origin's FlipY
  bit flips the winding back, so the counter clockwise rectangle is still front. Two cases.
- **A device with depth from minus one to one** (`DkDeviceFlags_DepthMinusOneToOne`): four columns
  at clip z -1, -0.5, 0 and 0.5 over a buffer of 1, depth test ALWAYS with the write on, store 0,
  0.25, 0.5 and 0.75, bit for bit. The library sets the 3D class's depth mode, which moves the clip
  volume to minus one, and a z scale and offset of a half: an emulator that only rescales z, and
  clips where Vulkan clips, loses the first two columns.

The notes, which no machine is held to, since a console run is what says which answer is right:

- **Which way up a point's coordinate runs.** deko3d sets PointCoordReplace's origin to lower
  left. A point of size 8 drawn with a shader that writes `gl_PointCoord` reads 16 at its first row
  when the origin is at the top of the target's memory and 239 when it is at the bottom; the note
  gives both corners.
- **A depth outside the range, with the clamp off and on.** A quad at z -0.5 and one at 1.5 on the
  default device, over a buffer of 0.75: drawn at depth 0 or 1 is clamped, the clear and 0.75 is
  clipped. deko3d's ViewVolumeClipControl words for the clamp off and on are not documented, so the
  library's word alone does not settle it.

In an emulator a failure points at the 3D engine's registers the state comes from and the way it
reaches the host: the point size (`maxwell_3d.h`, `point_size`; `vk_graphics_pipeline.cpp`), the
alpha test (`shader_recompiler`'s alpha test emulation, `vk_pipeline_cache.cpp`), the depth bounds
(`vk_rasterizer.cpp`, `UpdateDepthBounds`), the rasteriser enable, the viewport transform's scale,
swizzle and the window origin (`vk_rasterizer.cpp`, `GetViewportState`; `FrontFace` in
`maxwell_to_vk.cpp`), and the depth mode (`depth_mode`, VK_EXT_depth_clip_control or the shader's
z fix-up).
## stencil

Fifty six cases, on the two depth formats that carry stencil, Z24S8 and ZF32_X24S8 (a 32-bit float
depth with stencil). The 32 by 32 target is three bands of rows, each cleared to a stencil and a
depth of its own, so one draw meets three situations at once. A stored stencil is never read as
bytes, since how a Z24S8 word splits is the zeta suite's open question: a band is checked by a
draw with the test at EQUAL against the value the band should hold, which marks its pixels, so
the compare cases come first and everything after leans on them. Per format: the eight compares
against 0x80 over bands holding 0x40, 0x80 and 0xc0, below, equal and above, which gives each
compare its own pattern (the reference suite checks the eight differ); eight op cases, GREATER
against 0x80 over 0xa5 (the test fails), 0x5a nearer than the draw (the depth test fails) and 0x5a
(both pass), each op once as the fail op, once as the depth fail op and once as the pass op, at
values where every op leaves its own byte; INCR against INCR_WRAP at 0xff and DECR against
DECR_WRAP at nought; the write mask (0x0f inverted under 0xf0 is 0xff) and the compare mask (0x3a
equals 0x5a under 0x1f, and not without it); and the two faces, two quads wound opposite ways
with front and back references 0x11 and 0x22, where the case asks only that each takes one face's
whole state, since which winding is front follows the window origin the library sets up, and a
note says which one was. Exact: every expected value is a byte `source/stencilref.c` works out by
the specification's rules.

The last six per format are about where the tests run. A fragment shader that writes its own depth
(`depth_out.frag.glsl`, from the vertex colour's alpha) is tested at that depth and not the
vertices': a half drawn at 0.25 that writes 0.75 is rejected under LESS against 0.5, and its
stencil takes the depth fail op. And a shader that writes depth runs for every fragment, the ones
the depth or the stencil test then rejects included, while one with early fragment tests runs only
for those that pass (ARB_shader_image_load_store, "Early Fragment Tests"): `count_late.frag.glsl`
and `count_early.frag.glsl` add one to a storage buffer word per invocation over 1024 pixels with
half of them rejected, so the count is 1024 or 512. This is the behaviour an emulator has to keep
when it moves the depth and stencil tests into a shader of its own, as one that emulates ordered
blending does.

## texture

Twelve cases. A 4 by 4 texture over the whole target, so sixteen by sixteen pixels
land on each texel. Nearest filtering with repeating, clamping to the edge and mirroring, each on
a quad three copies wide, and linear filtering laid out so the only weight the filter can use is
one half and every pair of texels averages to a whole byte. Exact, earned by the layout: no pixel
centre comes within a thirty second of a texel boundary (the reference suite measures it), and no
two texels the linear case blends differ by more than 48, so the filter's weight would have to be
wrong by one part in 96 before any byte moved.

## lod

Fourteen cases. The level of detail a lookup picks and the texel it then reads, on an R32_Float
texture of 16 by 8 with its five levels whose every texel is a whole number naming its level and
place (`source/lodref.c`), one lookup per pixel of a 16 by 1 target at (13/32, 13/32), a
coordinate at least 3/16 of a texel from any boundary on any level. textureGrad with gradients of
one, two, four and eight texels along x, along y (where a gradient scaled by the width instead of
the height lands a level up, which the texture's shape is for) and in the second derivative, and
with two derivatives of different lengths, where the longer decides; textureGradOffset and
textureLodOffset, whose offset counts in the texels of the level picked, not of level nought;
textureLod at levels nought to four; and the sampler's minimum (2), maximum (1) and bias (1).
Every lambda is a whole number however a machine measures a gradient's length, half a level
from where nearest mip filtering changes its mind, so these are exact. The last case blends:
a minimum of 1.25 with linear mip filtering gives a quarter of level 2's texel and three quarters
of level 1's, 1264.5, a float exactly; the units in the last place tier at a budget of nought.
The clamp a lookup carries itself (textureClampARB, which a translator meets as the instruction's
own minimum) is out of reach: the shader compiler refuses ARB_sparse_texture_clamp.

## coverage

Twenty three cases. Multisample coverage, read through coverage to color
(NV_fragment_coverage_to_color): the fragment's coverage, bit i for sample i, takes the place of
the red component the shader wrote to an integer target, after the sample mask, alpha to coverage
and the depth and stencil tests. With more raster samples than colour samples
(NV_framebuffer_mixed_samples), a one sample R32_Uint target receives every raster sample's bit,
so the word a pixel holds says which of its samples a draw covered, with no multisample image to
read. The shader writes 0xdead0000 and the clear is 0xbad, so a pixel holding the first drew
without its coverage and one holding the second drew nothing.

Every draw that cuts pixels is a rectangle from the left edge to a pixel's centre or a quarter
into it, and the sample positions are whole sixteenths, so which samples it covers is exact
(`source/coverageref.c`, whose reference cases keep every split a sixteenth or more from its
edge). Eight cases: four raster samples write 0xf, and 0x5 at a pixel the standard positions
split at its centre; two write 0x3 and 0x2; eight 0xff and 0x3a; the sample mask 0x6 is in the
mask written; positions the list sets (samples 0 and 1 two sixteenths in from the left, 2 and 3
from the right) split the same pixel as 0x3; every sample at the centre, the library's default,
leaves a pixel cut a quarter in unwritten, where the standard positions would give 0x4; and the
2 by 2 grid four samples' positions can vary over, with the mirrored pattern in the odd columns
(0xc where the even give 0x3) and the same in both rows, so which row the grid starts on decides
nothing. An emulator that rasterises at the host's own positions and ignores the list's passes
the first and fails the positions it sets.

Then alpha to coverage, through a second target: at alpha nought no sample is covered, at one
every sample, and the sample mask still applies. Those are all the specification fixes; the masks
in between are the machine's, so the suite leaves fourteen notes, alpha one eighth to seven eighths
with the dither off and on, each with the masks the 256 pixels held. Alpha to one, which the
library has no call for and goes in as the raw MULTISAMPLE_CONTROL word, must leave the coverage
the shader's alpha of one half gave, pixel by pixel, and write an alpha of 0xff; a machine where one
half covers no sample or every sample cannot show the order, and the case says so.

Coverage modulation, four raster samples over two RGBA8 targets: four bands of rows under the
sample masks 0x1, 0x3, 0x7 and 0xf scale the colour by a quarter to all of it, every channel of
the two colours a whole byte at each share, with room under a factor kept as an eight bit fraction
(the reference suite checks it). RGBA scales every channel, RGB leaves alpha, ALPHA leaves the
colour, the second target is scaled too and keeps its own write mask, the table (entries 3, 7, 11
and 15 set to one, a quarter, a half and three quarters) takes the share's place, and a pixel the
standard positions split at its centre gets half the colour.

Then the depth samples outnumbering the colour ones: a four sample depth image under the one sample
target, samples 0 and 2 drawn nearer first, so the coverage to color draw behind them must leave
0xa, the raster coverage after the depth test, in every pixel.

The last two need a four sample image: each sample of an R32_Uint one written s + 1 by a draw under
the sample mask 1 << s and fetched back sample by sample, its left four columns into a 16 by 16
target, which the last case leans on; then coverage to color on that image after a depth test, samples 0 and 2 drawn nearer
first, so samples 1 and 3 pass and each must hold 0xa, the coverage left after the test, where 0xf
would be the coverage before it.

## msaa

Seven cases. Multisampled targets as memory holds them, where a game finds them when it copies
one, samples one it rendered or loads one it wrote: a 4x4 target of four samples is an 8x8 image,
sample i of pixel (x, y) at (2x + i % 2, 2y + i / 2), the 3D class's 2x2 layout (deko3d's
DkMsMode_4x), which ScarletNX's multisample tests hold its renderer to in guest memory. Each
target is cleared to a blue of 128 and drawn with a green of 128 over the triangle (0, 0), (4, 0),
(0, 4) at the standard positions, (6, 2), (14, 6), (2, 10) and (10, 14) in sixteenths: a sample is
green where 16 (x + y) and its two sixteenths make less than 64, which no sample ties, so the
pixels where x + y is 3 have samples 0 and 2 green and 1 and 3 blue. The cases: the samples read
from processor-visible memory after the level two cache is written back; a resolve into a 4x4
image of one sample, every pixel the average of its four (128 makes it exact: 0, 32, 64, 96 or
128); a 4x ZF32 target cleared to 0.75 holding the float at all 64 samples; each sample of a 4x
RGBA8_Uint texture the processor wrote fetched onto its own pixel (`msfetch.frag.glsl`); and a
conservative triangle, every sample of each pixel it crosses green and none of a pixel it misses,
the pixels its edge touches only at a corner left unjudged. Exact. In an emulator a failure points
at the layout it writes a multisampled image back in and reads one from (the resolve's average,
the sample order, a target never written back), or at conservative rasterisation it does not pass
on.

## sampling

Twenty one cases. What the sampler hands a shader, onto a 4x4 target of 32-bit floats so it
arrives unrounded. Thirteen formats, every texel of a 4x4 texture one word the processor wrote:
R8, RG8 and the defaults of the channels each lacks (nought, and one for alpha: deko3d's format
table); RGBX8, whose alpha is one whatever memory holds; BGRA8's blue in its first byte; R8 snorm
0x80 clamped to -1; RGB565 and BGR565, red or blue in the low five bits; RGB5A1's alpha in the top
bit; RGBA4's red in the low nibble; RGB10A2; R16F, RG11B10F and E5BGR9's shared exponent, each a
value the format holds exactly. Then the sampler's reduction (its word 1, bits 10 and 11): a
linear lookup of a 2x1 R32F texture of nought and one at the edge the two share, which averages
them, the minimum reduction the lesser and the maximum the greater; a reduction of 3, which names
no mode, goes into a note (ScarletNX's sampler test holds it to the average). Then a rectangle
view (the descriptor's normalised coordinates off, as NVN writes its RECTANGLE target) reads
texel (3, 0) at (3.5, 0.5), which a normalised view reads at (0.875, 0.125), and a normalised
view repeats (3.5, 0.5) to the middle, texel (2, 2). Exact, as each value is one every
conversion lands on. In an emulator a failure points at its format table (`texture_cache/format_lookup_table.cpp`,
`renderer_vulkan/maxwell_to_vk.cpp`), the sampler's reduction (`textures/texture.h`,
`vk_texture_cache.cpp`, `Sampler`) or a descriptor's coordinate mode left unread.

## coherence

Twenty five cases, six of them the none tier (the device, the shaders, the images
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

Last, on a device of its own, code and vertices rewritten in place, each target half judged whole:
a fragment program the processor rewrites at its address between two lists, and one the copy
engine rewrites in the middle of a list behind a full barrier and the shader cache's invalidation,
both drawing as the new program after (`const_red.frag.glsl`'s code overwritten by
`const_green.frag.glsl`'s, which share their control section, so the program is the other in
every byte the draw reads; the case says so if the compiler ever makes them differ); and
vertices the copy engine writes between two draws of one list, which the second draw fetches.
ScarletNX's shader invalidation and pass break tests hold its renderer to these. In an emulator a
failure points at a shader cache keyed by address that misses the rewrite, or at a buffer cache
that serves the second draw what the first one read.

## fmath

Twenty nine cases. Addition, multiplication, a fused multiply and add, division, the
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

The last three ask the saturate a game's compiled shaders clamp to nought and one with: uam folds
GLSL's clamp into the hardware's .SAT (`saturate.frag.glsl` compiles to FADD.SAT), which turns a
NaN of either sign, quiet or signalling, into +0, where GLSL's clamp of a NaN promises nothing;
and clamps the rest, a nought keeping either sign. Seven NaNs in one case, twelve other values in
the next, each a column of the target. Exact. ScarletNX's saturate test holds its translation to
the same; a translator that makes .SAT a plain clamp gives the NaN back, or whatever its host's
clamp gives.

## order

Twenty three cases, on the graphics unit with a compute queue. A dispatch writes a buffer,
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

Then four a launch answers on its own, which ScarletNX's runtime tests hold it to: an indirect
dispatch whose grid the processor wrote as nought groups runs none, every slot keeping its
nought; a draw with no target bound at all, colour or depth, still runs its fragments, whose only
effect is the words they store (`store_ssbo.frag.glsl`, sixteen words and none past them); and a
block of 64 invocations uses all 48 KiB of shared memory a block of the console's GPU is given
(`shared_reach.comp.glsl`), each invocation folding in order the 192 words it wrote, through the
last; then the same past a barrier (`shared_swap.comp.glsl`), each folding the words the one at
the other end wrote, the barrier as uam compiles one (BAR.SYNC with RZ for its barrier and its
count, which neither ScarletNX nor Eden takes: both leave the dispatch out). In an emulator these
point at a dispatch of nothing turned into one of something, a draw skipped for having no target,
shared memory a host gives a block less of, and a barrier form its translator refuses.

## queue

Five cases. A list that waits on a word in memory until another queue, or the processor, writes
it: the semaphore acquire a game's driver orders its queues with (deko3d's `dkCmdBufWaitVariable`,
the GPFIFO's ACQUIRE and ACQ_GEQ), which an emulator runs on threads of its own or one queue after
the other. Each list waits, then copies a buffer by the copy engine and signals a fence; the
processor looks 50 ms after the submit whether the copy was made, releases the word, and waits for
the fence. A copy made while the word was short is a wait not kept; no fence after the release is a
wait never woken.

- **The processor's write.** A wait for 1 or more holds at 0 and goes on at 1; a wait for exactly 2
  holds at 1 and goes on at 2.
- **Another queue's signal.** The first queue's list waits for 1 and then copies a middle buffer
  out; the second queue copies a pattern into that buffer, waits for the copy (a full barrier) and
  sets the word from its 3D engine (`dkCmdBufSignalVariable`, after the pipeline): the first
  list's copy must hold the pattern, not what the middle held before.
- **A wait already met.** The word at 5, a wait for 3 or more: the list runs at once.
- **The device opens** with a second queue, a command list of its own, and the word in a block
  uncached on both sides, as the library wants a variable.

No case can hang the run: the processor waits for a fence a second at most, and when the release
it expected never came writes the word itself and waits again. A machine whose list never comes
back even then fails the case, skips the rest as `depends` and leaves the device open, since
closing it would wait for that list, and a note says so. In an emulator a failure points at the
puller's semaphore acquire (`engines/puller.cpp`, `ProcessSemaphoreAcquire`) and at how its
queues run: one after the other, a wait cannot be released by the queue behind it.
## storage

Eighteen cases. Compute programs that store into and load from images, as a game's programs make
the levels of a texture, fill a volume or keep masks in an array. A store converts by the format
of the image its descriptor names, the shader declaring none: ten formats, each value one that
lands on a single encoding under any legal rounding (Vulkan's texel output conversion: unsigned
normalised clamped at both ends, signed normalised -1 to 0x81 and below it too, floats every
narrower float holds exactly, integers in range): RGBA8, RGBA8 snorm, RGB10A2, RG11B10F,
RGBA16F, R32F, RGBA32F, R32, RGBA8 and RGBA16 integers. A store to level 1 of three, to layer 1
of a three layer array and to slice 1 of a volume writes that texel and no other. Every image is
filled first through the copy engine, every level, layer and slice with bytes of its own, read
back the same way after the dispatch and judged byte for byte. Then a formatted load of RGBA8
00 80 ff 40 gives 0, 128/255, 1 and 64/255 rounded to the nearest float (0x3f008081 and
0x3e808081), and a load and a store keep a texel's bits: an R32F array's layer 1 at level 1, and an
RGBA8 sint volume's slice 1. Exact. ScarletNX's storage tests hold its runtime to these against
guest memory. In an emulator a failure points at the storage image's view (a level, a layer or a
slice taken as another), a format it stores with its own conversion, or a store it never writes
back.

## index

Eight cases. Indexed draws whose index limit register holds the last byte of the
indices, the way a driver that bounds its draws exactly programs it (the register is a limit,
the last valid byte; nouveau writes the address plus the size minus one). The library never
writes that register after start-up, so each case writes it with raw methods before the draw
and restores the library's value after. The draw's last index closes the second of two
triangles over the target, so reading the limit as one past the end loses half the target.
8, 16 and 32 bit indices, a long draw, and one that starts four indices in; one control with
the library's open limit. In an emulator a failure points at the size the index buffer is bound
with (`buffer_cache/buffer_cache.h`, `UpdateIndexBuffer`).

## topology

Twenty eight cases and two notes. The primitive types the 3D class draws and Vulkan does not
(quads, quad strips, polygons, line loops), an index list that restarts at any index, quads and
polygons drawn as lines, and which vertex a flat colour comes from in each. An emulator remakes
each of them out of triangle lists, strips and the all-ones restart, often with an index buffer
of its own. Every case draws flat colours on a 64 by 64 RGBA8 target, deko3d's default
rasteriser with no culling and the last vertex provoking unless it says otherwise, and reads all
4096 pixels back: exact. The shapes have their corners on whole pixel corners and their diagonals
at slopes that keep every pixel centre a twentieth of a pixel or more from an edge where the
colour changes (two rows down for one column across, two across for one down, three for two), so
no case leans on a fill rule. The rules are `source/topologyref.c`'s, from OpenGL's section 10.1
and table 10.2, pinned by the reference suite. In the order they run:

- **Quads.** Two rectangles of four vertices each in one draw, wound around their edge: each
  covered, nothing else. Then each vertex its own colour: a quad takes its fourth vertex's, so a
  split into two triangles that keeps the last vertex of each gives half the quad its third
  vertex's colour, and one that keeps the first gives it the first's. Then from 8, 16 and 32-bit
  indices, the second quad's first (three cases); from 16-bit indices that start three in, with
  a vertex offset of two over two vertices of padding; and six vertices drawn as quads, which draw
  one: the buffer holds the two that would finish a second, so an emulator that rounds the count
  up draws it.
- **A quad strip** of eight vertices, its pairs top then bottom: three quads covered, then each
  in its last vertex's colour (vertices 3, 5 and 7).
- **A polygon** of six corners around a rectangle, two of them on its sides: the rectangle and
  nothing else. Taken as a triangle strip or a list it leaves part of the rectangle out.
- **A triangle fan** of four triangles around a hub: the rectangle covered, then each triangle
  in its last vertex's colour; and a strip of four triangles, each in its last vertex's colour.
  The last vertex provokes on the console's default and Vulkan's is the first: an emulator needs
  VK_EXT_provoking_vertex's last vertex mode, or to rotate each triangle. Then the first vertex
  convention, set by the list: a strip's triangles take their first vertex's colour, a fan's the
  first of their rim vertices', not the hub's, as both OpenGL and Vulkan have it.
- **Restart.** Two strips of two triangles with the restart index between them, and a ninth
  vertex far to the right that every bridge a missed restart makes reaches for: restart index 8
  with 16, 8 and 32-bit indices, which Vulkan cannot restart at (it restarts at the all-ones value
  of the index's width alone); 0xff with 8-bit indices, which an emulator that widens 8-bit
  indices to 16 has to turn into 0xffff; and with restart off, index 8 a vertex like any other,
  which draws the bridges and shows where they land. Then two fans, each with a hub of its own
  after the restart, and two quads from 16-bit indices with the restart index between them.
- **Lines.** A line loop of a rectangle's four corners, on pixel centres, draws its fourth side
  from the last vertex back to the first; a line strip of the same four leaves that side clear. A
  quad of the same corners drawn as lines (the polygon mode Line, both faces) shows its four sides
  and no diagonal, as does a polygon of them: an emulator that draws them as triangles in line mode
  shows the diagonal the two triangles share. Two triangles drawn as lines show that diagonal,
  the control. A line case judges a pixel drawn when its centre is within a quarter of a pixel of
  a line and a pixel or more from its ends, and clear when it is over a pixel from every line,
  which the diamond exit rule, Bresenham's and rectangular lines all agree on; the rest is not
  judged.

A case that tells vertices apart names, on a failure, whose colour the first wrong pixel holds
and whose it should.

The notes, which no machine is held to, since a console run is what says which answer is right:

- **A polygon's flat colour.** OpenGL takes a polygon's from its first vertex under either
  convention. The six corners each their own colour: the note counts the rectangle's pixels by
  the vertex whose colour they hold.
- **Quads under the first vertex convention.** OpenGL takes a quad's from its first vertex when
  QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION is true and from its fourth when it is false, and
  either is allowed. The note counts each quad's pixels the same way.

In an emulator a failure points at the topology's remaking (`renderer_vulkan/vk_rasterizer.cpp`
and `maxwell_to_vk.cpp`, `PrimitiveTopology`; the quad and restart index passes,
`vk_compute_pass.cpp`, `QuadArrayPass` and `QuadIndexedPass`, and `vk_buffer_cache.cpp` for 8-bit
indices), the restart index (`vk_graphics_pipeline.cpp`, `primitiveRestartEnable`), the
provoking vertex mode (`VK_EXT_provoking_vertex`), and the polygon mode (`fillModeNonSolid`).

## vertex

Twelve cases and a note. How the 3D class fetches a vertex attribute where Vulkan asks more of the
layout than the guest's driver did, so an emulator restrides, repacks or splits the stream. Each
case draws one point per vertex, at the pixel the vertex's own number names (vertex_value.vert.glsl:
the number plus eight per instance, row by row), carrying the attribute as fetched, on a 16 by 16
RGBA32F target: every component judged bit for bit, every pixel the draw does not reach held to the
clear. What an attribute gives from its bytes is `source/vertexref.c`'s, pinned by the reference
suite. The bytes around every vertex are 0xff, a NaN in any float a wrong fetch reads.

- **Strides and offsets.** RGBA32F vertices 69 bytes apart, so seven in eight of them start on no
  multiple of four, which Vulkan's attribute alignment does not allow; 48 bytes apart with the
  attribute at offset 8; and 32 bytes apart with the attribute at offset 40, past the stride, so
  each vertex reads from where the next one starts.
- **The components a format lacks.** RGB32F reads one in w, RG32F nought and one in z and w, R32F
  nought, nought and one: OpenGL's and Vulkan's rule for a vector the format leaves short.
- **Formats.** Four unsigned normalised bytes in BGRA order, red from the third byte (each byte
  nought or full, exact in any conversion); four 16-bit signed scaled values from -32768 to 32767,
  whole numbers as floats; four 8-bit unsigned scaled ones to 255; and 10_10_10_2 unsigned
  normalised, each field nought or full and the 2-bit one nought or three.
- **A per instance attribute** with divisor 3: one vertex drawn nine times, each instance reading
  the element its number divided by three names.

The note, which no machine is held to: what a fixed attribute gives a shader, the state deko3d
leaves every attribute it was not given (`IsFixed`, 1x32 float): OpenGL's default `0 0 0 1`, or a
constant of nought in every component, which the note says in bits.

In an emulator a failure points at the vertex buffer's binding (`buffer_cache/buffer_cache.h`,
`UpdateVertexBuffers`; a stride or an offset the host refuses, made into a copy), the attribute
format's host mapping and its BGRA and scaled forms (`maxwell_to_vk.cpp`, `VertexFormat`;
`vk_graphics_pipeline.cpp`), the components a short format leaves to the shader, and the
instance divisor (`VK_EXT_vertex_attribute_divisor`).

## instance

Five cases. The 3D class's instanced vertex array draws, VERTEX_ARRAY_INSTANCE_FIRST
and _SUBSEQUENT, sent as raw methods between the library's own commands. FIRST draws instance
nought when it arrives, with the state it arrived under: a uniform value pushed after it must
not reach it, and the library draw that follows must add no instances of its own. Each
SUBSEQUENT draws the same range as the next instance, and the vertex shader moves each instance
one region along, so the target says which instances ran. In an emulator a failure points at
`engines/draw_manager.cpp`, `DrawArrayInstanced`, `DrawDeferred`.

## geometry

Eighteen cases. The two optional stages every other suite leaves out. A geometry shader
turns each of four points into an 8 by 8 square in the point's colour, or into nothing when the
point's alpha is nought; a tessellation control and evaluation pair spreads a four point patch
over its corners' rectangle at level 4 and at level 7, the level taken from the patch and handed
back in the alpha of every pixel, which says whether the control stage's levels reached the
evaluation. Every edge is a pixel edge, so every pixel is exact. A host without geometry or
tessellation shaders has to emulate them to pass (Ryujinx runs them as compute there); an emulator
that assumes them leaves it to the driver, which gets a pipeline it cannot run
(`vulkan_device.cpp` lists `geometryShader` as mandatory and carries on without it). The two
geometry shader cases end Eden, and the fork before ECFE-2026-1163, inside the pipeline cache: the
shader compiles to a PRET; the launch after goes on past the case. Two more run twins of the shaders
that need neither: `square_branch.geom` emits its square inside the test instead of after an early
return, which uam 1.1.0 compiles with SSY and SYNC rather than PRET and RET, over the layout with
two points hidden; `quad_color.tese` takes the alpha from the first corner's colour, which
`quad.tesc` passes on, instead of reading the level back, at level 4. A translator that lacks PRET
fails the first two and passes the geometry twin; one that cannot read the levels in the
evaluation stage fails the patches at 4 and 7 and passes the tessellation twin; one without the
stages fails every case that draws. The twins end no program.

The next four draw the patch with no control stage, which the console allows and a Vulkan host
does not: the levels come from the list (level 5 here, so an emulator's own default shows), and the
evaluation stage reads the vertex stage's outputs straight. quad_color.tese has to cover the
rectangle and quad.tese has to say 5. Then `clip.vert.glsl` writes all eight clip distances, the
eighth minus one and the rest one, and `clip_last.tese` passes the eighth on as its own first, which
clips the whole patch, while `clip_first.tese` passes the first and the patch is drawn: a control
stage an emulator makes up has to pass every clip distance on.

The last four (after one that sets the image up) put a storage image in the stages before the
fragment one. `store.tese` writes 0xa0 to 0xa2 into texels 0 to 2 of a four texel R32_Uint image
while the fragment stage samples texel 3 of the same image, which nothing writes: the patch must
show 0x44, the texel's starting word, and the image must hold the three stores; a draw after a
barrier must fetch them; and `store.vert` writes 0xb0 plus the vertex index into texel index mod 4.
A host that binds images per stage for the fragment stage only, or keeps an image in a layout the
other use cannot see, fails these.

The last case binds one storage buffer to both stages before the fragment one:
`buffer_store.geom.glsl` stores words 0 to 15 while `buffer_read.vert.glsl`, in the same draw,
reads word 32 for the colour. Every pixel must be the colour and the buffer must hold the
sixteen stores and its other words as they were. ScarletNX's draw storage test holds its
renderer to the geometry stage's writes aliasing the vertex stage's reads; a host that gives the
geometry stage none of the draw's global memory loses the stores. Eden passes; ScarletNX skips the
draw, refusing a vertex program that reads a storage buffer (scarletnx/ScarletNX#1738).

## xfb

Four cases. Transform feedback, which a game's driver turns on through the 3D class's registers
and deko3d has no call for: one triangle drawn with the rasterizer off, its three positions
captured into buffer 0, four words a vertex in the order the stream layout names them (the
position's attributes, 0x1c to 0x1f), and the primitives written counter reading one. The
registers are the 3D class's, at the byte offsets reden's `maxwell_3d.h` asserts: buffer n's
enable, address, size and start offset at 0x380 + 0x20 n, its stream, varying count and stride at
0x700 + 0x10 n, the enable at 0x744, and the stream layout, a byte per varying, at 0x2800 + 0x80 n.
Every captured word is the float the vertex stage wrote, and the bytes past the 48 keep what they
held: exact. ScarletNX's transform feedback test captures through the same registers. In an
emulator a failure points at the capture (`renderer_vulkan/vk_rasterizer.cpp`, transform feedback
through VK_EXT_transform_feedback, and the shader translator's outputs decorated from the
layout) or at the counter.

## bindless

Six cases. A fragment shader picks one of 256 one-texel textures through a handle
read from a uniform array at a dynamic index (ARB_bindless_texture, which uam compiles to the
console's bindless fetch), so column i of the target must show texture i. Split at 128 so a
host array cut short at 128 descriptors says so on its own line. The same shader with a
constant index is how the setup was checked: the handles and the index reach the shader. In an
emulator a failure points at the dynamic descriptor arrays the shader translator builds
(`shader_recompiler/ir_opt/texture_pass.cpp`, `DESCRIPTOR_MAX_COUNT`,
`DynamicDescriptorCount`). The sixth reaches four sample textures the same way: two R32_Uint
images whose sample s holds 0x100 + s and 0x200 + s, each written by a draw under the sample mask
1 << s, through four handles alternating between them, each fetch naming its sample; a translator
that keeps the image and drops its being multisampled, or the sample, reads another word.

## astc

Eighteen cases. ASTC textures of one block each, the block written by the processor
into image memory and every texel fetched onto its own pixel: a void extent, 4-bit weights 0 to
15 and every 5-bit weight between endpoints 0 and 255 (built by hand, `tools/astc-blocks.py`),
trit and quint weights, trit and quint endpoints, two, three and four partitions, a dual plane,
weight infill on 8x8 and 12x12 blocks, an HDR partition before an LDR one, and an illegal block.
What each texel must be is `source/astcref.c`, an LDR profile decoder written from the Khronos
Data Format Specification's ASTC chapter, which the reference suite holds block by block to ARM's
reference decoder (astcenc 9fbec680) and which agreed with it on 458,752 random blocks over
every 2D footprint when it was written. A texel may be any of three bytes its 16-bit value gives:
the specification's default decode, divided by 65536 and cut to FP16, then rounded as the target
is written; divided by 65535 and rounded; or its top byte, where the texture unit decodes to eight
bits. Nothing else. In 2,080 of the 65,536 values the first is a byte neither other way gives
(0x8101 is 128 from FP16, 129 both other ways), so a texture unit that follows the specification
would fail where the case allowed only two. HDR endpoint modes are the error colour, opaque magenta, on their own partition's
texels in this profile, as astcenc has it. In an emulator a failure points at the ASTC decoder
it uses (`video_core/host_shaders/astc_decoder.comp`, `video_core/textures/astc.cpp`): the 4-bit
and 5-bit cases at the weight unquantization, the partition cases at the partition hash. Every
block's line says `"expected_from":"astcenc"`: no console has shown its texels yet. The HDR case
rests on that most, since it takes the console's texture unit for an LDR profile one; a unit that
decodes HDR endpoints would answer otherwise, and only a console run settles which.

## astcimage

Eleven cases. Whole ASTC textures, as a game's are. One 128x128 picture in four
regions (smooth gradients; hard edges where three colours meet inside a block; fine noise; alpha,
a ramp above and a cutout below), made by `tools/astc-images.py` and encoded by ARM's astcenc
(9fbec680, `-medium`) at 4x4, 6x6, 8x8 and 12x12, and in its sRGB mode at 4x4 and 8x8, each written
by the processor in the block linear layout and fetched onto a 128x128 target, a texel a pixel.
A linear texel may be any of three bytes: the specification's default decode, C / 65536 cut to
FP16 and rounded as the target is written, C / 65535 rounded, or C's top byte where the texture
unit decodes to eight bits. An sRGB one is the code `source/astcref.c` decodes in that mode (each
endpoint `(e << 8) | 0x80`, the result's top byte, as astcenc has it), which the texture unit
turns into linear: red, green and blue may be either byte beside that value times 255, alpha is
the code. The reference suite holds both decodes of the picture to astcenc's. A failure says how
many texels are wrong, how far the worst channel is, and the PSNR over the picture and over each
region: an emulator whose host has no ASTC and compresses the decoded texels again to save memory
(to BC7, say) fails here, and those numbers are what it cost. Then two 2048x2048 textures of the
same blocks repeated, 4x4 and 8x8, each made and fetched at every sixteenth texel, whose times are
the upload, decoding included; and the 4x4 one fetched by 256 draws, each texel once, whose time
is sampling it. In an emulator a failure points at its ASTC decoder, or at what it does with the
texels after (`video_core/host_shaders/astc_decoder.comp`, `astc_recompression`), and the times
at how it uploads and keeps an ASTC texture. A linear picture's lines say
`"expected_from":"astcenc"`, an sRGB one's `"spec"`: its linear value is astcenc's code through the
sRGB transfer function, which no decoder here gives.

## eac

Four cases. The one- and two-channel 11-bit EAC formats of ETC2 (the library's
R_ETC2 and RG_ETC2), one 4x4 block each written by the processor and fetched texel by texel:
every index, both clamps, and the multiplier of nought, which adds the bare modifier. What each
texel must be is `source/eacref.c`, the specification's formula, which agreed with Mesa's
`texcompress_etc.c` on a million random blocks; the 11-bit value may reach the target as either
of two bytes (divided by 2047, or extended to 16 bits first). A host without EAC textures has
to decode them itself; in an emulator a failure points at the EAC format mapping
(`texture_cache/format_lookup_table.cpp`, `surface.h`) and at whether it decodes EAC for such a
host.

## etc2

Twenty cases. The ETC2 colour formats (the library's RGB_ETC2, RGB_PTA_ETC2 and
RGBA_ETC2), one 4x4 block each written by the processor and fetched texel by texel: an individual,
a flipped differential, a T mode, two H mode blocks (the first base the larger and the smaller,
which sets the distance index's last bit), a planar block, three punch-through blocks (index 2
transparent in differential and T mode, and planar always opaque), and an RGBA block whose 8-bit
alpha reaches both clamps. Then eight blocks, each for one rule a decoder can have wrong and still
pass those ten: pixel indices that differ along a row, where the others' are the same read down
the columns or along the rows; an H mode block with the same base twice, which counts as not
smaller, and one whose bases share their red; two differential blocks whose second base is
exactly 31 0 0 and 0 31 31, the edges of the range past which the T, H and planar modes begin; a
planar block of six bit colours whose top bits repeated are not the code over 63 rounded (11 is
44, not 45); a see-through punch-through H block; and an RGBA block whose alpha multiplier is
nought. Every block is built from its fields by `source/etc2ref.c`, whose
decoder says what each texel must be; the reference suite holds it to Mesa's. ETC2 decodes to
exact bytes, so the tier is exact. A host without ETC2 textures has to decode them itself; in an
emulator a failure points at whether it does (`renderer_vulkan/maxwell_to_vk.cpp`,
`vk_texture_cache.cpp`, `texture_cache/util.cpp`, `ConvertImage`): an upload of the raw blocks
shows bytes of block data. A failing case whose texels 0 and 1 are the block's own bytes (an RGB
format's alpha may read 255, from its swizzle) says so: the block was uploaded as it is and never
decoded, the way the order and coherence suites name their signatures. The reference suite checks
that every block's bytes read that way and that no block's colours do.

## bcn

Twenty eight cases. BC4 arrays whose levels or layers do not end on a block boundary: six
layers with levels down to 1x1, the shape of every mipmapped cube map; two layers six texels
tall; and a 4x16 texture whose second level is two texels wide and two blocks tall. Every block
is one value unique to its layer, level and position, written by the processor where
`source/bcnref.c` says the library put it, and every texel of every level and layer is fetched.
Exact tier. A host with BCn textures samples the blocks as they are and passes; one without
decodes them on upload, and in an emulator a failure points at that decode's walk over the
layers of a level (`texture_cache/decode_bc.cpp`, `DecompressBlocks`). The three array cases end
Eden, and the fork before ECFE-2026-1163, inside the pipeline cache: the array fetching shader
compiles to a PRET; the launch after goes on past each.

Then one block of every BCn format, built a field at a time from the Khronos Data Format
Specification's S3TC, RGTC and BPTC chapters and fetched texel by texel onto a target of 32-bit
floats: BC1's four and three colour modes, BC2's four bit alphas, BC3's eight and six alpha modes,
BC4 and BC5 unsigned and signed, BC6H mode 11 unsigned and signed and a reserved mode (opaque
black), BC7 modes 6 and 5 (with the rotation that swaps alpha and red) and a block with no mode
(transparent black). The interpolants are the ones NVIDIA's GPUs decode, which ScarletNX's BCn
decoder test holds its decoder to, and which differ from the specification's fractions where a
case can tell: BC1's midpoint of 31 and 0 is 127, BC3's six-value alpha at index 2 is 48 (a fifth
of 255 is 51), BC4's index 2 between 255 and 0 is 56355 of 65535 (six sevenths is 56173), BC6H's
index 8 in mode 11 is the half 0x41df. BC1's two thirds of 31 and 0, 170 and 85, are the
specification's as well, so seven more cases take endpoints where each formula gives its own
number: a BC1 block between 31 9 2 and 2 0 31, whose thirds are 176 25 96 and 96 11 176 (rounded
thirds give 175 24 96, an RX 6700 XT 177 24 94) and whose midpoint, 136, is above the half where
127 is below it; BC3 and BC4 through every weight of both modes between 255 and 4, where all ten
differ from the rounded sevenths and fifths; and a BC7 block of mode 1 with two subsets and the
second one's anchor at texel 2, whose indices line up only for a decoder that reads the partition
and the anchor from the tables. A channel times 255, 65535 or 32767, rounded, must be the decoded
integer; a BC6H channel must be the half exactly. A host that samples BCn natively answers with
its own GPU's interpolants, which is the question these ask of it.

## zeta

Seventeen cases. Depth textures the processor writes, sampled texel by texel: Z16 and ZF32
as controls, then Z24S8 twice, once with a word that reads the same whichever way its depth and
stencil are split and once with depth 0x00ff00 over stencil 0x5a, whose low 24 bits would give
254 where the depth gives 1. Exact tier: each depth is a whole number of 255ths or a quarter
away from a half. In an emulator a failure points at the upload of a depth texture: a host
without D24_UNORM_S8_UINT that copies the words into a 32-bit float depth image, or a copy that
takes the depth from the low 24 bits (`renderer_vulkan/vk_texture_cache.cpp`,
`TransformBufferImageCopies`).

Three more sample through a view of the stencil or of ZF32_X24S8: the stencil of Z24S8 from the
low byte (deko3d's view puts it in all four channels, fetched as unsigned, `swizzle.frag.glsl`),
ZF32_X24S8's depth from its float and its stencil from the low byte of the next word, with 0xc3
in the byte above it to catch a host that takes the wrong one. Then depth targets the graphics
unit clears and the processor reads in memory after the level two cache is written back, the
bytes a game finds when it copies a depth buffer through memory (ScarletNX's depth write-back
test): each cleared to 0.75, which is 0xbfffff in 24 bits and 0xbfff in 16 under either rounding,
and stencil 0x5a. Z24S8 holds 0xbfffff5a (deko3d's surface format S8Z24Unorm, 0x14: the stencil in
the low byte), Z24X8 0xbfffff above its low byte, Z16 0xbfff, ZF32 its float, ZF32_X24S8 the float
then 0x5a, S8 0x5a. What the low byte of Z24X8 and the 24 bits above ZF32_X24S8's stencil hold, no
format says, so a note gives it. Last, a three layer target cleared a layer at a time through a
view of one layer, to 0.2, 0.4 and 0.6: as ZF32 each layer copied out holds its float, and as
Z24S8 each layer sampled holds 51, 102 and 153 of 255 (ScarletNX's depth array test). In an
emulator these point at a depth target it never writes back to memory, or writes back in its
host's layout, and at a layered depth target cleared or kept as one layer.

## swizzle

Fourteen cases. Block linear textures the processor writes texel by texel at the
addresses the block linear rule gives (`source/coherref.c`, `CohBlockLinearOffset`) and samples
back one texel per pixel: 1, 2, 4, 8 and 16 bytes per texel, blocks 1 to 32 gobs tall, and a
37 by 23 texture that ends inside a gob. The texels are unsigned integers folded into one word
by `swizzle.frag.glsl` with shifts and ors, so every bit is compared exactly; each case first
checks the library laid the texture out in the bytes the rule says. The last case writes a
1024 by 1024 texture, 4 MiB, and its time is the whole upload an emulator makes on first use.
In an emulator a failure points at the unswizzle, on the processor (`textures/decoders.cpp`) or
in a compute pass (`host_shaders/block_linear_unswizzle_2d.comp`, `vk_compute_pass.cpp`).

## texel

Seven cases. RGBA8_Uint buffer textures 0, 4, 20, 36 and 68 bytes past a 256 byte
aligned address, all of them the four bytes the library asks of this format; column x must
fetch texel x, and a wrong one says how many bytes away the texel it showed sits. A host that
aligns texel buffers more coarsely (64 bytes is common on phones) has to make up the difference
somewhere other than the texels a shader reads; in an emulator a failure points at how a
buffer view is made (`renderer_vulkan/vk_buffer_cache.cpp`, `Buffer::View`).

## stream

Four cases, five with `stress`. One list of draws, each covering one pixel with the
first word of a uniform buffer of its own, every buffer different memory the processor filled
before the list: 4096 of 4 KiB (16 MiB) and 1024 of 64 KiB (64 MiB). Pixel k must hold buffer
k's word; the time is what the uploads cost. With the mode word `stress`, 4096 of 64 KiB,
256 MiB in one list, more than the ring an emulator stages uploads through, which then has to
wrap inside the list without handing a later draw's bytes to an earlier one. It is off by
default because an emulator that runs out of memory on it takes the rest of the run down with
it. In an emulator a failure points at the staging ring (`renderer_vulkan/vk_staging_buffer_pool.cpp`,
`GetStreamBuffer`) and the uniform buffer upload path (`buffer_cache/buffer_cache.h`).

## fill

Five cases. 128 quads over a 512 by 512 target in one list, first with colour alone
and then with a 32-bit float depth buffer tested and written by every quad, each nearer than the
last. The last quad's colour and depth must cover the target, so a time is never the time of a
wrong answer; the difference between the two times is what depth costs the machine, which is
where an emulator that loses depth compression (a depth image created with usage it cannot
compress, `renderer_vulkan/vk_texture_cache.cpp`) pays. A single depth tested quad goes first
and takes the target's first use in each pass: without it, whichever case came first measured
about 11 ms against 2 on RADV, in either order. There, colour alone takes about 2.0 ms and depth
2.7.

## ring

Four cases. One list of 4096 indexed draws, each covering one pixel with the word of
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

## occlusion

Six cases. The samples counter, which the library leaves enabled, reported
after each of 320 quads (reset before each, so each must count exactly its 64 pixels) and
after each of 320 cleared cells, in one list each: more than 256 reports, so an emulator that
keeps its queries in banks of 256 takes a new bank in the middle of a list, at a draw or at a
clear. Every quad, every count and every cleared cell is judged. An emulator that resets a
new bank's queries by ending the render pass the draw or clear has just begun records that
draw or clear outside any render pass; in Eden only on a driver without host query reset,
which the masquerade layer gives on any driver.

## drawtex

Eleven cases. DRAW_TEXTURE, the call the console's graphics API names DrawTexture:
a texture's rectangle copied onto the target by one method, between two draws. The library
has no call for it, so it goes in as raw words (`rawref.h`, `RawDrawTexture`), in the layout
the emulators that implement it read (the public class header leaves the range out). A list
draws the left half with a flat pipeline, copies a 16 by 16 texture onto x 40 to 56, y 24 to
40, and draws the bottom quarter with the same pipeline, or with another one; the copy, the
draw after it with the pipeline of the draw before, and the draw after it with another
pipeline are each a case. An emulator that blits with a pipeline, a viewport and a scissor of
its own behind its cache of what is bound draws the next quad with the blit's pipeline, or
into the blit's rectangle. The layout is the emulators' and has not been checked on a console.

The last five ask what the copy is made of. NV_draw_texture, the same call on the desktop, sends
its fragments through every per-fragment operation and takes its texels through the sampler it
names, which a copy done as a blit does not: a texture of columns (column x red 16 x, green
255 - 16 x, blue 0x55, alpha 0x80) goes onto a target holding 0x30405060 with blending adding the
two (saturating where a channel passes 255), with only red and alpha open to writes, under the alpha
test at GREATER than 0.75 (rejected) and then LESS (kept), and from two texels left of the texture,
where a clamping sampler repeats column nought and a repeating one starts at column 14. The source
origin is signed, as the emulators read it (`maxwell_3d.h`, `DrawTexture`).

## query

Twenty five cases and three notes. The samples passed counter over quads of whole
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

## dma

Five cases. The copy engine copies a 64 KiB buffer 32 times in one list, first from
words the processor wrote and then from words a dispatch wrote just before; every copy is
judged word by word. The difference between the two times is what copying data the graphics
unit wrote costs: an emulator that stops the processor until the graphics unit is idle on each
such copy (Eden's buffer cache, `DMACopy`, with a fence setting that asks for it) pays it 32
times in the second case. A single judged copy goes first and takes the first use.

## blit2d

Five cases. The 2D engine, by raw methods, copies a block linear source at half and
at two thirds size, from each sample origin, into a 21 by 9 window of a block linear destination
whose rows start and end inside a gob's 16 byte runs. Every pixel is judged twice, in the raw
bytes the processor reads and in a copy to buffer, against the image each answer to two open
questions gives. See [The blit2d suite](Blit2d.md).

## copy

Nineteen cases and three notes. The copies a game's driver makes between images and buffers,
through the library's own calls, which send them to the copy engine (bytes, and a pitch it can
step backwards) or the 2D engine (formats, mirrors, scales): what an emulator remakes in its
texture cache or its DMA path. Every image is 16 by 16 and every texel a value of its own
(`CopyTexel`: x, y and the layer in its channels), put in and read out by the copy engine with
no flags, every byte read back judged: exact. The rules are `source/copyref.c`'s, pinned by the
reference suite. In the order they run:

- **A round trip.** A picture copied into an image and back, no flags: every texel. Every case
  after it puts its pictures in and reads them out this way.
- **Flips by the copy engine and the 2D engine.** From a buffer with FlipY: the library sends
  the copy engine through the buffer from its last row with a negative pitch, so the image holds
  the picture upside down; to a buffer with FlipY, the same the other way. With FlipX the library
  takes the 2D engine instead, its source stepped from the right: each row mirrored, from and to a
  buffer, and both flips together from one. Five cases.
- **Padded buffers.** To a buffer whose rows are 96 bytes apart for 64 bytes of texels, over a
  fill of 0xee: every byte judged, the 32 between rows left as they were; and from such a buffer,
  0xee between its rows, each row taken from its own place.
- **Between images.** An 8 by 8 rectangle from (3, 5) of one picture to (6, 2) of another, the
  rest of the second untouched; an R32 float image copied whole into an RGBA8 one keeps every
  byte, among them a signalling NaN with a payload, a negative NaN of all ones, the smallest
  denormal and negative nought, which a copy through a float view changes; and four layers of an
  array copied with FlipZ, the last first.
- **Blits that convert.** Channels of nought and full (`CopyFullTexel`: every combination in
  every 4 by 4 square), which every format holds exactly whatever a converter rounds, blitted 1:1
  and nearest from RGBA8 into BGRA8 (red and blue swapped in memory), RGB565 (alpha dropped, each
  field all ones or all noughts) and RGBA16F (one is 0x3c00), and from RGBA16F back into RGBA8.
  Four cases.
- **Blits that move texels.** FlipX and FlipY together turn the picture around; a 16 by 16
  picture onto 32 by 32, nearest, puts each texel over two by two pixels (the centre of pixel x
  falls on texel x / 2); and a blit between two sRGB images keeps the bytes, where a blit through
  a linear view of either side would decode or encode them a second time.

The notes, which no machine is held to, since a console run is what says which answer is right:

- **sRGB into linear.** 0x80 in each colour channel blitted from an sRGB image into an RGBA8 one:
  0x37 if the 2D engine decodes the source, 0x80 if it moves the bytes.
- **A half of a quarter into eight bits.** 63.75: 0x3f truncated, 0x40 rounded to nearest.
- **The 2D engine's blend.** A source of ff 00 00 at alpha 0x80 blitted with the library's
  ModeAlphaBlend (the 2D engine's BLEND operation) over a destination of 00 00 ff ff: what it
  mixes, if it mixes. The library never sets the factor that operation reads, and Eden leaves any
  operation but a plain copy unimplemented.

In an emulator a failure points at the copy engine's pitch and its sign (`engines/maxwell_dma.cpp`,
`CopyBlockLinearToPitch`, `CopyPitchToBlockLinear`), the 2D engine's source step and format
conversion (`engines/fermi_2d.cpp`, `Blit`; `texture_cache/texture_cache.h`, `BlitImage`; a
negative du/dx), a copy between images of two formats of one size made as a conversion rather
than a byte copy (`texture_cache/util.cpp`), the layers of a FlipZ copy, and sRGB views in a
blit (`vk_texture_cache.cpp`, `BlitImage`).

## tex8k

Fifty six cases, on textures as large as a game's largest: 8192x8192, which the texture unit
takes (its limit is 16384), in RGBA8, BC1, BC7, ASTC 4x4 and ASTC 8x8, and one BC1 at 16384x16384.
The question is what each costs, so the suite runs once, not ten times: the uploads are the ones a
game makes once, and a run stays seconds long on a console. Each case times its own work on the
graphics unit (the submits, as every timed case does) and its line carries that time; the frames
case carries its median frame. The detail gives the parts: the copies apart from the draw, the
first frame, the 99th percentile (the frame time behind a 1% low) and the slowest, and what the
processor spent filling the staging, writing a region and reading back, from the system tick,
since an emulator can spend there what the console does not. `notime` drops the times and the
parts.

An emulator keeps each of these textures on its host, with its staging and a copy of its own:
under ScarletNX on a 12 GiB card the suite's staging alone peaked at 4512 MiB, and a handheld's
graphics device was lost inside the suite, its display reset with it. So the suite asks what the
machine has before it tries: the mode file's `vram=<MiB>`, which `make run` writes from the
host's card ([Under an emulator](../Emulator.md)). Under 4096 no texture is tried: the case that
opens each device is a `skip`, `unavailable`, and the cases on that texture `depends`.

```json
{"type":"case","suite":"tex8k","name":"graphics device opens with room for the 8192x8192 RGBA8 texture","tier":"none","result":"skip","reason":"unavailable","detail":"the mode file says this machine has 1024 MiB of graphics memory (vram=1024), and the suite's textures want 4096: not tried, since a host that runs out can lose its graphics device; a mode file with no vram=, or one of 4096 and more, tries them"}
```

With no `vram=`, which is a console and any emulator opened by hand, every texture is tried.

Every texture has a device of its own, sized for it, so a run never holds two. The copy engine
uploads it from staging the processor filled, each level a square of level 0's staging from a
corner of its own, so a fetch of the wrong level reads other texels: the first eight levels (8192
down to 64), and the BC7 texture's whole chain, fourteen. One RGBA8 texture is pitch linear
instead, in memory the processor writes, with no staging and no copy. Then, per texture:

- a draw fetches level 7 alone, the size of the 64x64 target, a texel a pixel: an emulator that
  makes every level before it samples one pays for that here;
- a draw fetches 4096 texels spread over level 0, every 128th, and the same draw again;
- sixty four frames each write a 256x256 region of level 0 somewhere else, by the copy engine from
  a staging slot the processor fills that frame (or by the processor, in the pitch linear one),
  and fetch every 32nd texel of it onto an 8x8 cell in the same list; sixty four, so the 99th
  percentile is a frame time of its own, between the two slowest;
- the 4096 texels again, which now read each frame's region where one wrote and the upload
  elsewhere;
- a 256x256 region of level 0, a quarter inside a frame's region, copied out to a buffer and
  compared byte for byte, blocks for the compressed formats.

Then the writes a partial update of a texture has to get right at its edges, where an emulator that
reloads only what changed works out which texels a write reached:

- in the pitch linear texture the processor writes a 48x48 square from texel 1000, 4100, whose rows
  cross the page edge at texel 1024 (a row is 32 KiB), and a draw fetches every second texel around
  it: the square, the edge inside it and the texels on its four sides;
- in the BC7 texture the copy engine copies raw blocks, laid out as the texture's memory holds
  them, over the last 4 KiB of level 3 and the first 4 KiB of level 4, and a draw fetches both
  levels, half of each window written;
- then 512 bytes into its mip tail, levels 8 to 13, which share one 4 KiB page: half of level 9's
  blocks and all of level 10's, and a draw fetches the six levels whole, the four not written
  holding the upload.

The raw bytes go through the memory's pitch mapping, where a copy lands as it is; where each block
sits is `source/bcnref.c`'s rule for the levels (deko3d's), and the case first checks the library
laid the texture out in the size that rule gives.

After the textures, a cold upload, on a pitch linear RGBA8 texture of its own: the processor
writes all of it and a draw fetches 4096 texels, then the same right after (warm), then 32 frames
that only sample it, a 60 Hz frame apart, then the whole write again (cold). An emulator that hands
back the memory it staged an upload in after a few quiet frames pays for it here; the detail gives
the cold draw, the warm one and the first.

Last, RGBA16F targets in memory the processor reads. An 8192x4096 one, 256 MiB, is cleared and
drawn over a rectangle and the processor reads eight texels through memory, four inside and four
outside. An 8192x8192 one, 512 MiB, the same at ten texels, rows 4095 and 4096 among them, either
side of its 256 MiB mark; then the 2D engine copies a 64x64 window across that mark and the drawn
rectangle's edge into a pitch linear image, and the processor reads every texel of it.

Every texel's value follows from its place, the upload or write that wrote it last and the format
(`source/tex8kref.c`): an RGBA8 texel is `x | y << 13 | s << 26`, s the salt folded into 1 to 63
(nought stays nought), so every texel of the texture differs; a BC1 block is one colour from a mix
of its place, and a channel may read as its code with the bits repeated or the code over its
largest rounded, which part ways on some codes; a BC7 block is mode 6 with both endpoints from the
mix and texel t taking index t, so its sixteen texels differ; an ASTC block is one of astcimage's
picture's blocks, the picture tiled and each tile turned by a shift its place and the salt choose,
any of the three bytes the astc suite allows. The reference suite holds the BC7 and BC1 blocks to
Pillow's decode of the same bytes, the ASTC texels to the picture's, and the regions, levels,
samples and raw copies to where they must sit. A device the machine has no memory for is a skip,
`unavailable`, naming the sizes: the homebrew menu in applet mode has far less memory than a title,
and may refuse the RGBA8 texture's 341 MiB with its 272 MiB of staging, or the 512 MiB target,
where the menu started over a title takes them.

Exact tier, the device opening none, but for an ASTC texture whose texels moved. A host that
samples no ASTC may decode it and encode it again as BC7, which moves texels; such a texel passes
when it is within BC7's loss on the same texels, a bound a channel per region of the picture:
8 in the gradients, 76 in the edges, 61 in the noise, 8 in the alpha. Each is the worst channel
of BC7 encodes of the picture's 4x4 and 8x8 decodes by ScarletNX's `bc7_encode.glsl` and by
bc7enc (etcpak 0.9.15, levels 0, 2 and 4, linear and perceptual weights); the texture holds no
4x4 block the picture does not, so that is the worst those encoders do anywhere in it, and the
reference suite holds each bound to the texel it was measured at. A case whose texels all came
back one of the right bytes says exact; one that passed within the bound says
[bound](../Design.md#the-tiers), and its detail how many pixels moved and the farthest in each
region against its bound (`within BC7's bound: 2648 pixels off, by at most 3 of 8 in gradients,
21 of 76 in edges, 35 of 61 in noise, 3 of 8 in alpha`); a texel past the bound fails, with how
far it is. An encoder that keeps to BC7's mode 6 alone moves the edges by up to 140 and fails
there.

In an emulator a failure or a time points at how it uploads a texture that large: in pieces or in
one allocation, whole on every change or by the region that changed, which blocks a write reached
at a page or a level's edge, a compressed format decoded or encoded again past BC7's loss, staging
handed back and taken again, and how a render target or a 2D engine copy reaches memory past an
allocation's cap.

The last case is a volume past 512 Mi texels: BC1 1024x1024x513, 257 MiB, in memory the processor
writes, of which it fills the first block alone, as a game's light volume is made by the graphics
unit and sampled where it was written. Its sixteen texels fetched must give the block's colours
(its two endpoints, red and blue, which every decoder gives exactly); a host that
takes a volume that large for no texture reads zeros (ScarletNX's big volume test). It skips as
unavailable where `vram=` says the machine is short, as the textures do.

## What a console run settles

Questions only a console answers, asked about the cases both emulators fail in this family; each
case's expectation already follows the specification or the library, and the console says
whether the machine does.

- **Q1, etc2.** Do the eighteen blocks give exactly the specification's texels? Expected yes.
- **Q2, zeta.** Does a Z24S8 word 0x00ff005a written by the processor sample as 1 (depth in the
  top 24 bits, as the case expects) or 254 (the low 24)? And 0x80808080 as 128?
- **Q3, query.** In one list, with a full barrier between the reports and the render enable, does
  IF_EQUAL over 9600 and nought skip (the reports visible) or draw (the memset's nought read)? And
  does a clear obey the render enable?
- **Q4, geometry.** Does the patch cover exactly 8..56 by 8..56, with alpha 4 and 7?
- **Q5, geometry.** Does the geometry shader make the four squares, and only two in the second case?
- **Q6, query notes.** What CONDITIONAL does over the value one, over two to the 32 and over a
  report of 9600 in the same list, which says which of its bytes decide.
- **Q7, tex8k.** What the console's times are: the copies of 341 MiB, a region's frame and its
  99th percentile, the first draw of a level, the cold and the warm write, the processor's fill and
  reads; an emulator's are held to them. Whether the RGBA16F texels and the 2D engine's window read
  through memory after a full barrier and a wait. And whether the raw copies through the pitch
  mapping sample as the blocks they carry.
- **Q8, fixed.** Does a point's `gl_PointCoord` read 16 or 239 at its first row in memory, under the
  PointCoordReplace origin deko3d writes (lower left)?
- **Q9, fixed.** With the clamp off, is a quad at z -0.5 or 1.5 clipped, and with the clamp on is it
  drawn at depth 0 and 1? And do the face cases hold: is the face decided after the viewport's scale
  and swizzle, as the cases expect?
- **Q10, stencil.** Which of the two quads, wound opposite ways, takes the front face's state
  under the window origin the library sets up? The case passes either way and its note says which;
  ScarletNX gives it to the one wound counter-clockwise in clip space.
- **Q11, coverage.** What alpha to coverage covers at one eighth to seven eighths, with the dither
  off and on: the fourteen notes, one per alpha and dither, hold the masks the 256 pixels took.
- **Q12, coverage.** Do the cases the specifications decide (NV_fragment_coverage_to_color,
  NV_framebuffer_mixed_samples, NV_sample_locations) come out as written: the raster coverage in
  a one sample integer target, the split each position pattern gives, the grid's odd columns, the
  modulation and its table, and 0xa after the depth test? Expected yes; no emulator passes most of
  them yet, so a console run is the first that says the cases are right.
- **Q13, topology.** Six cases no emulator passes yet, ScarletNX and Eden alike, whose answers are
  OpenGL's table 10.2 and section 10.1 for the types the 3D class shares with it: a quad's and a
  quad strip's flat colour from their last vertex, quads that restart at an index, a line loop's
  closing side, and a quad and a polygon drawn as lines with no diagonal. Do they come out as
  written? Expected yes. And the two notes: the vertex whose colour a polygon takes (OpenGL: its
  first under either convention; both emulators give it their fan's last vertices, 3 and 4), and a
  quad's under the first vertex convention (OpenGL: its first or its fourth; both give the first).
- **Q14, copy.** Six cases no emulator passes yet: FlipY to and from a buffer, which the library
  sends to the copy engine as a negative pitch, and the four blits between formats, whose answers
  are the formats' layouts in memory. Do they come out as written? Expected yes. And the three
  notes: what an sRGB 0x80 blitted into RGBA8 gives (0x37 decoded or 0x80 kept; ScarletNX keeps it,
  Eden decodes two channels of three), what a half of a quarter becomes in eight bits (0x3f or
  0x40), and what the 2D engine's BLEND operation makes of a half transparent source (both emulators
  copy it).
- **Q15, vertex.** What a fixed attribute, the state deko3d leaves every unbound one in, gives a
  shader: Eden gives OpenGL's `0 0 0 1`. And do the eleven cases hold? Eden passes all but BGRA.
- **Q16, queue.** Do the waits hold and wake as written? ScarletNX passes all five; Eden runs the
  waiting list at once and releases the other queue's wait before that queue's copy lands.
- **Q17, zeta.** What a cleared Z24S8, Z24X8 and ZF32_X24S8 target holds in memory, and what a
  stencil view of Z24S8 and ZF32_X24S8 samples: the cases expect the stencil in the low byte under
  0xbfffff (deko3d's S8Z24Unorm surface) and the stencil byte after ZF32_X24S8's float, and the
  stencil in all four channels of a view. No emulator passes them: ScarletNX writes no depth target
  back to memory at all (scarletnx/ScarletNX#1737) and samples the stencil views as other bits (0x45ba4144 for 0x5a); Eden
  writes 0x5a5a5a5a for Z24S8 and both words of ZF32_X24S8, nothing for Z24X8, and samples the
  stencil views as nought. Z16, ZF32 and S8 Eden writes back as expected.
- **Q18, bcn.** Do the BCn blocks decode with the interpolants the cases expect, NVIDIA's as
  ScarletNX's decoder has them (BC1's thirds 170 and 85, its midpoint 127, BC3's six-value index 2
  48, BC4's index 2 56355 of 65535)? Both emulators sample BCn on the host GPU (an RX 6700 XT here)
  and give its interpolants, alike in every case: 171 and 84, 128, 52, 56320, and 177 24 94 where
  the second BC1 block expects 176 25 96. BC5, BC6H and BC7, the mode 1 block among them, and the
  endpoints pass on both. ScarletNX decoding BCn itself (`native_bc=0`) passes all twenty eight.
- **Q19, order.** Does a compute program's barrier as uam compiles it (BAR.SYNC with RZ for its
  barrier and its count) hold 64 invocations until all have written their shared words? Neither
  emulator runs the dispatch: ScarletNX says `BAR f0a803800ff7ff00` is not translated, and Eden
  leaves every word as it was. The same 48 KiB without a barrier passes on both.
- **Q20, xfb.** What the primitives written counter reads after one captured triangle: the case
  expects 1. ScarletNX, which captures the positions, reads 1000; Eden, which captures nothing,
  reads nought.

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
- **Viewport swizzles other than NegativeY are dropped on a host without VK_NV_viewport_swizzle.**
  `renderer_vulkan/vk_rasterizer.cpp:82-83` (read at 44395ac49c) emulates NegativeY alone, by
  negating the viewport's height, and passes no other swizzle on: the fixed suite's swap of x and
  y comes out untransposed on RADV.
- **The 2D engine's scaled copy stays in the texture cache.** `engines/fermi_2d.cpp:84-88` and
  `:112-115` send any copy with a scale to `AccelerateSurfaceCopy`, which draws it into the cache's
  image of the destination, and nothing writes that image back to memory before the processor
  reads it (`texture_cache/texture_cache.h:712-713`, `engines/maxwell_3d.cpp:588-592`): the
  blit2d cases fail on the raw read and pass on the copy to buffer. See [The blit2d
  suite](Blit2d.md).
- **A restart index other than all ones is not passed on** (read at 44395ac49c, as the two after
  it). The Vulkan path never reads `primitive_restart.index` (only
  `renderer_opengl/gl_rasterizer.cpp:960` does), so a strip or a fan with restart at 8 draws through
  it: the topology suite's four restart cases at 8 fail, and the all-ones 0xff of 8-bit indices
  passes.
- **An indexed quad draw's first index is taken for its vertex offset.**
  `buffer_cache/buffer_cache.h:766` hands `index_buffer.first` to `BindIndexBuffer`'s `base_vertex`,
  which the quad pass adds to every index (`vk_buffer_cache.cpp:580-589`), and the draw's own vertex
  offset is not passed: the case with first index 3 and offset 2 draws nothing where its quads are.
- **Quads, quad strips, polygons and line loops are made into triangles.** A quad's two triangles
  carry their own last vertices under the last vertex convention, so half of each quad takes its
  third vertex's colour; a quad or polygon drawn as lines shows the diagonal the two triangles
  share; a polygon takes the colours of its fan's last vertices, not its first; and a line loop
  (`maxwell_to_vk.cpp:352-353`, made a triangle list) loses its closing side. ScarletNX fails the
  same six cases.
- **A copy engine copy that steps a buffer backwards.** The library's FlipY on a copy to or from a
  buffer is a negative pitch. From a buffer, `engines/maxwell_dma.cpp:275` sizes the source as the
  pitch times the lines in a `size_t`, which the negative pitch makes enormous: the program ends
  (`std::bad_alloc`). To a buffer, `:166` takes the pitch's absolute value and keeps the start, the
  last row's, so the rows go forward from there: one row of sixteen lands. ScarletNX writes none of
  either.
- **The 2D engine's copies between formats.** RGBA8 into BGRA8, RGB565 and RGBA16F, and RGBA16F into
  RGBA8, come back with their channels in the wrong places (`engines/fermi_2d.cpp`,
  `texture_cache/texture_cache.h`, `BlitImage`): an RGBA16F quarter reads ff 3f 3f 3f in RGBA8,
  alpha first, and the sRGB note's 80 80 80 reads 80 37 37. ScarletNX copies the bytes unconverted.
  The four cases follow the formats' layouts in memory (BGRA8 is the 3D class's A8R8G8B8, blue
  first); no emulator passes them yet.
- **A vertex attribute's BGRA order is dropped.** A 4x8 unsigned normalised attribute marked BGRA
  reads red from its first byte (`maxwell_to_vk.cpp`, `VertexFormat`, which maps the size and type
  and not the order): the vertex suite's BGRA case fails, the other ten pass. ScarletNX draws none
  of the suite's points, positioned by the vertex's number with no position attribute.
- **A queue's wait on a word in memory is not kept.** A list that waits for the word runs at once,
  its copy made while the word is short, and another queue's release reaches it before that queue's
  own copy has landed (`engines/puller.cpp`, `ProcessSemaphoreAcquire`): three of the queue suite's
  five cases fail. ScarletNX passes all five.

What the image cases found, read at 44395ac49c:

- **A multisampled target's samples are elsewhere than the 2x2 layout puts them.** The triangle's
  samples come back with sample 1 of the pixels on its edge green, where the standard positions
  leave it blue, the resolve averages the wrong samples, and a 4x texture the processor wrote is
  fetched sample by sample from other places (48 of 64): the msaa suite's first, second and fourth
  cases fail. Its 4x depth clear and the conservative triangle pass. ScarletNX passes all but the
  depth clear, which it never writes back (scarletnx/ScarletNX#1737).
- **A rectangle view's texel coordinates are taken as normalised.** The lookup at (3.5, 0.5) reads
  texel (2, 2), where the descriptor's coordinates are off; reden reads the bit
  (`shader_environment.cpp:48`, `texture_cache/image_view_info.cpp:73`) and the lookup still
  repeats. ScarletNX passes.
- **A copy into a volume's slices, and out of them, puts them elsewhere.** The storage suite's
  volume fills slice by slice through the copy engine and reads its slice 0 back as slice 2's bytes;
  the two volume cases fail and the arrays' pass. ScarletNX passes all eighteen.
- **A packed depth target written back holds its stencil everywhere** (Q17).

What the launch cases found, at the same commit:

- **A fragment program the copy engine rewrites in the middle of a list keeps its old code.** The
  draw after the copy, a full barrier and the shader cache's invalidation draws red where the new
  program draws green; the processor's rewrite between two lists passes. ScarletNX passes both.
- **Transform feedback set by the 3D class's registers captures nothing.** The buffer keeps every
  byte it held (`vk_rasterizer.cpp`, `HandleTransformFeedback`, which the registers reach); the
  counter reads nought (Q20). ScarletNX captures the three positions.
