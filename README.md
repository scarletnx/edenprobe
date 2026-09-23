# edenprobe

A test program that runs on the console and inside an emulator of it, decides for itself whether
the graphics came out right, and writes a report you can diff between the two.

It is not a benchmark and not a demo. Every case computes the answer it expects and checks the
answer it got. Nothing here needs a person to look at the screen and judge.

It was gfxprobe until 23 September 2026. The name changed because the program is growing suite
families beyond the graphics unit, and one binary with one judge is the reason to keep them
together rather than start a second program next to it.

## Why not compare screenshots

Because two correct implementations are allowed to disagree, and the specification says so. The
graphics API requires division to land within 2.5 units in the last place, the logarithm within
3 outside the range from a half to two, the power of two within 3 plus twice the magnitude of
its argument, and the sine and cosine only within an absolute error of 2^-11 and only inside
minus pi to pi. Sub-pixel rasterisation precision is a limit each implementation reports, not a
constant. A hash of a
shaded frame therefore breaks between two machines that are both right, and the projects that
took that route maintain one reference image per backend, per driver, per operating system, and
still chase false positives.

So the decision is made inside the program, on both machines, by the same code.

## The three tiers

Which tier a case uses is a claim about the hardware, and it belongs in the case.

- **Exact.** Bit for bit. Legal only where the result is defined exactly: integer render targets,
  integer arithmetic, bit patterns moved around. A float shader never qualifies, however tidy the
  numbers look on one machine.
- **Units in the last place.** For float work, with the budget stated per case.
- **Position.** The right answer, possibly a pixel or two away, for rasterisation coverage.

## What it checks today

Eleven suites, in the order they run, because each leans on the ones before it. A run is 257
cases.

- **selfcheck**, sixteen cases, no hardware involved. That two neighbouring floats measure one
  unit apart, that a zero budget rejects them, that an absolute budget is the same on both sides
  of the expected value, that a NaN is never equal to anything including itself, not even against
  a budget of every value there is, and that a budget of three units in the last place reaches
  six representable values below a power of two and three above it. If one of these fails,
  nothing else the suite says can be believed.
- **support**, fifty cases, no hardware and nothing of the hardware's looked at. The two
  things this program does around a verdict rather than to reach one: the picture it leaves
  beside a failing case, and the screen the detailed mode shows that picture on. It checks that
  a case name becomes the file name its own line says it does, that a written picture parses
  back as the image its header claims to be, that the bytes kept beside a picture of an integer
  target are every byte that target held, that two case names which spell one file name still
  get a file each, that the suite takes its own fixtures away again, and that the navigation and
  the layout are what the screen is made of. One of them carries the whole idea: the diff
  picture is built from what the judge decided pixel by pixel and not from comparing the two
  images, so a pixel that differs and was allowed to is not painted as a difference. Every line
  is the none tier, because none of it is a claim about a machine.
- **reference**, sixty nine cases, no hardware involved either. The other half of the judge: where
  clip space lands on the target, which pixels a triangle covers, and which of two triangles owns
  the centres that sit exactly on the edge they share. It runs in both builds, because a
  reference that answered differently on the two processors would be blamed on the graphics unit.
  It also projects the very clip space numbers the rasterisation cases hand the hardware, so what
  it proves is the geometry those cases draw and not a restatement of it, and it pins fifteen
  transcendental answers as bit patterns worked out to fifty digits away from any C library, so
  that a runtime which drifts moves an expected value on one machine and says so there rather
  than inside a float case. Three of its cases are about the blend table rather than about
  arithmetic: that no case asks the graphics library for something it does not send, and that
  what the table gave up in exchange is still covered. The blend suite below says why.
- **clear**, five cases, on the graphics unit: one that reports whether the graphics device came
  up at all, at no tier because it compares nothing, and four that draw. Each of the four clears a
  64 by 64 RGBA8 target and copies all 4096 pixels back to compare them. Three cover the whole
  target, in three colours in a row, so a readback handing over a stale buffer cannot pass by
  accident. The fourth covers the target in one colour and then clears a rectangle inside it in
  another, which is what makes the answer depend on where a pixel is: a target that is all one
  colour reads back the same whether or not the copy walked its blocks in the right order, so the
  three before it cannot catch a transposed, flipped or shuffled readback and this one can. All
  four are Exact tier: the format is integer and every clear value is a constant, so each channel
  has exactly one right byte.
