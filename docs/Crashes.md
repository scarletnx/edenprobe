# When the program ends in the middle

A case can end the program that runs it: a shader an emulator cannot translate, an instruction its
processor lacks, a list asking for more memory than it has, a service it has not written. Once,
such a case cost the rest of the run. Now it costs the case: the run goes on over as many launches
as it takes, and one report covers it all.

**The journal.** `journal`, in the run's folder (see [Where a run leaves its
report](Report.md#where-a-run-leaves-its-report)), says what the run has done, a line at a time,
each on the card before the program goes on: before a case starts, its name and the pass; after it
ends, its result. A launch that finds a run whose last line is a case starting knows the program
ended inside that case, and goes on with the run instead of starting another. It skips the steps the
journal says are done, cuts the report back to where the step under way began, reports the case as a
skip whose reason is `crashed`, puts it on the crash list, and runs the step again without it;
everything after it runs as it would have. The launch says so in a note where the report goes on,
and the note before the summary says how many launches the run took, how many cases it skipped as
crashed, how many of those only because the crash list named them, and which cases the program ended
inside in this run:

```json
{"type":"note","text":"journal: launch 2 resumes the run; the program ended inside index case \"an indexed draw with the library's open limit uses every index\" in a timed pass: that case is skipped as crashed and goes on the crash list, and step index runs again from its start"}
{"type":"case","suite":"index","name":"an indexed draw with the library's open limit uses every index","tier":"none","result":"skip","reason":"crashed","detail":"the program ended inside it earlier in this run, in a timed pass; ..."}
{"type":"note","text":"journal: this run took 2 launches; 1 case skipped as crashed; the program ended inside index \"an indexed draw with the library's open limit uses every index\""}
```

And in the next run, which skips it from the crash list without ending:

```json
{"type":"note","text":"journal: this run took 1 launch; 1 case skipped as crashed, 1 of them from the crash list"}
```

A step is what main runs as one: a suite, or the whole net family. A launch goes on with a run only
when the journal is this build's, was begun with the same mode words (quick and detailed aside,
since they change nothing a report holds), and the report it describes is still there and at
least as long as the journal says it was; otherwise it starts the run again, and says why in a
note. A case the journal shows the program ended inside goes on the crash list whichever way the
launch goes. To start over rather than go on, delete the journal or the report, or choose it on the
screen below.

**Going on or starting over.** A detailed launch that could go on with a run asks first, before
it opens anything: a screen says where the program stopped (inside which case, in which step, or
between two steps), which launch of the run this is, how many of the run's steps are done, the
counts so far and the run's folder, and offers two choices, the cursor on the first: go on from
where it stopped, or start over. Up and down move the cursor, A confirms; nothing pressed for ten
seconds goes on, as a launch always did, so a detailed run nobody attends still finishes over its
relaunches. Starting over begins a new report in the same folder, and the report's first note
says why: `journal: a new run (the screen was asked to start over rather than go on)`; a case the
program ended inside still goes on the crash list. A quick run never asks: it is a script's, and
`make run` relaunches it expecting it to go on. A card that says `quick` with X held at launch does
not ask either, since the pad is read only once the run has started.

| journal line | written |
|---|---|
| `run <version> <build> <mode words>` | first, when a run begins |
| `chosen screen <words>` | after it, when the start screen chose a mode the mode file does not say: the words a launch compares, and `quick` |
| `launch <n>` | each time the program starts on the run, 1 the first |
| `step <step> <offset> <passed> <failed> <skipped> <crashed>` | before a step: the report's length then, and its counts |
| `pass <step> <n>` | before each pass of a drawing suite, 1 the warm-up and 10 the verdict pass |
| `start <suite> <pass> <case>` | before a case's work; pass 0 outside the drawing suites |
| `end <suite> <pass> <result> <case>` | after its line: `pass`, `fail`, `skip`, `crashed`, or `muted` in a rehearsal |
| `crashed <suite> <pass> <case>` | by the launch after, for the case the program ended inside |
| `done <step>` | after a step |
| `finished <passed> <failed> <skipped> <crashed>` | after the summary |

Every line ends in a tab before its newline, which no field can hold. A last line with no
newline that ends in its tab lost only the newline, which is what an abort on Eden takes (see
below), and is read as the record it is: a start there is the case the program ended inside. A
last line without the tab is a write cut inside its body, and is never read as a record; nor is
a line that is not a whole one. A launch that goes on with the run ends that line before its own
first record, and `tools/run.py` does the same before it writes `stopped`, so nothing joins it.

