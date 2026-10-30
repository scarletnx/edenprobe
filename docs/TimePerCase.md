# Time per case

Every line a drawing suite reports ends with how long the case kept the graphics unit busy, in
microseconds, and every drawing suite ends with a line that sums its cases up:

```json
{"type":"case","suite":"clear","name":"clear to transparent cyan","tier":"exact","result":"pass","detail":"","us":231}
{"type":"perf","suite":"clear","timed":4,"total_us":7161,"min_us":192,"max_us":6530,"median_us":219,"submits":9,"samples":9}
```

**What is measured.** Every piece of work a case puts on the graphics unit goes through one function
in `source/gpu.c`, which reads the system tick when it closes a command list and again when the
queue stands idle after it. A case's time is the sum of those intervals since the line before it:
its clears, uploads, draw and readback. A line with no work since the previous one has no `us` (the
device opening, a shader loading, a case judged from another's readback). Recording the commands,
judging the pixels and writing the line or a picture happen outside those intervals, so a number is
the machine's work, and a slower judge cannot move it. So does drawing the running screen, which a
run with a screen does between cases, at most twice a second; a quick run from the mode file draws
none, which is the run to compare numbers from.

**The game scene's four times** are of another kind: the mean and the 99th percentile of its
frames, present to present and as a frame's work without the waits, measured once and not
repeated; see [The game family](families/Game.md#what-is-timed).

**How it is repeated.** A drawing suite runs ten times: a warm-up whose numbers are dropped, eight
rehearsals with the report muted (no line, no count, no picture), and the verdict pass, the only one
a reader sees. Each line carries the smallest of the case's nine samples, because everything else on
the machine only ever adds time. Five runs of a build that kept every sample settled both choices:
over the ten pairs, the minimum of nine put 92 percent of cases within a tenth of the other run, the
median of nine 79, and past nine bought almost nothing (93 at thirteen). The perf line's median is
over the suite's cases (the middle one, or between the two middle ones rounded down); `samples` is
how many runs each number is the smallest of, and `submits` how many submits the timed cases made in
the verdict pass.

**Why no verdict moves.** Each suite opens its own graphics device and closes it before it returns,
so a rehearsal leaves the verdict pass nothing but the emulator's shader cache, which a second
`make run` inherits anyway. Checked on the build with the `format` suite, the grown `intmath` and
`fmath` and the `net` family: all 460 case lines the same once `"us"` was taken off, failures and
skips included, in six quick runs and a `notime` run, and the failures' pictures identical. The one
allowance is the net family's `detail`, which carries the ports and buffers a run was given.

**When a rehearsal hangs**, the run says where three times. The report has one note before each
drawing suite's rehearsals, and a report that ends on it stopped while that suite was being timed:

```json
{"type":"note","text":"timing depth: 1 warm-up and 8 rehearsals before the verdict pass"}
```

The debug channel, and only it, gets a line before every rehearsed case, so the emulator's log ends
on the case that was about to run:

```
EDENPROBE TIMING depth pass 4 case a fragment nearer than the buffer passes
```

Passes count from one, the warm-up first. The warm-up has no earlier pass to learn names from, so
its lines number the case, `pass 1 case number 3 after a depth clear writes the value it was given`,
meaning the case after the one named; one past the suite's last case is closing the device. The log
line counts only when the report ends on the timing note: a hang in the verdict pass leaves the
report ending on the case line before it, and a stale timing line in the log. The third is the
journal on the card, whose last line names the case and the pass by name, the warm-up's included;
on a console with no debugger it is the one that says which case.

`notime` in the mode file (`make run MODE="quick notime"`) skips the warm-up and the rehearsals:
every drawing suite runs once, no line carries `us`, no perf line or timing note is written, and the
mode note says `no timing passes`. That is the old drawing run case for case, plus the nineteen
support cases that check the timing, and the way to get the case line a hang stops at.

**What a number can see.** Every submit costs a round trip, whatever it carries. Under eden-cli,
over the 374 case lines under two milliseconds in two runs at a load near eight, a case's time is
about 145 microseconds plus 31 per submit, or 80 per submit taken alone (the middle half between 73
and 102). The targets are 64 by 64, so for most cases the round trip is the whole number: a case of
three submits takes about 235 microseconds, its floor and not its draw. The critic proved it with
one case changed at a time, 40 comparisons of three runs a side:

| one case changed to | its time | listed |
|---|---|---|
| read its target back twice | 190 to 270 microseconds to 320 to 370 | 40 of 40 |
| clear twice | about 40 more, inside the noise | 0 of 40 |
| draw its triangle twice, or sixteen times | no change | 0 of 40 |
| upload its 4 by 4 texture once more | 45 to 70 more, one round trip | 0 of 40, unsteady in 20 |

So under an emulator this watches the readbacks and the first case of each suite. A clear, a draw or
an upload of this size costs less than one round trip, about a hundred microseconds, and sits under
the floor: `submits` times the per-submit cost is the floor under a suite's total, and what is above
it is the work. A console's floor is unknown until one has run it.

Two kinds of case are dominated by something other than their draw, and the warm-up fixes neither,
since every pass opens a fresh device and the emulator pays again:

- the first case of every suite, the device's first work: 6 to 10 milliseconds where that is a
  clear, a draw and a readback. It is not a shader compiling: `clear` has no shader and pays it too.
  In `texture` and `coherence` the first work is an upload, and that case's 2 to 2.9 milliseconds is
  the device's first work too, not the upload;
- the first draw that samples a texture, 14 to 18.5 milliseconds in `texture` and `coherence`.

Those two first uploads ("the texels reach the textures", "the starting texels reach the textures")
are not one number at all: 2 to 2.9 milliseconds in most emulator processes, anything from 60
microseconds to 1.6 milliseconds in about one in five. A side of three holding such a process
spreads past the unsteady limit, about half the sides, and perf.py names the case as unsteady
instead of seeing a move (the `texture` one in 7140 of the 9240 splits below, the `coherence` one in
4620).

**The method.** Inside one process the smallest of nine samples is steady. Between processes it is
not: now and then one emulator process runs nearly every case about 13 percent slower than the next,
which no number of samples inside a process can see. So one report is one sample of the process, and
a comparison takes three runs a side, both sides run in the same sitting:

```sh
tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
```

A case's time on a side is its median over the three, which needs two processes to agree; the
smallest was tried and lost, since the one fast process then decides the side (it listed a move in 9
of 840 splits of nine runs and 16206 of 185640 of eighteen). A move is listed when it is more than
20 percent **and** more than 100 microseconds, and a case whose three runs on either side are more
than 75 percent apart is named as unsteady and not compared. On twelve runs of one build in one
sitting (six at a load of 4 to 5.5, six at 1 to 14), split every way into two sides of three, 9240
comparisons of a build with itself:

| limits | comparisons listing a move |
|---|---|
| 20 percent alone | 840 |
| 20 percent and 100 microseconds | 840 |
| 20 percent and unsteady at 100 percent, with or without the floor | 91 |
| 20 percent, 100 microseconds, unsteady at 75: the defaults | 0 |
| 30 percent, 100 microseconds, unsteady at 100 | 0 |

The unsteady rule is what keeps noise out, not the 20 percent: everything the lesser limits listed
was one of the two first uploads, and at 75 the rule sets aside those two and nothing else, while at
50 steady `blend` cases start being set aside. The floor keeps the quarter-millisecond cases quiet
on a noisier day: on the critic's nine runs of an earlier build, twenty percent with no floor listed
a move in 76 of 1680 ordered splits, and none with it. And 30 percent would have hidden a 20 to 30
percent move in exactly the cases this can watch.

On those twelve runs the sides agree on 85.4 percent of cases within a tenth at worst, 98.0 typical,
99.0 within a fifth at worst, and suite medians within 10.1 percent; on the six at a load of 4 to
5.5 alone, every case within a tenth in all ten splits and suite medians within 4.7 percent. Six
more runs of the build with the `net` family, at a load near nine, listed no move, with cases within
a tenth 99.0 percent at worst, suite medians within 7.5 percent, and the `texture` upload set aside
in all ten. One report a side is accepted for a quick look, but 32 of the 66 pairs of the twelve,
and 8 of the 15 of the six, list a case that did not change. Both sides in one sitting is part of
the method, not advice: eighteen runs from two sittings about an hour apart, at different loads,
listed a move in 24135 of 185640 splits, nearly all the two first sampling draws (about 18.3
milliseconds typical in the first sitting, 14.8 in the second): the device's first work moves with
the machine more than anything else. Run the two builds alternately if the machine is shared. The
perf line and a case's `us` stay one process's figure; the method lives in the comparison.

**Comparing two runs.**

```sh
tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
tools/perf.py before.jsonl after.jsonl      # one run a side
tools/perf.py report.jsonl                  # the per-suite table of one run
tools/perf.py r1.jsonl r2.jsonl r3.jsonl -- # the table of one side of three
```

It lists the cases that moved, largest first, then the unsteady ones, then the cases only one side
timed, then a table per suite: cases timed, the two totals, the two medians and the change in each.
`--threshold`, `--floor` and `--unsteady` change the three limits, each a finite number of nought or
more. It reads the case lines, not the perf lines, and skips a line it cannot parse, or that is not
UTF-8, with a count on standard error, so a report cut off by a killed run still compares. A report
with nothing timed (an empty file, a `notime` run, a report from before cases were timed) is refused
on standard error with exit status 1, alone or as one run of a side, rather than compared as a
hundred percent faster; a suite only one side timed says so instead of a percent. Its tests run with
the host suite.
