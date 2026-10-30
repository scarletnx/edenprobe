# When the program ends in the middle

A case can end the program: a shader an emulator cannot translate, an instruction its processor lacks, a service it
has not written. That costs the case, not the run: the run goes on over as many launches as it takes, in one report.

## The journal

`journal`, in the run's folder ([the report](Report.md#where-a-run-leaves-its-report)), records the run a line at a
time, each on the card before the program goes on. A launch that finds the last line a case's start knows the
program ended inside that case: it skips the steps already done, cuts the report back to where the step under way
began, reports the case as a `skip` with the reason `crashed`, puts it on the crash list and runs the step again
without it.

```json
{"type":"note","text":"journal: launch 2 resumes the run; the program ended inside index case \"an indexed draw with the library's open limit uses every index\" in a timed pass: that case is skipped as crashed and goes on the crash list, and step index runs again from its start"}
{"type":"case","suite":"index","name":"an indexed draw with the library's open limit uses every index","tier":"none","result":"skip","reason":"crashed","detail":"the program ended inside it earlier in this run, in a timed pass; ..."}
{"type":"note","text":"journal: this run took 2 launches; 1 case skipped as crashed; the program ended inside index \"an indexed draw with the library's open limit uses every index\""}
```

A step is what main runs as one: a suite, or the whole net family. A launch goes on with a run only when the journal
is this build's, began with the same mode words (quick and detailed aside), and the report is still there and at
least as long as the journal says; otherwise it starts again and says why. To start over, delete the journal or the
report, or choose it on the screen.

A detailed launch that could go on asks first: a screen says where the program stopped, which launch this is, the
steps done, the counts and the folder, with two choices, go on (the cursor's) or start over. Up and down move, A
confirms, and nothing pressed for ten seconds goes on. A quick run never asks.

| journal line | written |
|---|---|
| `run <version> <build> <mode words>` | first, when a run begins |
| `chosen screen <words>` | after it, when the start screen chose a mode the mode file does not say |
| `launch <n>` | each time the program starts on the run, 1 the first |
| `step <step> <offset> <passed> <failed> <skipped> <crashed>` | before a step: the report's length then, and its counts |
| `pass <step> <n>` | before each pass of a drawing suite, 1 the warm-up and 10 the verdict pass |
| `start <suite> <pass> <case>` | before a case's work; pass 0 outside the drawing suites |
| `end <suite> <pass> <result> <case>` | after its line: `pass`, `fail`, `skip`, `crashed`, or `muted` in a rehearsal |
| `crashed <suite> <pass> <case>` | by the launch after, for the case the program ended inside |
| `done <step>` | after a step |
| `finished <passed> <failed> <skipped> <crashed>` | after the summary |

Every line ends in a tab before its newline. A last line that ends in its tab but has no newline is a whole record
(Eden's abort keeps only the newline back); one without the tab is a cut write and never read.

## The crash list

`sdmc:/edenprobe/crashes` names the cases that ended this machine's program, `<suite> <case>` a line. They are skipped
as `crashed` in every later run until the word `retry` runs them again; one that then runs to its end leaves the
list. A case that ended the program in this very run is skipped even under `retry`. Delete a line to try one case
again; the program rewrites the file whole, so a comment typed into it does not stay.

## The may-crash and hang tables

`source/maycrash.c` names the cases that end the program on a machine that gets them wrong; they are skipped as `may
crash` unless `fatal` or the start screen asks for them. `source/hang.c` names the cases that never return on such a
machine (the bsd receive with a timeout); they are skipped as `hangs` unless `hang` asks, and `retry` never runs
them, since a hang does not end the program. A case on the crash list stays `crashed` whatever `fatal` or `hang`
say, until `retry`. A note before the summary names an entry whose case this build never reached.

## What a crash costs

Every case of the cpu, graphics, fs, kernel, display, video, audio and net families says it starts before any of its
work, so a launch can skip it. A case judged from another's work is skipped as `depends` when that one does not run.
What runs between two cases (a device reopened, a service opened, the listening sequence) belongs to no case: a crash
there costs the rest of that step, and a note says so. A case that reports without having said it started is a bug
here, named by a `bookkeeping:` note.

The journal writes two lines per case per pass, never inside a timed interval. `nojournal` turns it off, and a crash
then costs the rest of the run. Each line is written without its newline, flushed, then the newline written, so an
emulator that buffers the card's writes (Eden) loses at most the newline; the report is written the same way while
the journal is on.

## Ryujinx and the console

Started by hand: when the program ends, open `edenprobe.nro` again, until the report ends on its summary. Ryujinx's
card is `~/.config/Ryujinx/sdcard/edenprobe/` on Linux (`$XDG_CONFIG_HOME/Ryujinx/...`, or `portable/sdcard/` beside a
portable build).
