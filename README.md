# gfxprobe

A test program that runs on the console and inside an emulator of it, decides for itself whether
the graphics came out right, and writes a report you can diff between the two.

It is not a benchmark and not a demo. Every case computes the answer it expects and checks the
answer it got. Nothing here needs a person to look at the screen and judge.

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

Nine suites, in the order they run, because each leans on the ones before it. A run is 170
cases.

- **selfcheck**, sixteen cases, no hardware involved. That two neighbouring floats measure one
  unit apart, that a zero budget rejects them, that an absolute budget is the same on both sides
  of the expected value, that a NaN is never equal to anything including itself, not even against
  a budget of every value there is, and that a budget of three units in the last place reaches
  six representable values below a power of two and three above it. If one of these fails,
  nothing else the suite says can be believed.
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
- **reference**, fifty three cases, no hardware involved either. The other half of the judge: where
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
| reference | none, no hardware | the expected answers: the coverage rule, the blend arithmetic, the texel address. The judge, not the machine. |
| clear | exact | the render target's memory layout and the path that copies it back. Everything below draws into that same target. |
| raster | position for edges, exact for interiors | vertex fetch, the clip space to screen space transform, and the coverage rule. In an emulator: the viewport registers, the y direction, the half pixel offset, and the fill rule the host rasteriser is asked for. |
| intmath | exact | the shader translator, bit for bit, and the delivery of the fragment's own coordinate. In an emulator: one wrongly decoded instruction, or an integer render target mapped to a normalised host format. |
| blend | exact | the fixed function blend unit: the equation and factor registers, and the rounding on the way back to eight bits. |
| depth | exact | the depth test: the depth target's format and layout, the depth range set up by the viewport, and the compare operation. |
| texture | exact | the texture and sampler descriptors: the wrap mode and filter fields, and the block linear layout a sampled image is stored in. |
| fmath | units in the last place | the float side of the shader translator, and the transcendental unit behind it. This is the only suite where two correct machines are allowed to differ, which is why it is last. |

Two rules hold across all of them. Every case computes what it expects on the processor and
checks every pixel, so there is no stored image anywhere and no machine's answer is the
reference for another's. And every rule a case judges by, wherever that rule is more than one
line of arithmetic, is implemented once in `source/` and pinned by the `reference` suite, which
runs with no graphics device at all: `make -f host.mk host-run` proves the judge before a
console or an emulator is involved.

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

## Building

For the console, with the toolchain installed:

```sh
export DEVKITPRO=$HOME/devkitpro
make
```

That produces `gfxprobe.nro`. The same file runs on hardware and in an emulator, unchanged: that
is the point, since the two reports are only comparable if the binary is identical.

There is also a host build, which compiles the parts that touch no console and runs them on the
machine you are sitting at:

```sh
make -f host.mk host-run
```

It covers the suite's judgement of itself: that two neighbouring floats measure one unit apart,
that a zero budget rejects them, that a NaN is never equal to anything including itself. If those
fail, nothing else the suite says can be believed, and you find out in a second instead of after
a boot.

## Running it under an emulator

```sh
make run
```

`EMULATOR` points at the binary to test. The report lands on the emulator's virtual memory card,
which on a desktop is an ordinary directory.

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
