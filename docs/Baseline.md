# Baselines

A run says whether each case got the right answer. A baseline says whether it got slower, on the
machine itself and with nothing but the memory card: a machine's own times, kept as samples in a
file, and a run held to them case by case.

```json
{"type":"baseline","baseline":"console","suite":"clear","name":"clear to cyan","result":"same","us":231,"reference_us":228,"limit_percent":20,"floor_us":100,"samples":5}
{"type":"baseline","baseline":"console","suite":"stream","name":"4096 draws from one list","result":"slower","us":9120,"reference_us":4410,"limit_percent":20,"floor_us":100,"samples":5}
{"type":"baseline_summary","baseline":"console","samples":5,"compared":341,"slower":1,"faster":0,"too_few":12}
```

## Making one

`baseline=<name>` in the [mode file](ModeFile.md) adds the run to
`sdmc:/edenprobe/baseline/<name>.jsonl` once its suites end: one sample, the time every timed case
took (the `us` its line carries, see [Time per case](TimePerCase.md)), and a note saying which
sample it was and how many the file now has. The name follows the label's rules: letters, digits,
dot, dash and underscore, at most 32, not starting with a dot.

A baseline is a machine's, not a build's, so it is named after the machine: `console`,
`eden-master`, `scarletnx`. It is one edenprobe build's too, though: a newer edenprobe can time a
case differently, so a comparison whose file has samples of more than one says how many builds and
names the first two. A few runs make one; at least three before anything is compared, and five or
more where a case runs at one of two speeds per process (see below), which three runs can all miss.
The file is plain text, one line per case of each sample, and travels: a console's copied to an
emulator's memory card is what `compare=console` holds the emulator to. A line is read only whole,
every field and its closing brace: one cut short when the program ended while writing it, inside a
number say, is no case, and the next sample added starts on a line of its own, which a note says. A
sample is read only whole too, every case its first line says it has: one short of them, one that
opened with none, and one whose first line was lost are left out, each named in a note with its
counts. A case is found in the file by its suite and name as a line carries them, cut to 190
bytes: two names a file cannot tell apart, the same that far or a tab where the other has a space,
are one case to a baseline, which holds and adds the first one's time and says so in a note.

A run with `notime` has no times and adds nothing, nor does one whose families time no case: only
the drawing suites and `fs` are timed, so `quick cpu` adds nothing. Neither does a run that went on
after the program ended inside it: its first launch's times went with that launch, and a sample
with half its cases would pull every reference toward the half it has. Each says so in a note.

## Holding a run to one

`compare=<name>` holds the run to that file once its suites end. A file that is not there is said
in a note, and so is one that is there and cannot be read, with the error: that one is neither
compared nor added to, since a sample added after a file it could not read could repeat a number or
land on a cut line. A case with fewer than three samples there, or none, is counted in `too_few`
and not compared, and one with more than 64 is held to its newest 64, which a note says. For the
others:

- the **reference** is the samples' median (the lower of the two middle ones);
- the **limit** is the larger of 20 percent and one and a half times the farthest a sample lies from
  that median, in percent of it;
- a case **moved** when it is past the limit and more than 100 microseconds away: `slower` above,
  `faster` below, and `same` otherwise.

Every case compared gets a `baseline` line, the ones that did not move too, since how far each case
is from the machine the samples came from is what a baseline is read for: its result, its time,
the reference, its limit, the floor (`floor_us`) and how many samples it was held to. A case
moved when it lies farther from `reference_us` than both `floor_us` and `limit_percent` of it.

One `baseline_summary` line follows, with the file's samples and the counts. Both words in one run
compare first and then add, so a run is never held to itself. No verdict moves: the run's summary is
the same with either word or without, and a slower case is never a failure. Whether a slower case
matters is for whoever reads the line, who has its numbers.

## The floor

A move also has to pass 100 microseconds, the floor `tools/perf.py` uses too (see [Time per
case](TimePerCase.md)): under an emulator one submit's round trip costs about that, so a case that
moved by less may have moved by a round trip and not by its work. It is a constant
(`BASELINE_FLOOR_US`), not a word, until a console's numbers say what its round trip is.

It costs the short cases. A case of R microseconds is called slower only past R + 100: twice its
time at 100 microseconds, three times at 50, five times at 25. On the machine measured below, 222
of the 331 timed cases have a reference of 100 microseconds or less, so none of them is called
slower under twice its time, whatever its spread. A floor of 50 found the same planted delays there
and called a case that had not changed in 1.6 to 2.2 times as many comparisons (see below).

## Why the limit is each case's own

A fixed percentage calls a build of unchanged source slower. Twelve runs of one build on one machine
(an RX 6700 under RADV, the recompiler of ScarletNX), compared three runs a side with 20 percent and
100 microseconds, called 57.6 percent of the comparisons slower, always in the same two `order`
cases: each runs at about 970 or about 1390 microseconds, decided per process. No threshold, floor
or median fixed that without hiding real slowdowns. A limit taken from each case's spread on the
same machine did: none of 400 comparisons of the build against itself called slower, and delays
planted in the runtime (5 microseconds a draw, 1 a file read, 50 a draw) were each found in all 400,
in the cases that do that work. That was measured on the reports outside the program, three runs a
side.

## What it calls on one machine

Inside the program, on the same machine and recompiler: twenty runs of one runtime, one after the
other in one game folder, `quick graphics fs`, 331 timed cases each. Each run was held to every set
of 5, 10 and 15 of the other nineteen with this program's own code, and a run with a delay planted
in the runtime to every set of the twenty:

| samples | comparisons | calling a case that had not changed | the same with a floor of 50 |
|---|---|---|---|
| 5 | 232560 | 10.2 percent | 22.2 percent |
| 10 | 1847560 | 10.0 percent | 19.9 percent |
| 15 | 77520 | 10.0 percent | 16.3 percent |

At the floor of 100 that is 3 in 10000 of the case comparisons, and the same two of the twenty runs
every time, whatever the samples: one with a case at four times its usual time once (414
microseconds against about 105, in `order`), and the first run in the folder, with `fill`'s first
depth tested quad at two thirds of its usual time (849 against about 1240). On this machine one run
in ten carries a case like that (two in twenty, which twenty runs pin only roughly), and more
samples do not stop it being called: the run moved, not the samples. Inside the program the twenty
said the same, each held to the runs before it from the fourth on: one of the seventeen called a
case, the run with the 414.

The planted delays, with nothing called faster, and at a floor of 50 the same but for one number,
`swizzle`'s upload in 78 percent of the five-sample comparisons against 74:

- 5 microseconds on every draw: 9 to 10 cases slower a comparison. The four `order` cases, both
  `stream` ones, the two 128-quad `fill` ones and `ring`'s 4096 draws, which draw hundreds or
  thousands of times, in 99 percent of the comparisons or more; `swizzle`'s 4 MiB upload in 24
  (fifteen samples) to 74 percent (five).
- 1 microsecond on every file read: 6 a comparison. The sequential reads of 512 bytes and 4 KiB, the
  4096 random reads at either offset and the 4096 reads of the same 4 KiB in 99 percent or more, the
  sequential 64 KiB reads in 87 to 100, the 64 KiB reads walking the file backwards in 0 to 21;
  not the sequential 1 MiB and 16 MiB reads, a few large pieces, nor the 256 small files.
