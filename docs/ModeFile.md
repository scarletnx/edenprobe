# The mode file

**The mode file** is `sdmc:/edenprobe/mode`: a few words in any order and any case, anything that is
not a letter between them.

| word | what it does |
|---|---|
| no file, or no `quick` in it | detailed |
| `quick` | the automatic run: every suite, the report, exit |
| `detailed` | detailed even beside `quick`, the same as holding X |
| `images` | a picture for every drawing case, passing ones too, on either mode |
| `notime` | no timing passes: each drawing suite runs once, and no line carries a time; see [Time per case](TimePerCase.md) |
| `stress` | the heavy variants too, today the stream suite's 256 MiB list; off by default, since an emulator that runs out of memory on one takes the rest of the run with it |
| `fatal` | the cases the may-crash table names, known to end the program on a machine that gets them wrong: today SM4EKEY and SM4E, which the console's processor lacks, and the CNTVCT_EL0 and DAIF reads dynarmic does not model; off by default. On a console it costs a relaunch per case its processor lacks |
| `hang` | the cases the hang table names, known to never return on a machine that gets them wrong: today the bsd receive-timeout case; off by default. `retry` does not run one, and the start screen cannot turn this on |
| `retry` | the cases on the crash list run again; one that runs to its end leaves the list (see [When the program ends in the middle](Crashes.md)). A case the hang table names stays skipped until `hang` asks too |
| `nojournal` | nothing is written between cases: no journal, so a crash costs the rest of the run as it did before the journal; for measuring what the journal costs |
| `noprogress` | no running screen while the suites run: the window keeps the screen before it, as it did before there was one; for a machine whose window misbehaves while the suites draw |
| `cpu` | the processor family: the cpu suite runs in every mode, so naming it alone keeps the graphics, fs and net families out, a run of a few seconds with nothing drawn |
| `graphics` | the graphics family: the twenty eight drawing suites, clear to dma; selfcheck, support, reference and cpu always run, whatever family is named |
| `fs` | the fs family: reads from the memory card, timed and checked; it writes a 64 MiB file and 256 small ones under `sdmc:/edenprobe/fs` the first time |
| `audio` | the audio family: the renderer's resampler, circular buffer sink and limiter, recorded through its own effect and sink; only when named, since it makes sound, most of it at a gain of nought |
| `listen` | the listening sequence before the audio family's cases, and the audio family with it: about 40 seconds of sound meant to be heard; see [The audio family](families/Audio.md) |
| `pad` | the pad family: a person presses each button the screen names, by its label, then the sticks and a vibration; only when named, since it waits for a person; see [The pad family](families/Pad.md) |
| `kernel` | the kernel family: the scheduler's priority inheritance, preemption and migration, threads of chosen priorities on one core and a watchdog on another; only when named, until a console has run it, and the start screen has no row for it; see [The kernel family](families/Kernel.md) |
| `display` | the display family: the buffer history of a layer of its own (transaction 17); only when named, until a console has run it, and the start screen has no row for it; see [The display family](families/Display.md) |
| `video` | the video family: the video image compositor given input the processor wrote, one job per case, and the decoder given VP9 jobs built from libvpx's test vectors, one job per frame; only when named, until a console has run it, and the start screen has no row for it; see [The video family](families/Video.md) and [the nvdec suite](families/Nvdec.md) |
| `net` | the net family: nifm, dns, bsd, ssl, acc and friend, against what `sdmc:/edenprobe/net` names |
| `label=<name>` | not a word: the run's folder gets `_<name>` after the build's hash, to keep apart runs of one build under two emulators; see [Where a run leaves its report](Report.md#where-a-run-leaves-its-report) |

Families choose what runs beyond the judge, which always runs because nothing a family reports can
be believed without it, and beyond the cpu suite, which needs no device and runs in every mode. None
named means every family this build has but audio, which makes sound, pad, which waits for a person,
and kernel, display and video, which no console has run yet; a named family this build lacks is
listed in the mode line and skipped, so a file written for a later build is reported, not refused.
`net` alone runs the judge, cpu and the six net suites, which is what [the
fixture](families/Net.md#the-fixture) writes. Any other word is reported in the mode line, quoted
and counted, and ignored: a typo costs a line in the report, never the run.

The start screen chooses the same words for a person; the file is for a script, for a run with no
one in front of it, and for a choice that should stay. On a console it is `sdmc:/edenprobe/mode` on
the memory card; under `make run` it is the `MODE` variable; on Android it is
`files/sdmc/edenprobe/mode` in the app's data directory (see [Running it on Android](Android.md)).
For example:

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

The first 4096 bytes of the file are read. A longer file is read that far and the mode line says
"mode file longer than 4096 bytes, the rest ignored"; a word the limit cuts in two is dropped rather
than reported as an unknown word nobody wrote. A directory called `mode` reads as no file, and the
mode line says so.

```json
{"type":"note","text":"mode: quick, from the mode file; pictures of failures only; families graphics; 2 unknown words ignored: grpahics fast"}
```
