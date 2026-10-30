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
| `fatal` | the cases the may-crash table names, which end the program on a machine that gets them wrong: SM4EKEY and SM4E, which the console's processor lacks, and the CNTVCT_EL0 and DAIF reads dynarmic does not model. On a console, a relaunch per case its processor lacks |
| `hang` | the cases the hang table names, which never return on a machine that gets them wrong: the bsd receive-timeout case. `retry` does not run them, and the start screen has no row for it |
| `retry` | the cases on the crash list run again; one that runs to its end leaves the list (see [When the program ends in the middle](Crashes.md)). A case the hang table names stays skipped until `hang` asks too |
| `nojournal` | no journal between cases, so a crash costs the rest of the run; for measuring what the journal costs |
| `noprogress` | no running screen while the suites run; for a machine whose window misbehaves while the suites draw |
| `cpu` | the processor family. It runs in every mode, so naming it alone leaves the graphics, fs and net families out: a few seconds, nothing drawn |
| `graphics` | the graphics family: the thirty drawing suites, clear to blit2d; selfcheck, support, reference and cpu always run, whatever family is named |
| `fs` | the fs family: reads from the memory card, timed and checked; it writes a 64 MiB file and 256 small ones under `sdmc:/edenprobe/fs` the first time |
| `audio` | the audio family: the renderer's resampler, circular buffer sink and limiter, recorded through its own effect and sink; only when named, since it makes sound (most at a gain of nought) |
| `listen` | the listening sequence before the audio family's cases, and the audio family with it: about 40 seconds of sound meant to be heard; see [The audio family](families/Audio.md) |
| `pad` | the pad family: a person presses each button the screen names, the sticks and a vibration, then taps targets on the touch screen; only when named; see [pad](families/Pad.md) and [touch](families/Touch.md) |
| `kernel` | the kernel family: the scheduler's priority inheritance, preemption and migration; only when named, with no row on the start screen; see [kernel](families/Kernel.md) |
| `display` | the display family: the buffer history of a layer of its own (transaction 17); only when named, with no row on the start screen; see [display](families/Display.md) |
| `video` | the video family: the video image compositor, a job per case, and the decoder on VP9 jobs from libvpx's test vectors, a job per frame; only when named, with no row on the start screen; see [video](families/Video.md) and [nvdec](families/Nvdec.md) |
| `service` | the service family: commands a game sends to the system's services, each answer held to reden's; only when named, with no row on the start screen; see [service](families/Service.md) |
| `net` | the net family: nifm, dns, bsd, ssl, acc and friend, against what `sdmc:/edenprobe/net` names |
| `game` | the game family: one scene that draws, plays a silent tone, reads the card, calls services and computes on four threads over three cores, as a game does, and presents and times every frame; only when named, with no row on the start screen; it writes the fs family's files the first time; see [game](families/Game.md) |
| `frames=<n>` | the game scene's frames, a whole number from 1 to 36000; 600 without it. Anything else is refused in the mode line with the number in effect: `frames=abc is not a whole number from 1 to 36000: it keeps 600`; without `game` the mode line says the family is left out |
| `label=<name>` | the run's folder gets `_<name>` after the build's hash, to keep apart runs of one build under two emulators; see [the report](Report.md#where-a-run-leaves-its-report) |
| `baseline=<name>` | once the suites end, this run's times go into `sdmc:/edenprobe/baseline/<name>.jsonl` as a new sample; the name takes the label's rules; see [Baselines](Baseline.md) |
| `compare=<name>` | once the suites end, this run's times are held to that file's samples: a line for each case held to them, `same`, `slower` or `faster`, and one that sums them up, no verdict changed; see [Baselines](Baseline.md) |

The judge and the cpu suite always run. With no family named, every family runs but audio, pad, kernel, display,
service, video and game. A family this build lacks is listed in the mode line and skipped. Any other word is quoted
and counted in the mode line and has no effect: a typo costs a line, never the run.

The start screen chooses the same words for a person; the file is for a script or a choice that should stay. Under
`make run` it is the `MODE` variable; on Android, `files/sdmc/edenprobe/mode` in the app's data folder
([Android](Android.md)). For example:

| the file holds | what runs |
|---|---|
| `quick net` | the judge, cpu and the six net suites, then exit: `make run MODE="quick net"` |
| `quick audio` | the judge, cpu and the audio suite, nearly silent, then exit |
| `listen` | every family but video, the listening sequence just before the audio suite, then the result screens |
| `quick listen audio` | the judge, cpu, the listening sequence and the audio suite, then exit |
| `quick fs` | the judge, cpu and the fs suite, then exit |
| `quick video` | the judge, cpu, the vic suite and the nvdec suite, then exit: what a console run of the video family wants |
| `quick pad` | the judge, cpu and the pad suite with a person in front of it, then exit |
| `quick kernel display` | the judge, cpu, the sched suite and the binder suite, then exit |
| `quick service` | the judge, cpu and the service family's suites, then exit |
| `quick baseline=console` | the judge, cpu and the families a file naming none runs, then the run's times added as a sample to the console's baseline: on a console, a few times |
| `quick compare=console` | the same run, held to the console's baseline: on an emulator, with the console's file copied to its card |
| `quick game` | the judge, cpu and the game scene's 600 frames, then exit |
| `quick game frames=120` | the same scene for 120 frames, two seconds at 60 a second |

The first 4096 bytes are read, and a longer file says so in the mode line; a word, or a `label=`, `baseline=` or
`compare=` value, the limit cuts in two is dropped. A folder named `mode` reads as no file, and the mode line says so.
The mode line is a note of at most 767 bytes; a longer one ends `...; the mode line is cut here, its whole is N
bytes`.

```json
{"type":"note","text":"mode: quick, from the mode file; pictures of failures only; families graphics; 2 unknown words ignored: grpahics fast"}
```