- **raster**, ten cases, on the graphics unit. A triangle or a quad drawn in one flat colour
  through a vertex and a fragment shader compiled into the binary, with every pixel compared to
  what the coverage rule says. Exact for pixels at least half a pixel from every edge, position
  for the band an edge runs through, and exact everywhere for the two cases whose geometry puts
  32 pixel centres exactly on a shared edge: those are decided by the fill rule, not by sub pixel
  precision, and an implementation that draws a shared edge twice or not at all says so there.
- **intmath**, seventeen cases, on the graphics unit. A fragment shader doing integer arithmetic on
  its own coordinate into an R32_Uint target: add, multiply, all three shifts, and, or, exclusive
  or, bit counting, bit scanning, division and remainder. Exact tier, and it is the strongest
  claim in the suite: an integer target normalises nothing, so a wrong answer is a wrong bit
  pattern rather than a difference of opinion about the last place. The first three cases are the
  fragment coordinate and the uniform buffer, which every case after them reads its operands and
  its instructions out of.
- **blend**, thirteen cases, on the graphics unit. The fixed function blend unit: adding,
  subtracting both ways round, the minimum and the maximum, the source alpha, the blend constant,
  and the clamp at each end of the range. Exact tier on a normalised target, which takes an
  argument rather than an assertion: the specification requires the conversion back to eight bits
  to round to nearest but leaves a halfway value to the implementation, so every case here is
  built to land at least 0.49 of a step away from one and the reference suite measures that
  rather than taking it on trust. Two of the cases set both factors to zero under the minimum and
  the maximum, because the specification says those two equations ignore the factors entirely.

  The alpha channel has its own equation and its own pair of factors, and wiring it to the colour
  one is the classic way to get this unit wrong, so the table keeps them apart. It keeps them
  apart by the equation and by the source factor and never by the destination factor, and that is
  a limit of the graphics library rather than a gap in the suite. `dkCmdBufBindBlendStates` writes
  six registers per target and puts the colour destination factor in the last of them, which is
  the alpha destination one, so a blend state whose two destination factors differ reaches the
  hardware as one whose two are the same (deko3d 0.5.0, `source/maxwell/gpu_3d_state.cpp`; the
  other five words each carry the field they are named after). A case built on that difference
  fails on a machine that has done nothing wrong, which is the one kind of failure this suite
  must not be able to report, so the reference suite refuses one and checks that some case still
  separates each of the two parts that do reach the registers.

  Whether the hardware then acts on those two is a second question the library leaves open, and
  the blend suite measures it rather than assuming. Separate alpha blending is switched on by the
  register immediately before the six that call writes, and no call in the library writes that
  one or its global twin, so what it holds is whatever the machine left there and a console and
  an emulator of one can disagree. Two notes on every run carry the answers: whether the alpha
  channel runs an equation of its own, and whether an alpha destination factor of its own has
  started arriving. They are notes and not cases because each is a property of the way to the
  hardware rather than of the blend unit's arithmetic, and the first is what makes the second
  readable: with no separate alpha state, a destination factor that made no difference and one
  that never arrived look the same.
- **depth**, thirty one cases, on the graphics unit. One quad laid down at a known depth and a
  second one challenging it: every one of the eight compare operations the graphics library
  offers, at every one of three depths, which is twenty four cases. Three depths each is the
  smallest set that works and not padding: three depths give eight patterns of pass and fail,
  there are eight operations, and each owns exactly one pattern, so leaving any depth out of any
  operation lets that operation share its answers with another and an emulator that had wired the
  two together would go unnoticed. One further case works all twenty four answers out on the
  processor and checks that the eight really are different, which is that argument rather than a
  claim of it. Plus the depth buffer read back and compared as a float bit for bit. Exact tier:
  the depths are 0.25, 0.5 and 0.75, which survive the transform into depth space untouched, so
  "the buffer holds exactly one half" is a statement about a bit pattern, and a fragment at the
  buffer's own depth really is an equality rather than two numbers that came out close. Three of
  the cases are about the write rather than the test, which is the half a colour cannot show.
