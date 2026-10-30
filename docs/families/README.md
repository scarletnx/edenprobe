# What it checks today

Eighty five suites, in the order they run: the judge, then the processor, then the pad family when a
word asks for it, then the graphics family, then the fs family, then the kernel, display, video,
audio and service families when a word asks for them, then the net family, then the game family when
its word asks for it. A run is 1010 cases: 942 for the judge, the processor and the graphics family,
15 for the fs family, 53 for the net family; the pad family adds thirty five when named, the kernel
family seven, the display family four, the video family twenty seven, the audio family twelve when
named or with `listen`, the service family one hundred and nineteen, the game family thirteen (six
with `notime`), the mode word `stress` adds one, and `fatal` runs the cases the may-crash table
names, which are otherwise skipped. The order, and what a failure in each suite points at, is in
[The roadmap](../Roadmap.md). A case that ends the program costs that case and not the rest of the
run: see [When the program ends in the middle](../Crashes.md).

- **[The judge](./Judge.md)** - selfcheck, support and reference, no hardware involved
- **[The processor](./Cpu.md)** - cpu
- **[Pad](./Pad.md)** - what a game receives from a controller in a person's hands, and from the
  touch screen ([the touch suite](./Touch.md))
- **[Graphics](./Graphics.md)** - the thirty drawing suites, clear to blit2d ([the blit2d
  suite](./Blit2d.md) has a page of its own)
- **[fs](./Fs.md)** - reads through the file system service
- **[Kernel](./Kernel.md)** - the scheduler: priority inheritance, preemption and migration
- **[Display](./Display.md)** - the buffer producer's history, on a layer of its own
- **[Video](./Video.md)** - the video image compositor, on input the processor wrote, and the
  decoder, on VP9 jobs built from libvpx's test vectors ([the nvdec suite](./Nvdec.md))
- **[Audio](./Audio.md)** - the audio renderer, and the listening sequence
- **[Service](./Service.md)** - commands a game sends to the system's services, held to reden's
  answers: hwopus, am, fsp, account, ldn, set, csrng, apm, nfp, aoc, time, pl, profile, am-state, am-self, am-functions, auddev, fsdir, am-audio, am-channel, am-storage, mii, am-mode, bsd-local, ipc, hid, time-snap, vi, renderer, ngc, am-lock, set-sys, time-zone, time-perm, audout, fs-file, acc-app
- **[Net](./Net.md)** - nifm, dns, bsd, ssl, acc and friend, the net file and the fixture
- **[Game](./Game.md)** - one scene that runs everything at once, as a game does: four threads on
  three cores drawing, playing, reading and asking every frame, each image, the sound and each read
  judged, every frame presented and timed

No case draws to a screen but the game family's. The targets are offscreen and the program reads
them back itself, which keeps the compositor, the swapchain and vertical sync out of a test about
none of them, and keeps the first problem on a new machine from being "did the screen work"; the
game scene is about all of them at once, presents every frame through a swapchain, and judges the
offscreen copy it makes of each. The one thing on a screen while
the suites run is the running screen of a run that has one (see [Looking at it on the
console](../Console.md)), composed by the processor between cases, never inside a timed interval; a
quick run from the mode file draws nothing at all. The listening sequence and the pad family put a
screen of their own up for a person, and neither screen is an input to a verdict.
