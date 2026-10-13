# What it checks today

Forty suites, in the order they run: the judge, then the processor, then the graphics family, then
the fs family, then the audio family when a word asks for it, then the net family. A run is 809
cases: 741 for the judge, the processor and the graphics family, 15 for the fs family, 53 for the
net family; the audio family adds six when named or with `listen`, the mode word `stress` adds one,
and `fatal` runs the cases the may-crash table names, which are otherwise skipped. The order, and
what a failure in each suite points at, is in [The roadmap](../Roadmap.md). A case that ends the
program costs that case and not the rest of the run: see [When the program ends in the
middle](../Crashes.md).

- **[The judge](./Judge.md)** - selfcheck, support and reference, no hardware involved
- **[The processor](./Cpu.md)** - cpu
- **[Graphics](./Graphics.md)** - the twenty eight drawing suites, clear to dma
- **[fs](./Fs.md)** - reads through the file system service
- **[Audio](./Audio.md)** - the audio renderer, and the listening sequence
- **[Net](./Net.md)** - nifm, dns, bsd, ssl, acc and friend, the net file and the fixture

No case draws to a screen. The targets are offscreen and the program reads them back itself, which
keeps the compositor, the swapchain and vertical sync out of a test about none of them, and keeps
the first problem on a new machine from being "did the screen work". The one thing on a screen while
the suites run is the running screen of a run that has one (see [Looking at it on the
console](../Console.md)), composed by the processor between cases, never inside a timed interval; a
quick run from the mode file draws nothing at all.