- **texture**, twelve cases, on the graphics unit. A 4 by 4 texture drawn over the whole target, so
  sixteen by sixteen pixels land on each texel and every pixel names a texel this suite can work
  out. Nearest filtering with repeating, clamping to the edge and mirroring, each with a quad
  three copies wide so two thirds of it is outside the texture, and linear filtering laid out so
  the only weight the filter can use is one half and every pair of texels averages to a whole
  byte. Exact tier throughout, and what earns it is the layout: no pixel centre comes within a
  thirty second of a texel boundary, which the reference suite measures, and no two texels the
  linear case blends differ by more than 48, so the filter's weight would have to be wrong by one
  part in 96 before any byte moved.
- **coherence**, twenty one cases, on the graphics unit. Five are the none tier: the device, the
  shaders, the images and their starting texels coming up, and the block linear texture having
  exactly the size the gob rule below assumes. Sixteen are exact. Every suite above draws once
  into memory nobody has read, so the caches an emulator keeps between draws, submits and copies
  never have an old answer to give. Here each sampling and uniform case, except the first alias
  case, first makes the reader hold the old contents, then writes new ones by one path and reads
  them by another: a texture rendered and sampled in the same list, whole and in part; a texture
  the processor rewrites between two submits, pitch linear, block linear from the middle of a gob,
  and one row through a cached mapping flushed with `dkMemBlockFlushCpuCache`; a uniform buffer
  the processor rewrites between submits, once whole and then one word at a time over four
  submits, and one the list itself pushes between two draws; a render target sampled through an
  R32_Uint view of the same memory, judged as the little endian word rotated right by four bits,
  so every output byte mixes two input bytes; a texture filled by the copy engine from a buffer
  and from part of another image; and a render target copied, whole and in part, to another image
  and read back, which tests that rendered pixels are flushed before the copy engine reads them.
  Everything drawn or written is a pattern whose four bytes are unique to each texel and to each
  step, so a shift, a transposition or a stale texel is always a wrong byte, and the reference
  suite checks that over every seed. Region one of the target shows the old contents, region two
  the new, and a failing case counts how many of its wrong pixels hold exactly the old contents,
  which is what a stale cache looks like. The synchronisation is what `deko3d.h` documents for
  each hand over and no more, named in the suite's header: the fragments barrier between a draw
  that samples an image and one that renders it, the full barrier around the copy engine, the
  image or the shader invalidation before reading what was rewritten, and a wait on the queue only
  where the processor touches the memory, since a wait anywhere else would hide the fault the
  suite is there to find. Every expectation comes from one image model in `source/coherref.c`,
  pinned by the reference suite.
