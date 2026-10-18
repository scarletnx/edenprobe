# edenprobe documentation

What the program checks and how it judges, how to build and run it, and how to read what a run
leaves. The [README](../README.md) at the root says what edenprobe is.

- **[Design](./Design.md)** - why no screenshots are compared, and the four tiers
- **[The roadmap](./Roadmap.md)** - the order of the suites, and what a failure in each points at
- **[What it checks today](./families/README.md)**
  - **[The judge](./families/Judge.md)** - selfcheck, support and reference
  - **[The processor](./families/Cpu.md)** - the cpu suite
  - **[Pad](./families/Pad.md)** - a controller in a person's hands, button by button
  - **[The touch suite](./families/Touch.md)** - targets, two fingers, a drag; what Eden makes of a
    mouse
  - **[Graphics](./families/Graphics.md)** - the drawing suites, clear to blit2d
  - **[The blit2d suite](./families/Blit2d.md)** - the 2D engine's scaled copy, read raw and by
    copy, and Q1 and Q2
  - **[fs](./families/Fs.md)** - reads through the file system service
  - **[Kernel](./families/Kernel.md)** - the scheduler: six cases with a watchdog, Q1 to Q3
  - **[Display](./families/Display.md)** - transaction 17, the buffer history, Q1
  - **[Video](./families/Video.md)** - the VIC: the job, the nineteen cases, the open questions
  - **[The nvdec suite](./families/Nvdec.md)** - VP9 through the decode engine: six libvpx vectors
    by MD5, and Q1 to Q5
  - **[Audio](./families/Audio.md)** - the audio renderer, and the listening sequence
  - **[Service](./families/Service.md)** - hwopus, am, fsp, account, ldn, set, csrng, apm, nfp,
    aoc, time, pl, profile, am-state, am-self, am-functions, auddev, fsdir, am-audio, am-channel, am-storage, mii, am-mode and bsd-local: the commands a game sends, held to reden's answers
  - **[Net](./families/Net.md)** - nifm to friend, the net file and the fixture

## Running it

- **[Building](./Build.md)** - the NRO, the host build, the frames and the commit hook
- **[Releases](./Releases.md)** - the nightly and the versions, and how the CI publishes them
- **[Running it under an emulator](./Emulator.md)** - `make run`
- **[Running it on Android](./Android.md)**
- **[Looking at it on the console](./Console.md)** - the two modes, the screens and the buttons
- **[The mode file](./ModeFile.md)** - the words that choose what runs
- **[The validation layer's view](./Validation.md)** - `make validate`
- **[Making the driver look older](./OlderDriver.md)** - the masquerade layer

## Reading a run

- **[What it reports](./Report.md)** - the report's lines, and where a run leaves them
- **[Time per case](./TimePerCase.md)** - what a number measures, and comparing two builds
- **[What a failing case leaves behind](./FailurePictures.md)** - the pictures and the diff
- **[When the program ends in the middle](./Crashes.md)** - the journal, the crash list, the tables
- **[Versions](./Versions.md)** - the version, the build and the cover

## Credits

- **[Credits](./Credits.md)** - what comes from switch-examples, libnx, deko3d, astcenc, libvpx's
  test vectors, FFmpeg, nvtegra and a public domain MD5
