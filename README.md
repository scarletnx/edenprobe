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

## What it reports

One JSON object per line, to two channels at once, because the two machines read different ones:

- the debug string service, which an emulator logs with no setup and hardware ignores unless a
  debugger is attached;
- a file on the memory card, which is what a console run leaves behind.

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

## Licence

GPL-3.0-or-later; the full text is in `LICENSE`. That lets code from other GPL-3 projects come
in, and everything the program links or follows from elsewhere is under a licence compatible
with it.