- **fmath**, thirteen cases, on the graphics unit. Addition, multiplication, a fused multiply and
  add, division, the square root, powers and logarithms of two, the sine and the cosine, into an
  R32_Float target, compared against the same arithmetic in double precision. Units in the last
  place tier, with the budget per case, because the tables give each operation a different figure
  and two of them are not even the same shape. They all come from one place, named in
  `source/fmathref.h`: the Vulkan specification's precision tables, core for the operations that
  are core instructions and GLSL.std.450 for the rest. That choice is not arbitrary. The shading
  language's own specification leaves the trigonometric functions undefined, so a suite leaning
  on it would have no figure at all for two of these nine; the Vulkan tables cover all nine, and
  they are the right ones to hold an emulator to. What they give, and what each case therefore
  allows:

  | operation | the table | what the case allows |
  |---|---|---|
  | addition, multiplication | correctly rounded | nought |
  | a multiply and an add | inherited from a multiply and an add | 1.5 |
  | division | 2.5 | 3 |
  | the square root | inherited from one over the inverse square root, which is 2 and 2.5 | 5 |
  | two to the power of x | 3 plus twice the magnitude of x | 3.5 plus 2 per unit of it |
  | the logarithm to base two | 3 outside the range from a half to two | 3.5 |
  | the sine and the cosine | an absolute 2^-11 inside minus pi to pi | the same |

  Most of those carry half a unit on top of the table's figure, for the reference itself being a
  double precision answer rounded to single. Three do not, and each says why on its own line:
  addition and multiplication are exact in double precision before the rounding, the multiply and
  add is measured against the processor's own fused one which is the exact answer, and the sine
  and cosine's budget is two thousand times larger than anything the reference could contribute.
  The power of two is the only budget that is not a constant, because the table's figure for it
  is not one: over the range this case sweeps the allowance runs from three and a half to eleven
  and a half, and a flat number would either fail a correct machine at the ends or wave the
  middle through.

  A unit in the last place is a distance and not a count of representable values, because the
  values just below a power of two are half as far apart as the ones just above, and the suite's
  own answers land on powers of two often. Each case reports the worst error it saw next to the
  allowance it had, so a run that passes still says how much room was left. Every case that
  computes something reports the units in the last place tier, including the two whose budget is
  nought, because a tolerance of nought is still a tolerance and a float shader does not qualify
  for Exact; the one case that does is the guard, which copies a float through a uniform buffer
  and computes nothing. The suite runs last because it is the only one where two correct
  machines are allowed to disagree.

Nothing is ever put on a screen. The target is offscreen and the program reads it back itself,
which keeps the compositor, the swapchain and vertical sync out of a test that is about none of
them, and keeps the first problem on a new machine from being "did the screen work".

## The roadmap

The suites are ordered by how much one case tells you when two machines disagree, and each
one leans on the ones above it. That order is the whole point: when a run comes back with
forty failures, the first suite that failed is the only one worth reading, because everything
below it was measured with a broken instrument.

This is the order they run in, and the order they were built in. Anything added later goes where
its dependencies put it, not at the end.

| suite | tier | what a failure points at |
|---|---|---|
| selfcheck | none, no hardware | the comparators themselves. Nothing below can be believed. |
| support | none, no hardware | what is written and drawn around a verdict, never a verdict. |
| reference | none, no hardware | the expected answers: the coverage rule, the blend arithmetic, the texel address. The judge, not the machine. |
| clear | exact | the render target's memory layout and the path that copies it back. Everything below draws into that same target. |
| raster | position for edges, exact for interiors | vertex fetch, the clip space to screen space transform, and the coverage rule. In an emulator: the viewport registers, the y direction, the half pixel offset, and the fill rule the host rasteriser is asked for. |
| intmath | exact | the shader translator, bit for bit, and the delivery of the fragment's own coordinate. In an emulator: one wrongly decoded instruction, or an integer render target mapped to a normalised host format. |
| blend | exact | the fixed function blend unit: the equation and factor registers, and the rounding on the way back to eight bits. |
| depth | exact | the depth test: the depth target's format and layout, the depth range set up by the viewport, and the compare operation. |
| texture | exact | the texture and sampler descriptors: the wrap mode and filter fields, and the block linear layout a sampled image is stored in. |
| coherence | exact | a cache between two uses of the same memory. In an emulator: the texture cache not marking an image the graphics unit wrote as modified, or missing a processor write to one (`texture_cache/texture_cache.h`, `MarkModification`, `WriteMemory`); the buffer cache serving a uniform buffer the processor rewrote, or dropping an update pushed inside the list (`buffer_cache/buffer_cache.h`, `Maxwell3D::ProcessCBData`); the copy engine's writes not reaching the images it overlaps (`engines/maxwell_dma.cpp`), or a render target not flushed before the copy engine reads it; or two formats over one memory joined as a conversion rather than a view (`texture_cache/texture_cache.h`, `JoinImages`). The region two pixels that still hold region one's contents say which. |
| fmath | units in the last place | the float side of the shader translator, and the transcendental unit behind it. This is the only suite where two correct machines are allowed to differ, which is why it is last. |

