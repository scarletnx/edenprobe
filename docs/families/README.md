# What it checks

The families run in this order: the judge, the processor, pad (when a word names it), graphics, nvn (when its word
names it), fs, then kernel,
display, video, audio and service (each when a word names it), net, and game (when its word names it); libc, on the
console, when a word names it. Android's frontend has the judge, libc, graphics, net, jni and framework
([On Android](../Android.md)). The
[mode file](../ModeFile.md) says which words name what, and [the roadmap](../Roadmap.md) what a failure in each suite
points at. A case that ends the program costs that case, not the rest of the run
([When the program ends in the middle](../Crashes.md)).

- **[The judge](./Judge.md)** - selfcheck, support and reference, no hardware involved
- **[The processor](./Cpu.md)** - cpu
- **[Pad](./Pad.md)** - a controller in a person's hands, and the touch screen ([touch](./Touch.md))
- **[Graphics](./Graphics.md)** - the thirty four drawing suites, clear to blit2d ([blit2d](./Blit2d.md)), and tex8k
- **[nvn](./Nvn.md)** - the clear suite's question through the program's NVN, which ScarletNX takes as a game's
- **[fs](./Fs.md)** - reads through the file system service
- **[Kernel](./Kernel.md)** - the scheduler: priority inheritance, preemption and migration
- **[Display](./Display.md)** - the buffer producer's history, on a layer of its own
- **[Video](./Video.md)** - the video image compositor, and the decoder on VP9 jobs ([nvdec](./Nvdec.md))
- **[Audio](./Audio.md)** - the audio renderer, and the listening sequence
- **[Service](./Service.md)** - commands a game sends to the system's services, held to reden's answers
- **[Net](./Net.md)** - nifm, dns, bsd, ssl, acc and friend, the net file and the fixture
- **[Game](./Game.md)** - one scene that runs everything at once, as a game does
- **[libc](./Libc.md)** - the C library and what it asks of the system, the same cases on every frontend

Every drawing suite draws offscreen and reads its target back, so no case depends on the compositor, the swapchain
or vertical sync. The game scene is the one exception: it presents every frame and judges the offscreen copy it
makes of each. A run with a screen shows the running screen between cases, never inside a timed interval; a quick
run from the mode file draws nothing. The pad family and the listening sequence put up screens for a person, and
neither is an input to a verdict.
