# Time per case

## Index

* [What is measured](#what-is-measured)
* [How it is repeated](#how-it-is-repeated)
* [When a rehearsal hangs](#when-a-rehearsal-hangs)
* [What a number can see](#what-a-number-can-see)
* [Comparing two builds](#comparing-two-builds)

Every line a drawing suite reports ends with how long the case kept the graphics unit busy, in microseconds, and every
drawing suite ends with a line that sums its cases up:

```json
{"type":"case","suite":"clear","name":"clear to transparent cyan","tier":"exact","result":"pass","detail":"","us":231}
{"type":"perf","suite":"clear","timed":4,"total_us":7161,"min_us":192,"max_us":6530,"median_us":219,"submits":9,"samples":9}
```

## What is measured

All of a case's work on the graphics unit goes through one function in `source/gpu.c`, which reads the system tick
when it closes a command list and again when the queue stands idle. A case's time is the sum of those intervals since
the line before it: its clears, uploads, draw and readback. A line with no work since the previous one has no `us`
(the device opening, a shader loading, a case judged from another's readback). Recording commands, judging pixels,
writing the line or a picture and drawing the running screen all happen outside the intervals. Compare numbers from a
quick run from the mode file, which draws no running screen.

The game scene's seven times are frame statistics, measured once; see [The game family](families/Game.md#what-is-timed).
`tools/perf.py` compares them case by case and leaves them out of its per-suite sums.

## How it is repeated

A drawing suite runs ten times: a warm-up whose numbers are dropped, eight rehearsals with the report muted (no line,
no count, no picture), and the verdict pass, the only one a reader sees. Each line carries the smallest of the case's
nine samples, since everything else on the machine only adds time. The perf line's median is over the suite's cases
(the middle one, or the mean of the two middle ones, rounded down); `samples` is how many runs each number is the
smallest of, and `submits` how many submits the timed cases made in the verdict pass.

No verdict moves: each suite opens its own graphics device and closes it before it returns, so a rehearsal leaves the
verdict pass only the emulator's shader cache. The net family's `detail` carries the ports and buffers a run was
given, and so differs between runs.

`notime` (`make run MODE="quick notime"`) skips the warm-up and the rehearsals: each drawing suite runs once, no line
carries `us`, no perf line or timing note is written, and the mode note says `no timing passes`.

## When a rehearsal hangs

Three places say where:

- **The report** has one note before each drawing suite's rehearsals; a report that ends on it stopped while that
  suite was being timed:

  ```json
  {"type":"note","text":"timing depth: 1 warm-up and 8 rehearsals before the verdict pass"}
  ```

- **The debug channel** gets a line before every rehearsed case, so the emulator's log ends on the case about to run:

  ```
  EDENPROBE TIMING depth pass 4 case a fragment nearer than the buffer passes
  ```

  Passes count from one, the warm-up first. The warm-up numbers the case after the one it names (`pass 1 case number 3
  after a depth clear writes the value it was given`); one past the suite's last case is closing the device. The log
  line counts only when the report ends on the timing note: a hang in the verdict pass leaves the report ending on the
  case line before it.
- **The journal** on the card, whose last line names the case and the pass, the warm-up's included. On a console with
  no debugger it is the one that says which case.

## What a number can see

Every submit costs a round trip under an emulator, about a hundred microseconds whatever it carries, and the targets
are 64 by 64, so for most cases the round trip is the whole number: `submits` times that cost is the floor under a
suite's total, and what is above it is the work. A readback moves a case's time; a clear, a draw or a small upload sits
under the floor.

Two kinds of case are dominated by something other than their draw, and the warm-up fixes neither, since every pass
opens a fresh device:

- the first case of every suite, the device's first work: 6 to 10 milliseconds under Eden (2 to 2.9 where it is an
  upload, in `texture` and `coherence`);
- the first draw that samples a texture, 14 to 18.5 milliseconds in `texture` and `coherence`.

The two first uploads ("the texels reach the textures", "the starting texels reach the textures") also jump between
emulator processes, and `perf.py` usually names them unsteady.

## Comparing two builds

One report is one sample of the emulator's process, and processes differ (one now and then runs every case about 13
percent slower), so a comparison takes three runs a side, both sides in the same sitting, alternated if the machine is
shared:

```sh
tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
tools/perf.py before.jsonl after.jsonl      # one run a side, a quick look only
tools/perf.py report.jsonl                  # the per-suite table of one run
tools/perf.py r1.jsonl r2.jsonl r3.jsonl -- # the table of one side of three
```

A case's time on a side is its median over the three. A move is listed when it is more than 20 percent (`--threshold`)
**and** more than 100 microseconds (`--floor`); a case whose runs on either side are more than 75 percent apart
(`--unsteady`) is named unsteady and not compared. With those defaults, 9240 comparisons of a build with itself listed
no move; one run a side lists a case that did not change in about half the pairs.

It lists the cases that moved, largest first, then the unsteady ones, then the cases only one side timed, then a table
per suite: cases timed, the two totals, the two medians and the change in each. It reads the case lines, not the perf
lines, and skips a line it cannot parse, or that is not UTF-8, with a count on standard error, so a cut report still
compares. A report with nothing timed (empty, `notime`, or older than timing) is refused with exit status 1, alone or
as one run of a side; a suite only one side timed says so instead of a percent. Its tests run with the host suite.