Two rules hold across all of them. Every case computes what it expects on the processor and
checks every pixel, so there is no stored image anywhere and no machine's answer is the
reference for another's. And every rule a case judges by, wherever that rule is more than one
line of arithmetic, is implemented once in `source/` and pinned by the `reference` suite, which
runs with no graphics device at all: `make -f host.mk host-run` proves the judge before a
console or an emulator is involved.

Two things in the table are not suites and are not in it: the picture a failing case leaves on
the memory card, and the detailed mode that shows it. Both are support. Neither decides
anything, neither is compared to anything, and no run's answer changes if either is turned off.
They are in the `support` suite's row only because that suite checks them, the way the
`reference` row is about the judge rather than about a machine. The two sections below say what
they are for and where the line between them and a verdict is drawn.

## What it reports

One JSON object per line, to two channels at once, because the two machines read different ones:

- the debug string service, which an emulator logs with no setup and hardware ignores unless a
  debugger is attached;
- a file on the memory card, which is what a console run leaves behind.

A case line carries its tier next to the result:

```json
{"type":"case","suite":"clear","name":"clear to opaque red","tier":"exact","result":"pass","detail":""}
```

The tier is on the line and not only in the source because a pass means nothing on its own: two
machines agreeing is worth something only if both were held to the same tolerance, and a reader
diffing two reports has no way to know which one a case used otherwise. The values are `exact`,
`ulp` and `position`, and one more that is not a tier at all: `none`.

`none` is for any line, in any suite, that reports no comparison of something the hardware
produced. That is the whole rule, and it is worth stating as a rule rather than as a list,
because the list is longer than it looks: the two suites that judge this program's own
arithmetic, every case that reports whether some precondition held rather than what came out of
it, from the device coming up to a shader loading to a texture reaching the graphics unit, and
any drawing case that was stopped before it could compare anything, which still reports the
reason in its detail. The tier describes the comparison the line carries, not the ambition of the
case that wrote it. Calling any of those `exact` would read, in a diff, as a passing claim about
a machine that was never asked anything.

A run that dies halfway still leaves everything up to its last finished case, and the final line
carries the counts so a runner does not have to parse the rest.

## What a failing case leaves behind

A verdict says how many pixels were wrong and what the first one held. It cannot say that the
wrong ones were the right shape in the wrong place, or one block out of sixteen, or every other
column. So a drawing case that fails writes what it saw next to the report, and names the files
on its own line, which is one line and is only wrapped here to fit:

```json
{"type":"case","suite":"intmath","name":"bitwise and","tier":"exact","result":"fail",
 "detail":"4095 of 4096 pixels wrong, first at (1,0) is 0x00000000, expected 0x00106000",
 "image":"images/intmath/bitwise-and.pam","expected":"images/intmath/bitwise-and-expected.pam",
 "diff":"images/intmath/bitwise-and-diff.pam","raw":"images/intmath/bitwise-and.bin"}
```

Each field is a path relative to the directory the report is in, which is the one holding
`report.jsonl`, and each appears only when that file was written, so every line a run produced
before this existed still reads the same. The files land under `images/<suite>/`, named after
the case with the letters lower cased and every run of characters that is not a letter or a
digit turned into a single dash: "the fragment coordinate's x is the pixel centre, bit for bit"
becomes `the-fragment-coordinate-s-x-is-the-pixel-centre-bit-for-bit.pam`, one dash for the
apostrophe and one for the comma and the space together. Two case names that spell one file
name get a file each: the second is numbered, `-2` at the end of the name and before the
extension, giving up characters off the end of the name if that is what it takes to make room
for the number. A name is held together with the two the pictures beside it are spelled from,
so a case called "bitwise and expected" cannot land on the expectation of one called "bitwise
and". If the numbers run out, after ninety eight of them, the case is left with no picture at
all and its line simply carries none of the four fields, because one failure quietly
overwriting another's evidence is worse than a missing picture and far worse than a name
nobody likes. The same goes for the directory the whole lot lives in: one too long to hold
whole is refused outright rather than cut short into a place nobody named.

This is not a stored reference image and it must never become one. The README's first section
says why a hash of a frame breaks between two machines that are both right, and that is still
true of these files. Nothing compares them to anything, no case reads one, and none of them can
change an answer: the readback is judged, and then the very bytes the judge looked at are
written. If every image were deleted the run would report exactly what it reported before.