**The crash list.** `sdmc:/edenprobe/crashes` names the cases that ended this machine's program,
`<suite> <case>` a line under a comment that says what it is. They are skipped in every later run
as `crashed`, until the mode word `retry` runs them again; one that then runs to its end, through
every pass, leaves the list, and a note says so. A case that ended the program in this very run is
skipped even under `retry`, since running it is what ended the launch before. A case the hang
table names stays skipped as `hangs` whether or not `retry` asks, until `hang` does too. The file
can be edited by hand, one line deleted to try one case again; the program writes it whole when
it changes, so a comment typed into it does not stay.

**The may-crash table.** `source/maycrash.c` names the cases known to end the program on a machine
that gets them wrong, each with its suite, its family and why, in one table. They are skipped with
the reason `may crash` and the table's sentence unless the mode word `fatal` or the start screen
asks for them (see [Looking at it on the console](Console.md)); a machine that then ends inside one
goes on with the run after it like any other. A case on the crash list stays skipped as `crashed`
whether or not `fatal` asks, until `retry` does too; a case that ended the program in this run is
skipped whatever either says. A launch whose suites never reached a case the table names says so in
a note before the summary, since an entry naming a case this build lacks protects nothing.

**The hang table.** `source/hang.c` names the cases known to never return on a machine that gets
them wrong. They are skipped with the reason `hangs` and the table's sentence unless the mode word
`hang` asks for them. `retry` does not run one: a hang does not end the program, so there is no
next launch to mark it until something else kills the process, and running it again hangs that
launch too. Today the table has one case, the bsd receive that asks for a timeout. The start
screen cannot turn `hang` on. A case that ended the program in this run is still skipped as
`crashed` before the hang table is consulted, whatever `hang` says.

**Where a crash can be pinned.** Every case of the cpu, graphics, fs, video, audio and net families
says it starts before any of its work, which is what lets a launch skip it: the start and the skip
come before the first instruction, list or service call the case makes. A case judged from another's
work (two halves of one readback, a request asked about again, a token read twice) is skipped as
`depends` when that one does not run. The judge's cases ask the machine nothing but ordinary
arithmetic and are journaled only as they end. What runs between two cases belongs to none: a suite
reopening its device after a fault, blend's two probe notes, the net family opening a service or
waiting for the sign-in, the audio family opening the renderer or playing the listening sequence. A
crash there costs the rest of that step, which keeps what it reported and is not run again, and the
launch says so in a note. A case that reports a verdict without having said it started, where it had
to, is a bug in this program, and a `bookkeeping:` note says which.

**What it costs.** Two lines for each case in each pass, and a drawing suite runs ten passes: a run
of 670 cases writes 7228 lines, 474 kilobytes, none of them inside a timed interval, which is only
the submits and the waits after them. Under Eden, three runs a side, that run took 7.00 s against
6.81 s with `nojournal` (3 percent, some 27 microseconds a line), and `tools/perf.py` moved no
timed case past its threshold; the machine was not idle, so the two times are indicative.
`nojournal` in the mode file writes nothing between cases, and a crash then costs the rest of the
run.

**On an emulator that buffers.** Eden writes the card through the host's C library and seeks before
every write, so a file's last write sits in a buffer until the next one comes, and dies with the
process. Each line is therefore written without its newline, flushed, and then the newline is
written at the end: the flush puts the line on the host's disk, and what a crash can still hold is
the newline, which is why a last line that ends in its tab is read. Seeking back onto the line to
rewrite a byte already there drops, on the Android host library, every buffered byte past the seek.
A finished journal then keeps the offsets while the report loses whole suites and stays a dense
jsonl, shorter by exactly those suites. While the journal is on, the report is written the same way,
which makes true on Eden what a killed run always promised, everything up to its last completed
case.

**Under Ryujinx**, which a person starts by hand: open `edenprobe.nro`, and when Ryujinx stops or
closes, open it again, and again, until the report ends on its summary; each launch goes on with
the run. Its card is Ryujinx's own, `~/.config/Ryujinx/sdcard/edenprobe/` on Linux (or
`$XDG_CONFIG_HOME/Ryujinx/...`, or `portable/sdcard/` beside a portable build): the run folders
and the crash list are there. A console is the same: start the program again from the
menu after it ends.
