# The report

One JSON object per line, to two channels: the debug string service, which an emulator logs and a console ignores
without a debugger, and a file on the SD card.

```json
{"type":"case","suite":"clear","name":"clear to opaque red","tier":"exact","result":"pass","detail":""}
```

Each case line carries its tier ([Design](Design.md#the-four-tiers)), since a pass compares only with another of the
same tolerance: `exact`, `ulp`, `position`, `level`, or `none` for a line that compares nothing the hardware
produced (the judge's own arithmetic, a precondition such as the device opening, a drawing case stopped before it
compared, which gives the reason in its detail).

A case that could not ask its question is a `skip`, tier `none`, with a one-word `reason` and a sentence in
`detail`, counted apart:

```json
{"type":"case","suite":"acc","name":"the id token refresh completes and succeeds","tier":"none","result":"skip","reason":"not configured","detail":"needs an online session: ..."}
{"type":"summary","passed":424,"failed":9,"skipped":27,"crashed":2}
```

| reason | why the case did not ask |
|---|---|
| `crashed` | the program ended inside it on this machine, in this run (the journal) or an earlier one (the crash list) |
| `may crash` | it is in the may-crash table, and neither `fatal` nor the start screen asked for it |
| `hangs` | it is in the hang table, and `hang` did not ask for it |
| `not configured` | the net file does not name what it needs: a session, a server, a rewrite, an account, a certificate |
| `unavailable` | a service or a resource the program cannot have here |
| `depends` | a case before it did not leave what it needs |
| `unanswered` | it asked a person, and nothing was pressed in time |

The summary's four counts add up to the case lines; `skipped` leaves out the `crashed` ones, which have their own
count. The debug channel's `EDENPROBE DONE` line carries the same four. With `compare=<name>`, `baseline` lines and a
`baseline_summary` line come just before the summary ([Baselines](Baseline.md)). A run that dies halfway leaves every
line up to its last finished case. The summary is the last line, but for a detailed run nobody attended, which adds
one note after it ([On the console](Console.md)).

## Where a run leaves its report

```
sdmc:/edenprobe/
  mode              the mode file, read by every build
  net               what the net family is pointed at
  crashes           the crash list: this machine's, shared by every build
  last-run          the folder the latest launch reported in, e.g. runs/5fd6ec1_phone
  fs/               the fs family's files, written once
  service/          what the service family leaves for its next launch
  baseline/         the baselines, one file each
  runs/
    5fd6ec1/        a build, by its commit
      report.jsonl
      images/       the failing cases' pictures
      journal       where the run is, for the launch after a crash
    5fd6ec1_phone/  the same build with label=phone
    9e1c2a4-dirty/  a build of a tree with uncommitted changes
```

The folder is the commit as `git describe --always --dirty --exclude='*'` gives it (`unknown` outside git); the
report's first note names the build and the second the folder. `label=<name>` adds `_<name>`, for one build under two
emulators that share a card: 1 to 32 letters, digits, dots, dashes and underscores, not starting with a dot; anything
else is refused in the mode line and the run goes in the folder with no label. Each folder has its own journal, so a
run one build left in the middle waits while another build runs. The crash list is shared by every build. Every
launch rewrites `last-run` before it opens the report, which is how `make run` finds it.