**The formats.** All three pictures are binary PAM, which is the one netpbm format that carries
four channels, so the alpha of a colour target is in the picture rather than in a second file
beside it that can go missing. `magick`, `identify` and the netpbm tools read them as they are;
anything that wants a PNG is one `magick a.pam a.png` away.

| file | what it is |
|---|---|
| `<case>.pam` | what the machine drew, four channels, MAXVAL 255, labelled `drawn` on screen |
| `<case>-expected.pam` | what the processor worked out for that same pixel |
| `<case>-diff.pam` | three channels: the judge's verdict per pixel, labelled `judged` on screen |
| `<case>.bin` | the target's bytes as they were read back, for the formats an image cannot hold |

The four bytes of a pixel go into the image in the order the readback handed them over, and
that one rule covers every format the suite uses. For a colour target that is red, green, blue
and alpha and the picture is the picture. For a target holding 32 bit integers or floats it is
the little end first, which makes a picture of bit patterns rather than of magnitudes: equal
values look equal and one changed bit changes a colour, which is what a reader of an integer
target wants, while a float range has no honest mapping onto a byte and inventing one would
make the picture a judgement. Those formats also get their bytes verbatim in a `.bin`, so
`cmp` between two machines has something exact to work on.

**The diff is the judge's verdict and not a comparison of the two images**, and that distinction
is the whole reason this is safe. Dark grey means the judge accepted the pixel, red that it
refused it, blue that it declined to judge it at all, which is the band an edge runs through in
the rasterisation cases. A fourth colour, magenta, means no compare loop ever looked at that
pixel. It is there because the alternative was to start every pixel at one of the other three,
where a loop that skipped one would be indistinguishable from one that meant to leave it alone;
a run that shows magenta has a bug in this program and not in the machine. A pixel inside a
budget of units in the last place differs in its bytes and is not a difference here. An image
comparison would paint the entire float suite red on a machine that had done nothing wrong,
which is precisely the failure the suite exists not to report.

By default only a failing case writes anything, because a green run would otherwise leave three
files for every drawing case and nobody asked for them. A run that wants them all says so in
the `mode` file described in the next section. The `support` suite is the one exception and
only while it runs: it writes a handful of eight by four fixtures into `support-check/` beside
the report, parses them back, and then takes them away again, which is a case of its own. A run
that leaves that directory behind has failed that case and says so.

## Looking at it on the console

One binary, two ways to run it.

**Automatic** is what it always did and is still the default: every suite runs, both report
channels get every line, the program exits, about three seconds.

**Detailed** does all of that first, unchanged and to the last byte, and then puts the cases on
the screen one at a time: the three pictures above, side by side and labelled `drawn`,
`expected` and `judged`, each 64 by 64 target scaled up with nearest neighbour so it is
legible, with the suite, the case, the tier, the verdict and the detail as text around them,
and a key naming each of the four colours `judged` is made of. It is for the question a verdict
cannot answer, which is whether a failure looks like a rendering fault or like the judge being
wrong.

What it holds is every case that compared pixels, pass or fail, which on a full run is 96 of the
257 and is the number the heading counts in. The other 161 compared nothing the graphics unit
produced and have no picture to show. A case that failed before it read a target back has no
picture either, so it is not in the list; when that happens the screen says how many of the
run's failures are missing from it rather than letting the count quietly disagree. It holds them
in memory rather than reading back what it wrote, so a green run and a run that wrote nothing
are both worth looking at, and the `images` word changes what is on the memory card and not what
is on the screen.

The order is the safeguard and it is not a detail of the implementation. Every suite runs, every
line is emitted, the summary is counted and the report is closed, and only then does anything
reach a screen; the viewer refuses to start while a report is open. So the two modes leave the
same file, byte for byte and digest for digest, and the same images beside it. The screen is a
second opinion for a person, never an input to one.

Choose it in either of two ways, because a person and a script want different ones:

- put the word `detailed` in `sdmc:/edenprobe/mode`. The word `images` in the same file, on its
  own or beside it, also writes the pictures for the passing cases;
