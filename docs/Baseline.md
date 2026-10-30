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
counts.

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

## Why the limit is each case's own

A fixed percentage calls a build of unchanged source slower. Twelve runs of one build on one machine
(an RX 6700 under RADV, the recompiler of ScarletNX), compared three runs a side with 20 percent and
100 microseconds, called 57.6 percent of the comparisons slower, always in the same two `order`
cases: each runs at about 970 or about 1390 microseconds, decided per process. No threshold, floor
or median fixed that without hiding real slowdowns. A limit taken from each case's spread on the
same machine did: none of 400 comparisons of the build against itself called slower, and delays
planted in the runtime (5 microseconds a draw, 1 a file read, 50 a draw) were each found in all 400,
in the cases that do that work. That was measured on the reports outside the program, three runs a
side. The same limit inside it, one run held to five samples of the same machine and runtime, called
nothing slower or faster in three runs (331 cases each); with 5 microseconds planted on every draw
it called the seven cases slower that draw hundreds or thousands of times (`order`, `stream`,
`fill`, `ring`), and one faster: `fill`'s first depth tested quad, whose time is mostly a first
use's; with 1 microsecond on every file read, the seven `fs` cases that read a thousand times or
more, and not the three that read a few large pieces or 256 small files once each.
