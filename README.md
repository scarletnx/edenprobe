# gfxprobe

A test program that runs on the console and inside an emulator of it, decides for itself whether
the graphics came out right, and writes a report you can diff between the two.

It is not a benchmark and not a demo. Every case computes the answer it expects and checks the
answer it got. Nothing here needs a person to look at the screen and judge.

## Why not compare screenshots

Because two correct implementations are allowed to disagree, and the specification says so. The
graphics API requires division to land within 2.5 units in the last place, the exponential and
logarithm within 3, and sine and cosine only within an absolute error of 2^-11. Sub-pixel
rasterisation precision is a limit each implementation reports, not a constant. A hash of a
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

Two suites, in the order they run, because the second leans on the first.

- **selfcheck**, seven cases, no hardware involved. That two neighbouring floats measure one unit
  apart, that a zero budget rejects them, that a NaN is never equal to anything including itself.
  If one of these fails, nothing else the suite says can be believed.
- **clear**, five cases, on the graphics unit. A 64 by 64 RGBA8 target is cleared to a colour, all
  4096 of its pixels are copied back and compared, and that happens for three colours in a row, so
  a readback handing over a stale buffer cannot pass by accident. The fourth clear covers the
  target in one colour and then clears a rectangle inside it in another, which is what makes the
  answer depend on where a pixel is: a target that is all one colour reads back the same whether
  or not the copy walked its blocks in the right order, so the three before it cannot catch a
  transposed, flipped or shuffled readback and this one can. Exact tier throughout: the format is
  integer and every clear value is a constant, so each channel has exactly one right byte.

Nothing is ever put on a screen. The target is offscreen and the program reads it back itself,
which keeps the compositor, the swapchain and vertical sync out of a test that is about none of
them, and keeps the first problem on a new machine from being "did the screen work".

## What it reports

One JSON object per line, to two channels at once, because the two machines read different ones:

- the debug string service, which an emulator logs with no setup and hardware ignores unless a
  debugger is attached;
- a file on the memory card, which is what a console run leaves behind.

A case line carries the tier it claimed, next to the result:

```json
{"type":"case","suite":"clear","name":"clear to opaque red","tier":"exact","result":"pass","detail":""}
```

The tier is on the line and not only in the source because a pass means nothing on its own: two
machines agreeing is worth something only if both were held to the same tolerance, and a reader
diffing two reports has no way to know which one a case used otherwise. The values are `exact`,
`ulp` and `position`, and one more that is not a tier at all: `none`, for a case that compared
nothing the hardware produced. Every selfcheck case reports `none`, because its subject is this
program's own arithmetic; calling those `exact` would read, in a diff, as seven passing claims
about a machine that was never asked anything.

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

## Licence

GPL-3.0-or-later; the full text is in `LICENSE`. That lets code from other GPL-3 projects come
in, and everything the program links or follows from elsewhere is under a licence compatible
with it.
