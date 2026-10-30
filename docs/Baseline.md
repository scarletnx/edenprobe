# Baselines

A run says whether each case got the right answer. A baseline says whether it got slower, on the machine itself and
with nothing but the memory card: the machine's own times, kept as samples in a file, and a run held to them case by
case.

```json
{"type":"baseline","baseline":"console","suite":"clear","name":"clear to cyan","result":"same","us":231,"reference_us":228,"limit_percent":20,"floor_us":100,"samples":5}
{"type":"baseline","baseline":"console","suite":"stream","name":"4096 draws from one list","result":"slower","us":9120,"reference_us":4410,"limit_percent":20,"floor_us":100,"samples":5}
{"type":"baseline_summary","baseline":"console","samples":5,"compared":341,"slower":1,"faster":0,"too_few":12}
```

## Making one

`baseline=<name>` in the [mode file](ModeFile.md) adds the run to `sdmc:/edenprobe/baseline/<name>.jsonl` once its
suites end: one sample, the `us` of every timed case ([Time per case](TimePerCase.md)), and a note with the sample's
number and how many the file has. The name follows the label's rules: letters, digits, dot, dash and underscore, at
most 32, not starting with a dot.

- Name it after the machine (`console`, `eden-master`, `scarletnx`): a baseline is a machine's, not a build's.
- Take at least three runs before comparing, and five or more where a case runs at one of two speeds per process.
- A file with samples from more than one edenprobe build says how many builds and names the first two, since a newer
  edenprobe can time a case differently.

These add nothing, each with a note: a `notime` run; a run whose families time no case (only the drawing suites and
`fs` are timed); a launch that went on after the program ended inside the run, which has only the cases after the one
it ended in (it is still compared, and its note says how many cases it had).

## The file

Plain text: a line that opens each sample (its number, the edenprobe build, the label, how many cases follow), then a
line per case. A console's file copied to an emulator's card is what `compare=console` holds the emulator to. It is
read strictly, so what a cut write leaves cannot pass for a time:

- a line counts only whole, every field and its closing brace; the next sample after a cut line starts on a line of its
  own, and a note says so;
- a sample counts only whole, every case its first line says it has; one short, one with none, and one whose first
  line was lost are left out, each named in a note with its counts;
- a case is found by its suite and name cut to 190 bytes; two names the file cannot tell apart are one case, which
  holds the first one's time, and a note says so.

## Holding a run to one

`compare=<name>` holds the run to that file once its suites end. A missing file is said in a note; a file that cannot
be read is said with the error, and is neither compared nor added to. A case with fewer than three samples is counted
in `too_few` and not compared; one with more than 64 is held to its newest 64, which a note says. For the others:

- the **reference** is the samples' median (the lower of the two middle ones);
- the **limit** is the larger of 20 percent and one and a half times the farthest a sample lies from that median, in
  percent of it;
- a case **moved** when it is past the limit and more than 100 microseconds (`floor_us`) away: `slower` above, `faster`
  below, `same` otherwise.

Every case compared gets a `baseline` line, the ones that did not move too, then one `baseline_summary` line with the
file's whole samples and the counts. Both words in one run compare first and then add, so a run is never held to
itself. No verdict moves: a slower case is never a failure.

## The floor and the limit

The floor is 100 microseconds, the one `tools/perf.py` uses: under an emulator one submit's round trip costs about
that, so a smaller move may be a round trip and not the work. It is a constant (`BASELINE_FLOOR_US`) until a console's
numbers say what its round trip is. It costs the short cases: one of R microseconds is called slower only past R + 100,
twice its time at 100 microseconds.

The limit is each case's own because some cases run at one of two speeds per process (two `order` cases, at about 970
or about 1390 microseconds on an RX 6700 under RADV): a fixed 20 percent called an unchanged build slower in more than
half the comparisons there, and the per-case limit in none.

On that machine, twenty runs of one runtime (`quick graphics fs`, 331 timed cases) held to sets of 5, 10 and 15 of the
others called a case that had not changed in 3 of 10000 case comparisons, always in the same two runs, each with one
case far off its usual time. Delays planted in the runtime were found in the cases that do that work:

- 5 microseconds a draw: the four `order` cases, both `stream` ones, the two 128-quad `fill` ones and `ring`'s 4096
  draws in 99 percent of the comparisons or more;
- 1 microsecond a file read: the sequential 512 byte and 4 KiB reads, the 4096 random reads and the 4096 reads of the
  same 4 KiB in 99 percent or more; not the 1 MiB and 16 MiB reads nor the 256 small files.
