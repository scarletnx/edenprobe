# edenprobe

A test program that runs on the console and inside an emulator of it, decides for itself whether the
graphics came out right and whether the online services answered as their protocol says, and writes
a report you can diff between the two.

It is not a benchmark and not a demo. Every case computes the answer it expects and checks the
answer it got; nothing needs a person to look at the screen and judge.

It was gfxprobe until 23 September 2026. The name changed when suite families beyond the graphics
unit arrived: one binary with one judge is the reason to keep them together.

## Why not compare screenshots

Because two correct implementations are allowed to disagree, and the specification says so:
division, the logarithm, the power of two and the sine each have a budget rather than one answer,
and sub-pixel precision is a limit each implementation reports. A hash of a shaded frame breaks
between two machines that are both right. So every case works out on the processor what it
expects and decides inside the program, on both machines, by the same code, at a tolerance it
states: one of [the four tiers](docs/Design.md#the-four-tiers).

## Building and running

For the console, with devkitPro in `/opt/devkitpro`:

```sh
export DEVKITPRO=/opt/devkitpro
make
```

That produces `edenprobe.nro`, the one file that runs on a console and under an emulator. The
judge and the tools' tests run on the machine you are sitting at, with no console:

```sh
make -f host.mk host-run
```

Under an emulator, `make run EMULATOR=/path/to/emulator` runs it in the quick mode, starts the
emulator again after a crash, and its last line names the report. More in
[Building](docs/Build.md), [Running it under an emulator](docs/Emulator.md) and
[Running it on Android](docs/Android.md).

Without building it: the newest nightly is always at
`https://github.com/scarletnx/edenprobe/releases/download/nightly/edenprobe.nro`, and each version
has a release; [Releases](docs/Releases.md) says how the CI makes them.

## Documentation

What each suite checks, the report, the screens, the mode file, the times and what happens when
the program ends in the middle: [docs/](docs/README.md).

## Credits

edenprobe follows switch-examples (public domain) and builds on libnx (ISC) and deko3d (zlib), and
carries hashes of what astcenc (Apache 2.0) decodes; what comes from each is in
[Credits](docs/Credits.md).

## Licence

GPL-3.0-or-later; the full text is in `LICENSE`. That lets code from other GPL-3 projects come
in, and everything the program links or follows from elsewhere is under a licence compatible
with it.
