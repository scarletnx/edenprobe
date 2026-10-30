# edenprobe documentation

New here? Start with [Build](./Build.md), then [the mode file](./ModeFile.md) and [the report](./Report.md).

- **[Design](./Design.md)** - the four tiers a case judges by
- **[Roadmap](./Roadmap.md)** - the suites in the order they run, and what a failure in each points at
- Running it:
  - **[Build](./Build.md)** - the NRO, the host build and the commit hook
  - **[Releases](./Releases.md)** - the nightly, the versions and the CI
  - **[Under an emulator](./Emulator.md)** - `make run`
  - **[On Android](./Android.md)**
  - **[On the console](./Console.md)** - the two modes, the screens and the buttons
  - **[The mode file](./ModeFile.md)** - the words that choose what runs
  - **[The validation layer](./Validation.md)** - `make validate`
  - **[An older driver](./OlderDriver.md)** - the masquerade layer
- Reading a run:
  - **[The report](./Report.md)** - its lines, and where a run leaves them
  - **[Time per case](./TimePerCase.md)** - what a case's time measures, and comparing two builds
  - **[Baselines](./Baseline.md)** - a machine's own times, and a run held to them
  - **[Failure pictures](./FailurePictures.md)** - the pictures a failing case leaves
  - **[When the program ends in the middle](./Crashes.md)** - the journal, the crash list, the tables
  - **[Versions](./Versions.md)** - the version, the build and the cover
- **[What it checks](./families/README.md)**:
  - **[The judge](./families/Judge.md)** - selfcheck, support and reference
  - **[Processor](./families/Cpu.md)**, **[pad](./families/Pad.md)** and **[touch](./families/Touch.md)**
  - **[Graphics](./families/Graphics.md)** - the drawing suites, clear to blit2d, and
    **[blit2d](./families/Blit2d.md)**
  - **[fs](./families/Fs.md)**, **[kernel](./families/Kernel.md)** and **[display](./families/Display.md)**
  - **[Video](./families/Video.md)** - the VIC, and **[nvdec](./families/Nvdec.md)**, VP9 through the decode engine
  - **[Audio](./families/Audio.md)**, **[service](./families/Service.md)** and **[net](./families/Net.md)**
  - **[Game](./families/Game.md)** - one scene that runs everything at once, as a game does
- **[Credits](./Credits.md)**