- or hold **X** as the program starts. The pad is asked once it turns up, which on a machine
  with a controller already attached is a frame or two; the wait is twelve frames at the very
  most, and it is skipped altogether when the file has already asked for the detailed mode. The
  report is opened before any of this, so a machine that cannot read the file or reach the pad
  still leaves a report saying how far it got.

On the screen: **A** or right for the next case, **B** or left for the previous one, **down**
and **up** for the next and previous failure, **X** to cycle between the three side by side and
each one on its own, and **plus** to quit. The frame is composed on the processor and only when
something moved, so a still screen costs an emulated machine nothing.

## Building

For the console, with the toolchain installed:

```sh
export DEVKITPRO=$HOME/devkitpro
make
```

That produces `edenprobe.nro`. The same file runs on hardware and in an emulator, unchanged: that
is the point, since the two reports are only comparable if the binary is identical.

There is also a host build, which compiles the parts that touch no console and runs them on the
machine you are sitting at:

```sh
make -f host.mk host-run
```

It covers the suite's judgement of itself: that two neighbouring floats measure one unit apart,
that a zero budget rejects them, that a NaN is never equal to anything including itself. If those
fail, nothing else the suite says can be believed, and you find out in a second instead of after
a boot. It also covers what is written and drawn around a verdict, which has no console in it
either: the picture writer against a fixture it then parses back, and the detailed mode's layout
and navigation against a frame composed in ordinary memory.

Commit messages have a convention too: `edenprobe: what changed`, one to three lines of intent,
one Signed-off-by that is also the author and the committer, ASCII, no merges. `git config
core.hooksPath hooks` once per clone turns the check on; `sh hooks/test-commit-msg.sh` shows
what it accepts and `--history <range>` runs it over existing commits.

## Running it under an emulator

```sh
make run
```

`EMULATOR` points at the binary to test, and is a variable on the command line:
`make run EMULATOR=/path/to/emulator`. With nothing given it is the eden-cli build kept in
`~/WorkDir/accord/bin`.

The run leaves `report.jsonl`, and `images/` beside it, in `sdmc:/edenprobe`. On the console
that is the memory card. On a desktop the emulator keeps its memory card in an ordinary
directory under `$XDG_DATA_HOME`, and its settings and caches under `$XDG_CONFIG_HOME` and
`$XDG_CACHE_HOME`. `make run` points all three at `build/xdg/` inside this tree before it starts
the emulator, so the report is at `build/xdg/data/eden/sdmc/edenprobe/report.jsonl`, the last
line of the target prints that path, and `make clean` takes the lot away. That is not tidiness.
An emulator's frontend saves its settings when it exits, so a run pointed at your own
directories reads your settings and may rewrite them; one pointed here starts from the
emulator's defaults every time, which also makes two runs comparable. The target deletes the
previous report, its pictures and the support suite's scratch directory first, so a run that
never started cannot leave an old report looking like its own, and a green run cannot sit beside
the pictures of an earlier failing one. An emulator path that is missing or not executable, or a
run that leaves no report, stops the target with a line saying which, and a non-zero exit.
Any emulator with a different layout puts the same names under whatever it calls its memory
card.

The run needs a real rendering backend. An emulator set to a null or headless backend will still
report the selfcheck cases and still open a graphics device, and will then fail every drawing case
with every pixel reading zero, because a backend that draws nothing is behaving exactly as asked.
That is a property of the run, not a defect found, and a report is only worth diffing against
another machine when both were rendering.

A run like that is still worth doing, because it proves the things a null backend does not stand
in for. The graphics library runs on the emulated processor, not on the host, so everything it
checks about the commands it is given is checked for real: a shader module it will not load, a
copy it will not make, a rectangle it will not clear all end the program rather than fail a case.
A headless run that reports every case and reaches its summary has established that none of that
happened, which is most of what can go wrong before a pixel is involved.

## Licence

MIT, and it stays that way. The program links only the console's C runtime and the graphics
library, both permissively licensed. No emulator code is used or needed, so nothing here is
encumbered by the licence of any emulator it happens to be tested against.
