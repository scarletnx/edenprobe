# The mode file

`sdmc:/edenprobe/mode`: words in any order and any case, anything that is not a letter between them.

| word | what it does |
|---|---|
| no file, or no `quick` in it | detailed |
| `quick` | the automatic run: every suite, the report, exit |
| `detailed` | detailed even beside `quick`, the same as holding X |
| `images` | a picture for every drawing case, passing ones too, on either mode |
| `notime` | no timing passes: each drawing suite runs once, and no line carries a time; see [Time per case](TimePerCase.md) |
| `stress` | the heavy variants too: the stream suite's 256 MiB list. An emulator that runs out of memory on one loses the rest of the run |
| `fatal` | nothing: every case runs, and one that ends the program is skipped on the next launch ([When the program ends in the middle](Crashes.md)). Still read, so a file that has it keeps working, and the mode line says, once a run, `the mode word fatal changes nothing now: every case runs, and one that ends the program is skipped on the next launch` |
| `hang` | the cases the hang table names, which never return on a machine that gets them wrong: the bsd receive-timeout case. `retry` does not run them, and the start screen has no row for it |
| `unban` | the cases the ban list bans on this run's machine run anyway, to see whether they still end it; the start screen has no row for it ([The ban list](Crashes.md#the-ban-list)) |
| `retry` | the cases on the crash list run again; one that runs to its end leaves the list (see [When the program ends in the middle](Crashes.md)). A case the hang table names stays skipped until `hang` asks too |
| `nojournal` | no journal between cases, so a crash costs the rest of the run; for measuring what the journal costs |
| `noprogress` | no running screen while the suites run; for a machine whose window misbehaves while the suites draw |
| `all` | every family the build has but pad, which waits for a person, and nvn: one word for a run that needs nobody. `pad` or `nvn` beside it adds that family; the mode line lists the families that run |
| `cpu` | the processor family. It runs in every mode, so naming it alone leaves the graphics, fs and net families out: a few seconds, nothing drawn |
| `graphics` | the graphics family: the thirty four drawing suites, clear to blit2d, and tex8k; selfcheck, support, reference and cpu always run, whatever family is named |
| `nvn` | the nvn family: the clear suite's question asked through the program's NVN, a layer on deko3d that a runtime running a game's NVN itself (ScarletNX) takes as a game's driver; only when named, with no row on the start screen; its window case runs only in a quick run from the mode file, since a run begun on the start screen keeps the program's screen up to its end, `noprogress` or not, and the start screen says so; see [nvn](families/Nvn.md) |
| `fs` | the fs family: reads from the memory card, timed and checked; it writes a 64 MiB file and 256 small ones under `sdmc:/edenprobe/fs` the first time |
| `audio` | the audio family: the renderer's resampler, circular buffer sink and limiter, recorded through its own effect and sink; only when named, since it makes sound (most at a gain of nought) |
| `listen` | the listening sequence before the audio family's cases, and the audio family with it: about 40 seconds of sound meant to be heard; see [The audio family](families/Audio.md) |
| `pad` | the pad family: a person presses each button the screen names, the sticks and a vibration, then taps targets on the touch screen; only when named; see [pad](families/Pad.md) and [touch](families/Touch.md) |
| `kernel` | the kernel family: the scheduler's priority inheritance, preemption and migration; only when named, with no row on the start screen; see [kernel](families/Kernel.md) |
| `display` | the display family: the buffer history of a layer of its own (transaction 17); only when named, with no row on the start screen; see [display](families/Display.md) |
| `video` | the video family: the video image compositor, a job per case, and the decoder on VP9 jobs from libvpx's test vectors, a job per frame; only when named, with no row on the start screen; see [video](families/Video.md) and [nvdec](families/Nvdec.md) |
| `service` | the service family: commands a game sends to the system's services, each answer held to reden's; only when named, with no row on the start screen; see [service](families/Service.md) |
| `net` | the net family: nifm, dns, bsd, ssl, acc and friend, against what `sdmc:/edenprobe/net` names |
| `libc` | the libc family: the C library and what it asks of the system, the same cases on every frontend; on the console only when named, on Android in every run that names no family; see [libc](families/Libc.md) |
| `jni` | Android's jni family: native code and the Java runtime calling each other; see [On Android](Android.md) |
| `framework` | Android's framework family: the framework's Java API and its resources; see [On Android](Android.md) |
| `game` | the game family: one scene that draws, plays a silent tone, reads the card, calls services and computes on four threads over three cores, as a game does, and presents and times every frame; only when named, with no row on the start screen; it writes the fs family's files the first time; see [game](families/Game.md) |
| `frames=<n>` | the game scene's frames, a whole number from 1 to 36000; 600 without it. Anything else is refused in the mode line with the number in effect: `frames=abc is not a whole number from 1 to 36000: it keeps 600`; without `game` the mode line says the family is left out |
| `machine=<name>[@<version>]` | the machine the run is on, one of `console`, `eden`, `ryujinx`, `scarletnx`, `scarletnx-android` and `android`, and its version when the runner knows it (1 to 32 letters, digits, dots, dashes and underscores): the ban list's lines for it hold. The runners write it ([The ban list](Crashes.md#the-ban-list)); without it no line holds. A name the list does not know, or a version that cannot be one, is refused in the mode line with the machine in effect: `machine=yuzu names no machine the ban list knows (console eden ryujinx scarletnx scarletnx-android android): the run has no machine, and no ban holds` |
| `vram=<MiB>` | the graphics memory the machine's card has of its own, in MiB, a whole number from 1 to 1048576: with less than 4096 the tex8k suite's textures are not tried, each device's case a `skip` with the reason `unavailable` and both numbers ([tex8k](families/Graphics.md#tex8k)). `make run` writes it from the host ([Under an emulator](Emulator.md)); without it, as on the console, they are tried. Anything else is refused in the mode line with the number in effect: `vram=8G is not a whole number of MiB from 1 to 1048576: the run has no vram, and tex8k's textures are tried` |
| `label=<name>` | the run's folder gets `_<name>` after the build's hash, to keep apart runs of one build under two emulators; see [the report](Report.md#where-a-run-leaves-its-report) |
| `baseline=<name>` | once the suites end, this run's times go into `sdmc:/edenprobe/baseline/<name>.jsonl` as a new sample; the name takes the label's rules; see [Baselines](Baseline.md) |
| `skip=<suite>` | the suite's cases are skipped as `asked`, whatever `hang`, `unban` or `retry` ask: a suite that harms this machine left out of every run ([Leaving a suite out](Crashes.md#leaving-a-suite-out)). The name its lines carry, 1 to 16 letters, digits and dashes, in any case; up to four, one word each. Anything else, or a fifth, is refused in the mode line with the suites in effect: `skip=te_x is not a suite name, 1 to 16 letters, digits and dashes, the first a letter: the run leaves out tex8k` |
| `compare=<name>` | once the suites end, this run's times are held to that file's samples: a line for each case held to them, `same`, `slower` or `faster`, and one that sums them up, no verdict changed; see [Baselines](Baseline.md) |

The judge and the cpu suite always run. With no family named, every family runs but audio, pad, kernel, display,
service, video, game, nvn and libc; on Android every family it has ([Frontends](Frontends.md)). Naming a family runs
that family alone, so `quick audio kernel` runs no graphics; `all` is every family but pad and nvn. A family this
build lacks is listed in the mode line and skipped. Any other word is quoted and counted in the mode line and has no
effect: a typo costs a line, never the run.

The start screen chooses the same words for a person; the file is for a script or a choice that should stay. Under
`make run` it is the `MODE` variable; in Eden's Android build, `files/sdmc/edenprobe/mode` in the app's data folder;
for edenprobe's own APK, `files/edenprobe/mode` in its external files ([Android](Android.md)). For example:

| the file holds | what runs |
|---|---|
| `quick net` | the judge, cpu and the six net suites, then exit: `make run MODE="quick net"` |
| `quick audio` | the judge, cpu and the audio suite, nearly silent, then exit |
| `listen` | every family but video, the listening sequence just before the audio suite, then the result screens |
| `quick listen audio` | the judge, cpu, the listening sequence and the audio suite, then exit |
| `quick fs` | the judge, cpu and the fs suite, then exit |
| `quick video` | the judge, cpu, the vic suite and the nvdec suite, then exit: what a console run of the video family wants |
| `quick pad` | the judge, cpu and the pad suite with a person in front of it, then exit |
| `quick all` | the judge, cpu and every family that needs nobody: all but pad and nvn, then exit |
| `quick all skip=tex8k` | the same, the tex8k suite's cases each a skip asked for: a handheld's run |
| `quick nvn` | the judge, cpu and the nvn suite, its window case too, then exit |
| `quick kernel display` | the judge, cpu, the sched suite and the binder suite, then exit |
| `quick service` | the judge, cpu and the service family's suites, then exit |
| `quick baseline=console` | the judge, cpu and the families a file naming none runs, then the run's times added as a sample to the console's baseline: on a console, a few times |
| `quick compare=console` | the same run, held to the console's baseline: on an emulator, with the console's file copied to its card |
| `quick game` | the judge, cpu and the game scene's 600 frames, then exit |
| `quick game frames=120` | the same scene for 120 frames, two seconds at 60 a second |
| `quick skip=tex8k` | the families a file naming none runs, the tex8k suite's cases each a skip asked for |

The first 4096 bytes are read, and a longer file says so in the mode line; a word, or a `label=`, `baseline=`,
`compare=` or `skip=` value, the limit cuts in two is dropped. A folder named `mode` reads as no file, and the mode line says so.
The mode line is a note of at most 767 bytes; a longer one ends `...; the mode line is cut here, its whole is N
bytes`.

```json
{"type":"note","text":"mode: quick, from the mode file; pictures of failures only; families graphics; 2 unknown words ignored: grpahics fast"}
```
